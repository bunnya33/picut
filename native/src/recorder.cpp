#include "recorder.h"
#include "native_windows.h"
#include "recording_capture.h"
#ifndef Q_OS_MACOS
#include "recording_clicks.h"
#endif
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QStorageInfo>
#include <QtCore/QUuid>
#ifndef Q_OS_MACOS
#include <audioclient.h>
#include <dwmapi.h>
#include <cmath>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
struct ComScope {
  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~ComScope() {
    if (SUCCEEDED(result))
      CoUninitialize();
  }
};
static void check(HRESULT result, const char *message) {
  if (FAILED(result))
    throw std::runtime_error(message);
}
QList<AudioDevice> audioDevices(bool capture) {
  ComScope scope;
  ComPtr<IMMDeviceEnumerator> enumerator;
  check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                         IID_PPV_ARGS(&enumerator)),
        "无法枚举声音设备。");
  ComPtr<IMMDeviceCollection> devices;
  check(enumerator->EnumAudioEndpoints(capture ? eCapture : eRender,
                                       DEVICE_STATE_ACTIVE, &devices),
        "无法读取声音设备。");
  UINT count = 0;
  devices->GetCount(&count);
  QList<AudioDevice> list;
  for (UINT n = 0; n < count; ++n) {
    ComPtr<IMMDevice> device;
    devices->Item(n, &device);
    LPWSTR id = nullptr;
    device->GetId(&id);
    QString key = QString::fromWCharArray(id);
    CoTaskMemFree(id);
    ComPtr<IPropertyStore> properties;
    device->OpenPropertyStore(STGM_READ, &properties);
    PROPVARIANT name;
    PropVariantInit(&name);
    properties->GetValue(PKEY_Device_FriendlyName, &name);
    list.append({key, name.vt == VT_LPWSTR
                          ? QString::fromWCharArray(name.pwszVal)
                          : key});
    PropVariantClear(&name);
  }
  return list;
}
#endif
void ActiveClock::start() {
  std::lock_guard lock(mutex);
  begun = std::chrono::steady_clock::now();
  accumulated = elapsedAtPause = 0;
  active = true;
  paused = false;
}
void ActiveClock::pause() {
  std::lock_guard lock(mutex);
  if (active && !paused) {
    accumulated +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begun)
            .count();
    elapsedAtPause = accumulated;
    paused = true;
  }
}
void ActiveClock::resume() {
  std::lock_guard lock(mutex);
  if (active && paused) {
    begun = std::chrono::steady_clock::now();
    paused = false;
  }
}
double ActiveClock::elapsed() const {
  std::lock_guard lock(mutex);
  return active && !paused
             ? accumulated + std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - begun)
                                 .count()
             : elapsedAtPause;
}
void ActiveClock::finish() {
  std::lock_guard lock(mutex);
  if (active && !paused)
    accumulated +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begun)
            .count();
  elapsedAtPause = accumulated;
  active = false;
}
#ifndef Q_OS_MACOS
class ScreenGrabber {
  HDC desktop = nullptr, memory = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ previous = nullptr;
  void *bits = nullptr;
  QRect region;

public:
  explicit ScreenGrabber(QRect r) : region(r) {
    desktop = GetDC(nullptr);
    memory = CreateCompatibleDC(desktop);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = r.width();
    info.bmiHeader.biHeight = -r.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap =
        CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
      throw std::runtime_error("无法分配录屏画面。");
    previous = SelectObject(memory, bitmap);
  }
  ~ScreenGrabber() {
    if (previous)
      SelectObject(memory, previous);
    if (bitmap)
      DeleteObject(bitmap);
    if (memory)
      DeleteDC(memory);
    if (desktop)
      ReleaseDC(nullptr, desktop);
  }
  QImage grab(bool cursor) {
    if (!BitBlt(memory, 0, 0, region.width(), region.height(), desktop,
                region.x(), region.y(), SRCCOPY | CAPTUREBLT) &&
        !BitBlt(memory, 0, 0, region.width(), region.height(), desktop,
                region.x(), region.y(), SRCCOPY))
      throw std::runtime_error("画面采集中断。");
    if (cursor) {
      CURSORINFO c{sizeof(CURSORINFO)};
      if (GetCursorInfo(&c) && c.flags & CURSOR_SHOWING) {
        ICONINFO icon{};
        if (GetIconInfo(c.hCursor, &icon)) {
          DrawIconEx(memory, c.ptScreenPos.x - region.x() - int(icon.xHotspot),
                     c.ptScreenPos.y - region.y() - int(icon.yHotspot),
                     c.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
          if (icon.hbmColor)
            DeleteObject(icon.hbmColor);
          if (icon.hbmMask)
            DeleteObject(icon.hbmMask);
        }
      }
    }
    return QImage(static_cast<uchar *>(bits), region.width(), region.height(),
                  region.width() * 4, QImage::Format_RGB32)
        .copy();
  }
  void moveTo(QPoint point) { region.moveTopLeft(point); }
};
struct AudioTrack {
  QString path, id;
  bool loopback;
  ActiveClock *clock;
  std::atomic_bool *stop, *pause, *mute;
  std::atomic_bool *begun;
  std::atomic_bool ready = false;
  QString error;
  std::mutex errorMutex;
  QString errorText() {
    std::lock_guard lock(errorMutex);
    return error;
  }
  std::thread thread;
  AudioTrack(QString p, QString key, bool loop, ActiveClock *c,
             std::atomic_bool *s, std::atomic_bool *paused,
             std::atomic_bool *muted, std::atomic_bool *started)
      : path(std::move(p)), id(std::move(key)), loopback(loop), clock(c),
        stop(s), pause(paused), mute(muted), begun(started) {
    thread = std::thread([this] { run(); });
  }
  ~AudioTrack() {
    if (thread.joinable())
      thread.join();
  }
  void run() {
    ComScope scope;
    try {
      ComPtr<IMMDeviceEnumerator> enumerator;
      check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(&enumerator)),
            "声音设备初始化失败。");
      ComPtr<IMMDevice> device;
      if (id.isEmpty())
        check(enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture,
                                                  eConsole, &device),
              "未检测到声音设备。");
      else
        check(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(id.utf16()),
                                    &device),
              "所选声音设备已断开。");
      ComPtr<IAudioClient> client;
      check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void **>(client.GetAddressOf())),
            "无法启动声音采集。");
      WAVEFORMATEX *rawFormat = nullptr;
      check(client->GetMixFormat(&rawFormat), "声音格式读取失败。");
      QByteArray format(reinterpret_cast<const char *>(rawFormat),
                        18 + rawFormat->cbSize);
      int sampleRate = rawFormat->nSamplesPerSec,
          block = rawFormat->nBlockAlign;
      HRESULT initialized = client->Initialize(
          AUDCLNT_SHAREMODE_SHARED, loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
          1000000, 0, rawFormat, nullptr);
      CoTaskMemFree(rawFormat);
      check(initialized, "声音设备初始化失败，请更换设备。");
      ComPtr<IAudioCaptureClient> capture;
      check(client->GetService(IID_PPV_ARGS(&capture)), "声音服务不可用。");
      QFile file(path);
      if (!file.open(QIODevice::ReadWrite))
        throw std::runtime_error("无法写入声音文件。");
      QDataStream stream(&file);
      stream.setByteOrder(QDataStream::LittleEndian);
      file.write("RIFF", 4);
      stream << quint32(0);
      file.write("WAVEfmt ", 8);
      stream << quint32(format.size());
      file.write(format);
      if (format.size() % 2)
        file.write("\0", 1);
      file.write("data", 4);
      qint64 sizeOffset = file.pos();
      stream << quint32(0);
      qint64 dataStart = file.pos();
      check(client->Start(), "声音采集启动失败。");
      ready = true;
      while (!stop->load() && !begun->load())
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      qint64 frames = 0;
      auto silence = [&](qint64 count) {
        QByteArray zero(4096 * block, '\0');
        while (count > 0) {
          auto amount = qMin(count, qint64(4096));
          file.write(zero.constData(), amount * block);
          count -= amount;
          frames += amount;
        }
      };
      while (!stop->load()) {
        UINT packet = 0;
        check(capture->GetNextPacketSize(&packet), "声音设备已断开。");
        while (packet) {
          BYTE *data = nullptr;
          UINT count = 0;
          DWORD flags = 0;
          check(capture->GetBuffer(&data, &count, &flags, nullptr, nullptr),
                "无法读取声音数据。");
          if (!pause->load()) {
            auto target = qint64(clock->elapsed() * sampleRate);
            if (target > frames + count + sampleRate / 10)
              silence(target - frames - count);
            if (mute->load() || flags & AUDCLNT_BUFFERFLAGS_SILENT)
              silence(count);
            else {
              file.write(reinterpret_cast<const char *>(data),
                         qint64(count) * block);
              frames += count;
            }
          }
          capture->ReleaseBuffer(count);
          capture->GetNextPacketSize(&packet);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
      client->Stop();
      auto target = qint64(clock->elapsed() * sampleRate);
      if (target > frames)
        silence(target - frames);
      auto end = file.pos();
      file.seek(sizeOffset);
      stream << quint32(end - dataStart);
      file.seek(4);
      stream << quint32(end - 8);
      file.close();
    } catch (const std::exception &e) {
      std::lock_guard lock(errorMutex);
      error = QString::fromUtf8(e.what());
      ready = true;
    }
  }
};
#endif
Recorder::Recorder(QString folder, QObject *parent)
    : QObject(parent), root(std::move(folder)) {}
Recorder::~Recorder() {
  stop();
  if (worker.joinable())
    worker.join();
}
#ifndef Q_OS_MACOS
QString Recorder::ffmpegPath() {
  auto file =
      QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
  if (QFileInfo::exists(file))
    return file;
  auto override = qEnvironmentVariable("FLOATSHOT_FFMPEG");
  return override.isEmpty() ? QString("ffmpeg") : override;
}
#endif
void Recorder::start(RecordOptions options, std::function<QImage()> source) {
  if (active())
    throw std::runtime_error("已有录制正在进行。");
  if (QFileInfo::exists(options.output))
    throw std::runtime_error("目标文件已存在，请更换文件名。");
  QDir().mkpath(QFileInfo(options.output).absolutePath());
  QStorageInfo disk(QFileInfo(options.output).absolutePath());
  if (disk.bytesAvailable() < 256LL * 1024 * 1024)
    throw std::runtime_error("磁盘空间不足 256 MB。");
  if (worker.joinable())
    worker.join();
  stopFlag = pauseFlag = muteSystem = muteMic = begunFlag = false;
  userPaused = targetUnavailable = false;
  running = true;
  worker = std::thread([this, options, source] { run(options, source); });
}
void Recorder::pause() {
  if (!active() || stopFlag || !begunFlag)
    return;
  userPaused=!userPaused;
  syncPause();
}
void Recorder::setTargetUnavailable(bool unavailable) {
  targetUnavailable=unavailable;
  if (active() && begunFlag && !stopFlag) syncPause();
}
void Recorder::syncPause() {
  const bool next=userPaused || targetUnavailable;
  if (next==pauseFlag.load()) return;
  if (!next) {
    clock.resume();
    pauseFlag = false;
  } else {
    clock.pause();
    pauseFlag = true;
  }
}
void Recorder::stop() {
  if (active() && !stopFlag) {
    clock.finish();
    stopFlag = true;
    emit status("正在保存录屏…");
  }
}
void Recorder::setMuted(bool system, bool value) {
  (system ? muteSystem : muteMic).store(value);
}
#ifndef Q_OS_MACOS
void Recorder::run(const RecordOptions &options,
                   const std::function<QImage()> &source) {
  auto work = QDir(root).filePath(QUuid::createUuid().toString(QUuid::Id128));
  QDir().mkpath(work);
  auto video = work + "/screen.mp4";
  std::vector<std::unique_ptr<AudioTrack>> tracks;
  QProcess encoder;
  encoder.setStandardErrorFile(work + "/ffmpeg-video.log");
  RecordingClicks clicks;
  try {
    if (options.clickFeedback && !clicks.start([this,&options] {
      const auto state=options.live?options.live->snapshot():RecordingLiveState::Snapshot{options.region,options.painter};
      auto region=state.region;
      bool available=true;
      if (options.window) {
        const auto window=reinterpret_cast<HWND>(options.window);
        available=IsWindow(window) && !IsIconic(window) && IsWindowVisible(window);
        RECT bounds{};
        if (available && SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))))
          region=QRect(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top);
        else available=false;
      }
      return RecordingClicks::Target{region,state.controls,available && begunFlag && !pauseFlag && !stopFlag,
                                    RecordingClicks::mediaTime(clock.elapsed())};
    })) emit warning("鼠标点击反馈未能启用，本次视频继续录制。");
    if (options.systemAudio)
      tracks.push_back(std::make_unique<AudioTrack>(
          work + "/system.wav", options.speakerId, true, &clock, &stopFlag,
          &pauseFlag, &muteSystem, &begunFlag));
    if (options.microphone)
      tracks.push_back(std::make_unique<AudioTrack>(
          work + "/mic.wav", options.micId, false, &clock, &stopFlag,
          &pauseFlag, &muteMic, &begunFlag));
    for (auto &track : tracks) {
      auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(8);
      while (!track->ready && std::chrono::steady_clock::now() < deadline &&
             !stopFlag)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      auto audioError = track->errorText();
      if (!track->ready || !audioError.isEmpty())
        throw std::runtime_error(audioError.isEmpty()
                                     ? "声音设备初始化超时。"
                                     : audioError.toStdString());
    }
    if (stopFlag)
      throw std::runtime_error("录制已取消。");
    std::unique_ptr<ScreenGrabber> grabber;
    std::unique_ptr<RecordingWindowCapture> windowGrabber;
    if (!source && options.window)
      windowGrabber=std::make_unique<RecordingWindowCapture>(options.window,options.cursor);
    else if (!source)
      grabber = std::make_unique<ScreenGrabber>(options.region);
    QStorageInfo disk(QFileInfo(options.output).absolutePath());
    auto frame = [&] {
      const auto state=options.live?options.live->snapshot():RecordingLiveState::Snapshot{options.region,options.painter};
      if (grabber) grabber->moveTo(state.region.topLeft());
      auto image=(source ? source() : windowGrabber?windowGrabber->grab():grabber->grab(options.cursor)).convertToFormat(QImage::Format_RGB32);
      if (options.window && !image.isNull() && image.size()!=options.region.size())
        image=image.scaled(options.region.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
      if (!image.isNull() && state.painter) state.painter(image);
      return image;
    };
    auto first = frame();
    if (windowGrabber) {
      QElapsedTimer deadline; deadline.start();
      while (first.isNull() && deadline.elapsed()<5000 && !stopFlag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); first=frame();
      }
    }
    int w = first.width(), h = first.height();
    if (w < 2 || h < 2)
      throw std::runtime_error("录制区域无效。");
    const bool originalResolution = options.quality <= 14;
    double ratio = originalResolution ? 1. : qMin(1.0, qMin(1920.0 / w, 1080.0 / h));
    int outW = originalResolution ? (w + 1) / 2 * 2 : qMax(2, qRound(w * ratio) / 2 * 2),
        outH = originalResolution ? (h + 1) / 2 * 2 : qMax(2, qRound(h * ratio) / 2 * 2);
    QStringList args{"-y",
                     "-hide_banner",
                     "-loglevel",
                     "warning",
                     "-f",
                     "rawvideo",
                     "-pixel_format",
                     "bgra",
                     "-video_size",
                     QString("%1x%2").arg(w).arg(h),
                     "-framerate",
                     QString::number(options.fps),
                     "-i",
                     "pipe:0",
                     "-an",
                     "-vf",
                     QString(originalResolution ? "pad=%1:%2:0:0" : "scale=%1:%2").arg(outW).arg(outH),
                     "-c:v",
                     "libx264",
                     "-preset",
                     "veryfast",
                     "-crf",
                     QString::number(options.quality),
                     "-pix_fmt",
                     "yuv420p",
                     "-movflags",
                     "+faststart",
                     video};
    encoder.start(ffmpegPath(), args);
    if (!encoder.waitForStarted(10000))
      throw std::runtime_error(("无法启动 FFmpeg 编码器：" + encoder.errorString()).toUtf8().constData());
    auto thumbnail = QFileInfo(root).absolutePath() + "/history/" +
                     QUuid::createUuid().toString(QUuid::Id128) + ".png";
    first.scaled(384, 216, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .save(thumbnail);
    qint64 frames = 0;
    auto writeFrame = [&](QImage image) {
      // Use the encoded frame's media time, including catch-up frames. A slow
      // encoder must not let a short click expire before any video frame sees it.
      if (options.clickFeedback) clicks.paint(image,RecordingClicks::mediaTime(double(frames)/options.fps));
      auto bytes = qint64(image.width()) * image.height() * 4;
      if (encoder.write(reinterpret_cast<const char *>(image.constBits()),
                        bytes) != bytes)
        throw std::runtime_error("视频编码管道写入失败。");
      while (encoder.bytesToWrite() > bytes)
        if (!encoder.waitForBytesWritten(10000))
          throw std::runtime_error("视频编码器没有响应。");
    };
    clock.start();
    begunFlag = true;
    emit started();
    QImage last = first;
    first = {};
    double diskCheck = 0;
    while (!stopFlag) {
      if (options.window && !IsWindow(reinterpret_cast<HWND>(options.window))) { stop(); break; }
      if (pauseFlag) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      auto elapsed = clock.elapsed();
      auto target = qint64(elapsed * options.fps) + 1;
      if (target > frames) {
        auto image = frame();
        if (windowGrabber && image.isNull()) image=last;
        if (image.size() != last.size())
          throw std::runtime_error("显示器尺寸改变，录制已停止。");
        if (target - frames > 120)
          throw std::runtime_error("编码速度不足，请降低帧率或录制尺寸。");
        while (frames < target - 1) {
          writeFrame(last);
          ++frames;
        }
        writeFrame(image);
        last = image;
        ++frames;
      }
      if (elapsed - diskCheck > 2) {
        diskCheck = elapsed;
        disk.refresh();
        if (disk.bytesAvailable() < 128LL * 1024 * 1024) {
          emit warning("磁盘空间不足，正在保存已录内容。");
          stop();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto target =
        qMax(qint64(1), qint64(std::ceil(clock.elapsed() * options.fps)));
    while (frames < target) {
      writeFrame(last);
      ++frames;
    }
    last = {};
    clicks.stop();
    encoder.closeWriteChannel();
    if (!encoder.waitForFinished(120000) || encoder.exitCode() != 0)
      throw std::runtime_error("视频编码失败，原始片段已保留。");
    for (auto &track : tracks)
      if (track->thread.joinable())
        track->thread.join();
    auto duration = double(frames) / options.fps;
    QString staging = options.output + ".part.mp4";
    if (QFileInfo::exists(staging))
      staging = options.output + "." +
                QUuid::createUuid().toString(QUuid::Id128) + ".part.mp4";
    if (tracks.empty()) {
      if (!QFile::copy(video, staging))
        throw std::runtime_error("无法保存视频文件。");
    } else {
      emit status("正在合成声音…");
      QStringList mux{"-y", "-hide_banner", "-i", video};
      QStringList filters;
      int count = 0;
      for (auto &track : tracks) {
        if (!track->errorText().isEmpty())
          emit warning(track->errorText());
        if (QFileInfo(track->path).size() <= 44)
          continue;
        mux << "-i" << track->path;
        filters << QString(
                       "[%1:a]apad,atrim=duration=%2,asetpts=PTS-STARTPTS[a%3]")
                       .arg(count + 1)
                       .arg(duration, 0, 'f', 6)
                       .arg(count);
        ++count;
      }
      if (!count) {
        if (!QFile::copy(video, staging))
          throw std::runtime_error("无法保存录屏。");
      } else {
        QString audio = "[a0]";
        if (count > 1) {
          QString inputs;
          for (int n = 0; n < count; ++n)
            inputs += QString("[a%1]").arg(n);
          filters << inputs +
                         QString("amix=inputs=%1:normalize=0:duration=longest,"
                                 "alimiter=limit=0.95:latency=1[mixed]")
                             .arg(count);
          audio = "[mixed]";
        }
        mux << "-filter_complex" << filters.join(';') << "-map" << "0:v:0"
            << "-map" << audio << "-c:v" << "copy" << "-c:a" << "aac" << "-b:a"
            << "192k" << "-t" << QString::number(duration, 'f', 6)
            << "-movflags" << "+faststart" << staging;
        QProcess process;
        process.start(ffmpegPath(), mux);
        if (!process.waitForFinished(180000) || process.exitCode() != 0)
          throw std::runtime_error("声音合成失败，原始片段已保留。");
      }
    }
    if (!QFile::rename(staging, options.output))
      throw std::runtime_error("无法发布视频，请检查目标目录。");
    QDir(work).removeRecursively();
    running = false;
    emit finished({{"id", QUuid::createUuid().toString(QUuid::Id128)},
                   {"kind", "video"},
                   {"path", options.output},
                   {"thumbnail", thumbnail},
                   {"width", outW},
                   {"height", outH},
                   {"duration", duration},
                   {"click_feedback",options.clickFeedback},
                   {"click_feedback_events",qint64(clicks.count())},
                   {"frames", frames}});
  } catch (const std::exception &e) {
    clock.finish();
    stopFlag = true;
    for (auto &track : tracks)
      if (track->thread.joinable())
        track->thread.join();
    if (encoder.state() != QProcess::NotRunning) {
      encoder.kill();
      encoder.waitForFinished(5000);
    }
    running = false;
    QFile log(work + "/ffmpeg-video.log");
    QString detail;
    if (log.open(QIODevice::ReadOnly)) {
      log.seek(qMax(qint64(0), log.size()-1600));
      detail=QString::fromUtf8(log.readAll()).trimmed();
    }
    emit failed(QString::fromUtf8(e.what()) + (detail.isEmpty()?QString():"\n编码器详情：\n"+detail) + "\n原始录制目录：" + work);
  }
}

#endif
