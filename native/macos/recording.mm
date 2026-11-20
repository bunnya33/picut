#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreGraphics/CoreGraphics.h>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStorageInfo>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QPainter>
#include <QtWidgets/QToolButton>
#include <cmath>

#include "recorder.h"
#include "recording_capture.h"
#include "recording_overlay.h"
#include "screen_capture.h"

namespace {
std::runtime_error mediaError(AVAssetWriter *writer, const char *fallback) {
  return std::runtime_error(writer.error ? QString::fromNSString(writer.error.localizedDescription).toStdString()
                                        : fallback);
}

NSURL *fileUrl(const QString &file) {
  return [NSURL fileURLWithPath:file.toNSString()];
}

class MovieWriter {
public:
  MovieWriter(const QString &file, QSize size, const RecordOptions &options)
      : dimensions(size), fps(options.fps) {
    writer = [[AVAssetWriter alloc] initWithURL:fileUrl(file) fileType:AVFileTypeMPEG4 error:nil];
    if (!writer) throw std::runtime_error("无法创建 MP4 文件。");
    writer.shouldOptimizeForNetworkUse = YES;
    const double bitsPerPixel = options.quality <= 14 ? .27 : options.quality <= 18 ? .19 :
                                options.quality >= 28 ? .08 : .12;
    const auto bitrate = qBound(500000, qRound(size.width() * double(size.height()) * fps * bitsPerPixel), 100000000);
    video = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:@{
      AVVideoCodecKey: AVVideoCodecTypeH264, AVVideoWidthKey: @(size.width()),
      AVVideoHeightKey: @(size.height()), AVVideoCompressionPropertiesKey: @{
        AVVideoAverageBitRateKey: @(bitrate), AVVideoExpectedSourceFrameRateKey: @(fps)}
    }];
    video.expectsMediaDataInRealTime = YES;
    pixels = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:video
        sourcePixelBufferAttributes:@{
          (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
          (NSString *)kCVPixelBufferWidthKey: @(size.width()),
          (NSString *)kCVPixelBufferHeightKey: @(size.height()),
          (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
        }];
    if (![writer canAddInput:video]) throw mediaError(writer, "视频尺寸超出编码器支持范围。");
    [writer addInput:video];
    if (options.systemAudio) addAudio(0);
    if (options.microphone) addAudio(1);
    if (![writer startWriting]) throw mediaError(writer, "无法启动视频编码器。");
    [writer startSessionAtSourceTime:kCMTimeZero];
  }

  ~MovieWriter() {
    if (writer.status == AVAssetWriterStatusWriting) [writer cancelWriting];
  }

  void addAudio(int track) {
    auto input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:@{
      AVFormatIDKey: @(kAudioFormatMPEG4AAC), AVSampleRateKey: @48000,
      AVNumberOfChannelsKey: @2, AVEncoderBitRateKey: @160000
    }];
    input.expectsMediaDataInRealTime = YES;
    if (![writer canAddInput:input]) throw mediaError(writer, "无法创建声音轨道。");
    [writer addInput:input];
    audio[track] = input;
  }

  void writeAudio(int track, CMSampleBufferRef sample, double elapsed, bool muted) {
    auto input = audio[track];
    if (!input.readyForMoreMediaData || muted) return;
    const auto original = CMSampleBufferGetPresentationTimeStamp(sample);
    const auto lag = CMTimeGetSeconds(CMTimeSubtract(CMClockGetTime(CMClockGetHostTimeClock()), original));
    const double when = elapsed - qBound(0.0, lag, .5);
    if (!std::isfinite(when) || when < 0 || when < audioEnd[track]) return;
    CMItemCount count = 0;
    if (CMSampleBufferGetSampleTimingInfoArray(sample, 0, nullptr, &count) || count < 1) return;
    std::vector<CMSampleTimingInfo> timing(count);
    if (CMSampleBufferGetSampleTimingInfoArray(sample, count, timing.data(), nullptr)) return;
    const auto shift = CMTimeSubtract(CMTimeMakeWithSeconds(when, 48000), original);
    for (auto &entry : timing) {
      entry.presentationTimeStamp = CMTimeAdd(entry.presentationTimeStamp, shift);
      if (CMTIME_IS_VALID(entry.decodeTimeStamp))
        entry.decodeTimeStamp = CMTimeAdd(entry.decodeTimeStamp, shift);
    }
    CMSampleBufferRef adjusted = nullptr;
    if (CMSampleBufferCreateCopyWithNewTiming(nullptr, sample, count, timing.data(), &adjusted)) return;
    if ([input appendSampleBuffer:adjusted]) {
      const double duration = CMTimeGetSeconds(CMSampleBufferGetDuration(sample));
      audioEnd[track] = when + (std::isfinite(duration) ? duration : 0);
      hasAudio[track] = true;
    }
    CFRelease(adjusted);
  }

  void writeVideo(const QImage &image, qint64 frame, bool pad) {
    @autoreleasepool {
      const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!video.readyForMoreMediaData && writer.status == AVAssetWriterStatusWriting &&
             std::chrono::steady_clock::now() < timeout)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      if (!video.readyForMoreMediaData) throw mediaError(writer, "视频编码器没有响应。");
      CVPixelBufferRef buffer = nullptr;
      if (CVPixelBufferPoolCreatePixelBuffer(nullptr, pixels.pixelBufferPool, &buffer) != kCVReturnSuccess)
        throw std::runtime_error("无法分配视频编码缓冲区。");
      if (CVPixelBufferLockBaseAddress(buffer, 0) != kCVReturnSuccess) {
        CVPixelBufferRelease(buffer);
        throw std::runtime_error("无法写入视频编码缓冲区。");
      }
      QImage target(static_cast<uchar *>(CVPixelBufferGetBaseAddress(buffer)),
                    dimensions.width(), dimensions.height(),
                    qsizetype(CVPixelBufferGetBytesPerRow(buffer)), QImage::Format_RGB32);
      target.fill(Qt::black);
      QPainter painter(&target);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      if (pad) painter.drawImage(QPoint(), image);
      else painter.drawImage(target.rect(), image);
      painter.end();
      CVPixelBufferUnlockBaseAddress(buffer, 0);
      const auto success = [pixels appendPixelBuffer:buffer withPresentationTime:CMTimeMake(frame, fps)];
      CVPixelBufferRelease(buffer);
      if (!success) throw mediaError(writer, "写入视频帧失败。");
    }
  }

  bool finish(qint64 frames) {
    [video markAsFinished];
    for (auto input : audio)
      [input markAsFinished];
    [writer endSessionAtSourceTime:CMTimeMake(frames, fps)];
    auto complete = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(complete); }];
    if (dispatch_semaphore_wait(complete, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) ||
        writer.status != AVAssetWriterStatusCompleted)
      throw mediaError(writer, "保存录屏超时。");
    return hasAudio[0] && hasAudio[1];
  }

private:
  QSize dimensions;
  int fps;
  AVAssetWriter *writer = nil;
  AVAssetWriterInput *video = nil;
  AVAssetWriterInputPixelBufferAdaptor *pixels = nil;
  AVAssetWriterInput *audio[2]{};
  double audioEnd[2]{};
  bool hasAudio[2]{};
};

void mixMovie(const QString &source, const QString &destination) {
  auto asset = [AVURLAsset URLAssetWithURL:fileUrl(source) options:nil];
  auto exportSession = [[AVAssetExportSession alloc] initWithAsset:asset presetName:AVAssetExportPresetHighestQuality];
  if (!exportSession) throw std::runtime_error("无法合成录屏声音。");
  auto mix = [AVMutableAudioMix audioMix];
  NSMutableArray *tracks = [NSMutableArray array];
  for (AVAssetTrack *track in [asset tracksWithMediaType:AVMediaTypeAudio]) {
    auto input = [AVMutableAudioMixInputParameters audioMixInputParametersWithTrack:track];
    [input setVolume:.5 atTime:kCMTimeZero];
    [tracks addObject:input];
  }
  mix.inputParameters = tracks;
  exportSession.audioMix = mix;
  exportSession.outputURL = fileUrl(destination);
  exportSession.outputFileType = AVFileTypeMPEG4;
  exportSession.shouldOptimizeForNetworkUse = YES;
  auto complete = dispatch_semaphore_create(0);
  [exportSession exportAsynchronouslyWithCompletionHandler:^{ dispatch_semaphore_signal(complete); }];
  if (dispatch_semaphore_wait(complete, dispatch_time(DISPATCH_TIME_NOW, 180 * NSEC_PER_SEC)) ||
      exportSession.status != AVAssetExportSessionStatusCompleted) {
    [exportSession cancelExport];
    throw std::runtime_error("声音合成失败，原始录屏已保留。");
  }
}

class ClickFeedback {
public:
  void update(QRect region, QRect controls, double timestamp) {
    if (region.isEmpty()) return;
    const auto event = CGEventCreate(nullptr);
    if (!event) return;
    const auto cursor = CGEventGetLocation(event);
    CFRelease(event);
    const QPoint point(qRound(cursor.x), qRound(cursor.y));
    for (unsigned button = 0; button < 3; ++button) {
      const bool down = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, CGMouseButton(button));
      if (down && !pressed[button] && region.contains(point) && !controls.contains(point)) {
        clicks.append({QPointF(double(point.x() - region.x()) / region.width(),
                              double(point.y() - region.y()) / region.height()), timestamp, button});
        ++total;
      }
      pressed[button] = down;
    }
  }

  void draw(QImage &frame, double time) {
    while (!clicks.isEmpty() && time - clicks.first().time > .35) clicks.removeFirst();
    if (clicks.isEmpty()) return;
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto &click : std::as_const(clicks)) {
      const double phase = qBound(0., (time - click.time) / .35, 1.);
      QColor color(click.button == 0 ? "#ffca38" : click.button == 1 ? "#ff5d64" : "#51c4ff");
      color.setAlphaF(1 - phase);
      painter.setPen(QPen(color, 3));
      const QPointF center(click.position.x() * frame.width(), click.position.y() * frame.height());
      painter.drawEllipse(center, 8 + phase * 18, 8 + phase * 18);
    }
  }

  int total = 0;
private:
  struct Click { QPointF position; double time; unsigned button; };
  QList<Click> clicks;
  bool pressed[3]{};
};
}

QList<AudioDevice> audioDevices(bool capture) {
  if (!capture) return {};
  @autoreleasepool {
    auto discovery = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeMicrophone]
        mediaType:AVMediaTypeAudio position:AVCaptureDevicePositionUnspecified];
    QList<AudioDevice> devices;
    for (AVCaptureDevice *device in discovery.devices)
      devices.append({QString::fromNSString(device.uniqueID), QString::fromNSString(device.localizedName)});
    return devices;
  }
}

void Recorder::run(const RecordOptions &options, const std::function<QImage()> &source) {
  @autoreleasepool {
    const auto id = QUuid::createUuid().toString(QUuid::Id128);
    const auto staging = options.output + "." + id + ".part.mp4";
    const auto mixed = options.output + "." + id + ".mix.mp4";
    const auto thumbnail = QFileInfo(root).absolutePath() + "/history/" + id + ".png";
    try {
      std::unique_ptr<MovieWriter> movie;
      std::unique_ptr<MacScreenStream> stream;
      if (!source) {
        if (!checkMacScreenPermission(false)) throw std::runtime_error("请先允许屏幕与系统音频录制权限。");
        if (options.microphone && !checkMacMicrophonePermission())
          throw std::runtime_error("请在系统设置 → 隐私与安全性 → 麦克风中允许 picut。");
        stream = std::make_unique<MacScreenStream>(options.logicalRegion, options.region.size(), options.fps,
            options.cursor, options.window, options.systemAudio, options.microphone, options.micId,
            [this, &movie](int track, const void *sample) {
              if (!begunFlag || pauseFlag || stopFlag) return;
              movie->writeAudio(track, static_cast<CMSampleBufferRef>(const_cast<void *>(sample)),
                                clock.elapsed(), track == 0 ? muteSystem.load() : muteMic.load());
            });
      }
      auto frame = [&] { return source ? source() : stream->frame(); };
      auto last = frame();
      QElapsedTimer wait;
      wait.start();
      while (last.isNull() && wait.elapsed() < 5000 && !stopFlag) {
        if (stream && !stream->failure().isEmpty()) throw std::runtime_error(stream->failure().toStdString());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        last = frame();
      }
      if (stopFlag) throw std::runtime_error("录制已取消。");
      if (last.width() < 2 || last.height() < 2) throw std::runtime_error("没有可录制的画面。");
      const bool original = options.quality <= 14;
      QSize output = last.size();
      if (!original && (output.width() > 1920 || output.height() > 1080))
        output.scale(1920, 1080, Qt::KeepAspectRatio);
      output.setWidth((output.width() + 1) & ~1);
      output.setHeight((output.height() + 1) & ~1);
      movie = std::make_unique<MovieWriter>(staging, output, options);
      ClickFeedback clicks;
      qint64 frames = 0;
      auto write = [&](QImage image) {
        if (options.clickFeedback) clicks.draw(image, double(frames) / options.fps);
        movie->writeVideo(image, frames, original);
        ++frames;
      };
      auto state = options.live ? options.live->snapshot() : RecordingLiveState::Snapshot{options.region, options.painter, {}};
      if (state.painter) state.painter(last);
      last.scaled(384, 216, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(thumbnail);
      write(last);
      clock.start();
      begunFlag = true;
      emit started();
      QStorageInfo disk(QFileInfo(options.output).absolutePath());
      double diskCheck = 0;
      while (!stopFlag) {
        const auto elapsed = clock.elapsed();
        if (stream && !stream->failure().isEmpty()) {
          emit warning(stream->failure());
          stop();
          break;
        }
        if (pauseFlag) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        state = options.live ? options.live->snapshot() : RecordingLiveState::Snapshot{options.region, options.painter, {}};
        if (options.clickFeedback) {
          auto logical = state.region;
          if (options.window) logical = recordingWindowInfo(options.window).logical;
          else {
            logical.setSize(state.logicalSize.isEmpty() ? options.logicalRegion.size() : state.logicalSize);
          }
          clicks.update(logical, state.controls, elapsed);
        }
        const auto target = qint64(elapsed * options.fps) + 1;
        if (target > frames) {
          if (target - frames > 120) throw std::runtime_error("编码速度不足，请降低录制帧率或尺寸。");
          if (stream && !options.window) {
            // The live rectangle carries a logical origin and its native pixel size.
            const auto logical = QRect(state.region.topLeft(), state.logicalSize);
            stream->moveTo(logical.isEmpty() ? options.logicalRegion : logical);
          }
          auto image = frame();
          if (!image.isNull() && state.painter) state.painter(image);
          while (frames < target - 1) write(last);
          if (!image.isNull()) last = std::move(image);
          write(last);
        }
        if (elapsed - diskCheck > 2) {
          diskCheck = elapsed;
          disk.refresh();
          if (disk.bytesAvailable() < 128LL * 1024 * 1024) {
            emit warning("磁盘空间不足，正在保存已录内容。");
            stop();
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
      stream.reset();
      auto target = qMax(qint64(1), qint64(std::ceil(clock.elapsed() * options.fps)));
      while (frames < target) write(last);
      QString ready = staging;
      if (movie->finish(frames)) {
        emit status("正在合成声音…");
        mixMovie(staging, mixed);
        ready = mixed;
      }
      if (!QFile::rename(ready, options.output)) throw std::runtime_error("无法保存录屏，请检查目标目录。");
      if (ready == mixed) QFile::remove(staging);
      running = false;
      emit finished({{"id", id}, {"kind", "video"}, {"path", options.output},
                     {"thumbnail", thumbnail}, {"width", output.width()}, {"height", output.height()},
                     {"duration", double(frames) / options.fps}, {"frames", frames},
                     {"click_feedback", options.clickFeedback}, {"click_feedback_events", clicks.total}});
    } catch (const std::exception &error) {
      begunFlag = false;
      clock.finish();
      QFile::remove(thumbnail);
      QFile::remove(mixed);
      running = false;
      emit failed(QString::fromUtf8(error.what()) +
                  (QFileInfo(staging).size() > 0 ? "\n临时文件：" + staging : QString()));
      if (QFileInfo(staging).size() == 0) QFile::remove(staging);
    }
  }
}

bool verifyMacRecording(const QString &folder) {
  QDir().mkpath(folder);
  const auto path = QDir(folder).filePath("synthetic.mp4");
  QFile::remove(path);
  Recorder recorder(QDir(folder).filePath("recording"));
  std::unique_ptr<RecordingOverlay> overlay;
  RecordOptions options;
  options.region = {0, 0, 320, 180};
  options.output = path;
  options.fps = 30;
  QEventLoop loop;
  QJsonObject result;
  QObject::connect(&recorder, &Recorder::finished, &loop, [&](const QJsonObject &value) {
    overlay.reset();
    result = value;
    loop.quit();
  });
  QObject::connect(&recorder, &Recorder::failed, &loop, [&](const QString &error) {
    qWarning("%s", qPrintable(error));
    loop.quit();
  });
  overlay = std::make_unique<RecordingOverlay>(&recorder, QRect(100, 155, 320, 180), false, false);
  overlay->configureLive(std::make_shared<RecordingLiveState>(options.region));
  if (!overlay->show()) return false;
  auto stop = overlay->controls()->findChild<QToolButton *>("stopRecording");
  if (!stop) return false;
  QObject::connect(&recorder, &Recorder::started, &loop, [stop] {
    QTimer::singleShot(700, stop, [stop] {
      stop->setFocus();
      stop->click();
    });
  });
  QTimer::singleShot(20000, &loop, &QEventLoop::quit);
  recorder.start(options, [] {
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor("#2975ba"));
    return image;
  });
  loop.exec();
  if (result["frames"].toInt() < 15 || QFileInfo(path).size() < 1000) return false;
  auto asset = [AVURLAsset URLAssetWithURL:fileUrl(path) options:nil];
  return [asset tracksWithMediaType:AVMediaTypeVideo].count == 1 && CMTimeGetSeconds(asset.duration) > .5;
}
