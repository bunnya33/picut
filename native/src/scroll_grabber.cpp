#include "scroll_grabber.h"
#ifdef __APPLE__
#include "screen_capture.h"
#else
#include <Windows.h>
#endif
#include <chrono>
#include <memory>
#include <algorithm>
#include <vector>
#include <QtCore/QDebug>
#ifndef Q_OS_MACOS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

namespace {
qint64 monotonicNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// GDI objects are created, used and destroyed on the acquiring thread. A DIB is
// reused for the whole session; only changed frames need an owned pixel copy.
#ifndef Q_OS_MACOS
class RegionGrabber {
public:
  explicit RegionGrabber(QRect value): region(value) {
    source=GetDC(nullptr); target=CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=region.width(); info.bmiHeader.biHeight=-region.height();
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(target && bitmap) old=SelectObject(target,bitmap);
  }
  ~RegionGrabber() {
    if(old) SelectObject(target,old);
    if(bitmap) DeleteObject(bitmap);
    if(target) DeleteDC(target);
    if(source) ReleaseDC(nullptr,source);
  }
  QImage grab() {
    if(!source || !target || !bitmap ||
       !(BitBlt(target,0,0,region.width(),region.height(),source,region.x(),region.y(),SRCCOPY|CAPTUREBLT) ||
         BitBlt(target,0,0,region.width(),region.height(),source,region.x(),region.y(),SRCCOPY))) return {};
    GdiFlush();
    QImage view(static_cast<uchar *>(pixels),region.width(),region.height(),QImage::Format_RGB32);
    if(last!=view) last=view.copy();
    return last;
  }
private:
  QRect region;
  HDC source=nullptr,target=nullptr;
  HBITMAP bitmap=nullptr;
  HGDIOBJ old=nullptr;
  void *pixels=nullptr;
  QImage last;
};

class DuplicationGrabber {
  template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
public:
  explicit DuplicationGrabber(QRect value):region(value) {
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr)) { failure=QString("DXGI factory 0x%1").arg(quint32(hr),8,16,QChar('0')); return; }
    for(UINT a=0;;++a) {
      ComPtr<IDXGIAdapter1> adapter;
      if(factory->EnumAdapters1(a,&adapter)==DXGI_ERROR_NOT_FOUND) break;
      for(UINT o=0;;++o) {
        ComPtr<IDXGIOutput> output;
        if(adapter->EnumOutputs(o,&output)==DXGI_ERROR_NOT_FOUND) break;
        DXGI_OUTPUT_DESC desc{}; if(FAILED(output->GetDesc(&desc))) continue;
        const auto r=desc.DesktopCoordinates;
        const QRect bounds(r.left,r.top,r.right-r.left,r.bottom-r.top);
        if(!desc.AttachedToDesktop || !bounds.contains(region)) continue;
        if(desc.Rotation!=DXGI_MODE_ROTATION_IDENTITY) { failure="rotated display uses GDI"; return; }
        origin=bounds.topLeft();
        hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                             nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(hr)) { failure=QString("D3D device 0x%1").arg(quint32(hr),8,16,QChar('0')); return; }
        ComPtr<IDXGIOutput1> output1; hr=output.As(&output1);
        if(SUCCEEDED(hr)) hr=output1->DuplicateOutput(device.Get(),&duplication);
        if(FAILED(hr)) { failure=QString("DuplicateOutput 0x%1").arg(quint32(hr),8,16,QChar('0')); return; }
        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width=UINT(region.width()); stagingDesc.Height=UINT(region.height());
        stagingDesc.MipLevels=1; stagingDesc.ArraySize=1; stagingDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count=1; stagingDesc.Usage=D3D11_USAGE_STAGING; stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr=device->CreateTexture2D(&stagingDesc,nullptr,&staging);
        if(FAILED(hr)) { failure="DXGI staging texture unavailable"; duplication.Reset(); }
        return;
      }
    }
    failure="selection is not within one active display";
  }
  bool ready() const { return duplication && staging; }
  bool hasFrame() const { return !last.isNull(); }
  bool desktopUpdated() const { return updated; }
  qint64 readbackDurationNs() const { return readbackNs; }
  qint64 presentedAtNs() const { return presentationNs; }
  QString error() const { return failure; }
  QImage grab() {
    updated=false; readbackNs=0; presentationNs=0;
    if(!ready()) return {};
    DXGI_OUTDUPL_FRAME_INFO info{}; ComPtr<IDXGIResource> resource;
    auto hr=duplication->AcquireNextFrame(16,&info,&resource);
    if(hr==DXGI_ERROR_WAIT_TIMEOUT) return last;
    if(FAILED(hr)) { failure=QString("AcquireNextFrame 0x%1").arg(quint32(hr),8,16,QChar('0')); duplication.Reset(); return {}; }
    if(last.isNull()) qInfo()<<"Scroll initial DXGI frame:"<<"desktop_present"<<bool(info.LastPresentTime.QuadPart)
                            <<"accumulated_frames"<<info.AccumulatedFrames<<"metadata_bytes"<<info.TotalMetadataBufferSize;
    // Pointer-only notifications on a newly duplicated output may contain
    // an uninitialized black texture. Wait for a real desktop presentation.
    if(!info.LastPresentTime.QuadPart) { duplication->ReleaseFrame(); return last; }
    updated=true;
    LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0) {
      const auto ticks=info.LastPresentTime.QuadPart;
      presentationNs=(ticks/frequency.QuadPart)*1000000000LL+
                     (ticks%frequency.QuadPart)*1000000000LL/frequency.QuadPart;
    }
    if(!last.isNull() && info.TotalMetadataBufferSize && info.TotalMetadataBufferSize<=1024*1024) {
      const QRect local=region.translated(-origin);
      const auto touches=[&](const RECT &r) {
        return QRect(r.left,r.top,r.right-r.left,r.bottom-r.top).intersects(local);
      };
      const UINT bytes=info.TotalMetadataBufferSize;
      std::vector<DXGI_OUTDUPL_MOVE_RECT> moves((bytes+sizeof(DXGI_OUTDUPL_MOVE_RECT)-1)/sizeof(DXGI_OUTDUPL_MOVE_RECT));
      UINT moveBytes=bytes;
      bool relevant=true;
      if(SUCCEEDED(duplication->GetFrameMoveRects(bytes,moves.data(),&moveBytes))) {
        relevant=false;
        for(UINT i=0;i<moveBytes/sizeof(DXGI_OUTDUPL_MOVE_RECT);++i)
          if(touches(moves[i].DestinationRect)) { relevant=true; break; }
        std::vector<RECT> dirty((bytes+sizeof(RECT)-1)/sizeof(RECT));
        UINT dirtyBytes=bytes;
        if(FAILED(duplication->GetFrameDirtyRects(bytes,dirty.data(),&dirtyBytes))) relevant=true;
        else {
          if(!moveBytes && !dirtyBytes) relevant=true; // Empty metadata is not proof of no change.
          for(UINT i=0;i<dirtyBytes/sizeof(RECT) && !relevant;++i)
            relevant=touches(dirty[i]);
        }
      }
      if(!relevant) { duplication->ReleaseFrame(); return last; }
    }
    const auto readbackStarted=monotonicNs();
    ComPtr<ID3D11Texture2D> texture; hr=resource.As(&texture);
    if(SUCCEEDED(hr)) {
      const auto offset=region.topLeft()-origin;
      const D3D11_BOX box{UINT(offset.x()),UINT(offset.y()),0,UINT(offset.x()+region.width()),UINT(offset.y()+region.height()),1};
      context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture.Get(),0,&box);
      D3D11_MAPPED_SUBRESOURCE mapped{};
      hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
      if(SUCCEEDED(hr)) {
        QImage image(region.size(),QImage::Format_RGB32);
        if(!image.isNull()) for(int y=0;y<image.height();++y) {
          auto target=reinterpret_cast<QRgb *>(image.scanLine(y));
          const auto source=reinterpret_cast<const QRgb *>(static_cast<const uchar *>(mapped.pData)+size_t(y)*mapped.RowPitch);
          for(int x=0;x<image.width();++x) target[x]=source[x]|0xff000000;
        }
        context->Unmap(staging.Get(),0); last=std::move(image);
      }
    }
    duplication->ReleaseFrame();
    readbackNs=monotonicNs()-readbackStarted;
    if(FAILED(hr)) { failure="DXGI texture read failed"; duplication.Reset(); return {}; }
    return last;
  }
private:
  QRect region; QPoint origin; QString failure; QImage last;
  bool updated=false;
  qint64 readbackNs=0,presentationNs=0;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIOutputDuplication> duplication;
  ComPtr<ID3D11Texture2D> staging;
};
#endif
}
ScrollFramePump::ScrollFramePump(QRect area,std::function<QImage()> value)
    :region(area),provider(std::move(value)) {}
ScrollFramePump::~ScrollFramePump() { stop(); }
QString ScrollFramePump::backendName() const { std::lock_guard lock(mutex); return backend; }
QString ScrollFramePump::captureError() const { std::lock_guard lock(mutex); return error; }
void ScrollFramePump::setExternalBackend(const QString &name) { std::lock_guard lock(mutex); backend=name; }
void ScrollFramePump::requestStop() {
  { std::lock_guard lock(mutex); stopping=true; }
  wake.notify_all();
}
void ScrollFramePump::stop() { requestStop(); wait(); }
QImage ScrollFramePump::take() {
  std::lock_guard lock(mutex);
  if(frames.empty()) return latest;
  auto image=std::move(frames.front().sample.image); frames.pop_front(); return image;
}
ScrollFramePump::Sample ScrollFramePump::waitNext() {
  std::unique_lock lock(mutex);
  wake.wait_for(lock,std::chrono::milliseconds(20),[&] { return stopping || !frames.empty(); });
  if(frames.empty()) return {};
  auto sample=std::move(frames.front().sample); frames.pop_front(); return sample;
}
void ScrollFramePump::noteDirection(int value) {
  direction=value; directionAt=monotonicNs();
}
void ScrollFramePump::enqueue(const QImage &frame,qint64 presentedAtNs) {
  if(frame.isNull()) return;
  const auto now=monotonicNs();
  std::lock_guard lock(mutex);
  if(stopping) return;
  const size_t capacity=size_t(std::clamp<qint64>(24*1024*1024/qMax<qint64>(1,frame.sizeInBytes()),1,8));
  if(latest!=frame && (frames.size()>=capacity ||
      (!frames.empty() && now-frames.front().sample.capturedAtNs>250000000))) {
    // A frame may be the only bridge across a scroll. Keep the entire queued
    // interval and pause acquisition instead of silently breaking continuity.
    error=QStringLiteral("采集处理积压，已暂停扩展；可完成并保留已确认内容");
    stopping=true; ++dropped; wake.notify_all(); return;
  }
  if(!captured) firstCapture=now;
  else maximumCaptureInterval=qMax(maximumCaptureInterval.load(),now-lastCapture.load());
  lastCapture=now; ++captured;
  if(latest==frame) return;
  ++changed;
  const int hint=now-directionAt.load()<600000000?direction.load():0;
  const auto eventNs=presentedAtNs>0 && presentedAtNs<=now && now-presentedAtNs<5000000000LL?presentedAtNs:now;
  frames.push_back({quint64(captured.load()),{frame,hint,eventNs}}); latest=frame;
  queuedBytesMaximum=qMax(queuedBytesMaximum.load(),qint64(frames.size())*frame.sizeInBytes());
  wake.notify_all();
}
void ScrollFramePump::run() {
#ifdef Q_OS_MACOS
  try {
    auto stream = provider ? nullptr : std::make_unique<MacScreenStream>(region, nativeSize, 30, false);
    setExternalBackend(stream ? "ScreenCaptureKit" : "test provider");
    const auto start = monotonicNs();
    for (;;) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
      { std::lock_guard lock(mutex); if (stopping) break; }
      auto frame = provider ? provider() : stream->frame();
      if (stream) {
        const auto failure = stream->failure();
        if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
        if (frame.isNull() && monotonicNs() - start > 5000000000LL)
          throw std::runtime_error("未能读取滚动区域的画面。");
      }
      enqueue(frame);
      std::unique_lock lock(mutex);
      if (wake.wait_until(lock, deadline, [&] { return stopping; })) break;
    }
  } catch (const std::exception &failure) {
    std::lock_guard lock(mutex);
    error = QString::fromUtf8(failure.what());
  }
#else
  std::unique_ptr<RegionGrabber> grabber;
  std::unique_ptr<DuplicationGrabber> duplication;
  if(!provider) duplication=std::make_unique<DuplicationGrabber>(region);
  if(provider) setExternalBackend("test provider");
  else if(duplication->ready()) setExternalBackend("DXGI Desktop Duplication");
  else {
    qWarning()<<"Scroll acquisition fallback:"<<duplication->error();
    setExternalBackend("GDI fallback ("+duplication->error()+")");
    duplication.reset(); grabber=std::make_unique<RegionGrabber>(region);
  }
  qInfo()<<"Scroll capture backend:"<<backendName();
  const auto acquisitionStarted=monotonicNs();
  for(;;) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(12);
    { std::lock_guard lock(mutex); if(stopping) break; }
    auto frame=provider?provider():duplication?duplication->grab():grabber->grab();
    if(duplication && duplication->desktopUpdated()) {
      ++desktopUpdates;
      maximumReadback=qMax(maximumReadback.load(),duplication->readbackDurationNs());
    }
    if(duplication && !duplication->hasFrame() && monotonicNs()-acquisitionStarted>1000000000) {
      qWarning()<<"Scroll acquisition fallback: DXGI supplied no initial desktop frame";
      setExternalBackend("GDI fallback (DXGI supplied no initial desktop frame)");
      duplication.reset(); grabber=std::make_unique<RegionGrabber>(region); frame=grabber->grab();
    }
    if(duplication && !duplication->ready()) {
      qWarning()<<"Scroll acquisition fallback:"<<duplication->error();
      setExternalBackend("GDI fallback ("+duplication->error()+")");
      duplication.reset(); grabber=std::make_unique<RegionGrabber>(region); frame=grabber->grab();
    }
    { std::lock_guard lock(mutex); error=frame.isNull() && !duplication?QStringLiteral("未能读取画面，请检查窗口或结束当前截图"):QString(); }
    enqueue(frame,duplication?duplication->presentedAtNs():0);
    std::unique_lock lock(mutex);
    if(wake.wait_until(lock,deadline,[&] { return stopping; })) break;
  }
#endif
}
ScrollMatchPump::ScrollMatchPump(ScrollFramePump *value,ScrollStitcher *stitcher):source(value) {
  engineName="legacy comparison";
  accept=[stitcher](const QImage &image,int direction) { return stitcher->accept(image,direction); };
  getPreview=[stitcher] { return stitcher->preview(); }; getRevision=[stitcher] { return stitcher->contentRevision(); };
  getImage=[stitcher] { return stitcher->image(); };
}
ScrollMatchPump::ScrollMatchPump(ScrollFramePump *value,ScrollStitcherV2 *stitcher):source(value) {
  engineName="V2 document-coordinate matcher";
  accept=[stitcher](const QImage &image,int direction) { return stitcher->accept(image,direction); };
  getPreview=[stitcher] { return stitcher->preview(); }; getRevision=[stitcher] { return stitcher->contentRevision(); };
  getImage=[stitcher] { return stitcher->image(); };
  getViewport=[stitcher] { return stitcher->viewportPosition(); };
  finishCache=[stitcher] { stitcher->finishPendingWrites(); };
}
ScrollMatchPump::~ScrollMatchPump() { stop(); }
void ScrollMatchPump::requestStop(bool drain,bool prepareImage) {
  assembling=prepareImage; draining=drain; stopping=true;
}
void ScrollMatchPump::stop(bool drain) {
  requestStop(drain); wait();
}
std::optional<ScrollMatchPump::Update> ScrollMatchPump::takeUpdate(quint64 after) {
  std::lock_guard lock(mutex);
  if(update.serial<=after) return {};
  return update;
}
QImage ScrollMatchPump::finalImage() const {
  std::lock_guard lock(mutex);
  return finishedImage;
}
void ScrollMatchPump::run() {
  QImage previous;
  qint64 processed=0,totalNs=0,maximumNs=0,maximumAge=0,totalAge=0;
  qint64 lastPublished=0,previewTotal=0,previewMaximum=0;
  ScrollStitcher::Preview snapshot;
  std::optional<Update> pending;
  const auto publish=[&] {
    if(!pending) return;
    const auto before=monotonicNs();
    if(snapshot.revision!=getRevision() ||
       (pending->result==ScrollStitcher::Revisited && !getViewport)) snapshot=getPreview();
    else if(getViewport) snapshot.viewportY=getViewport();
    pending->preview=snapshot;
    pending->publishedAtNs=monotonicNs();
    previewTotal+=pending->publishedAtNs-before;
    previewMaximum=qMax(previewMaximum,pending->publishedAtNs-before);
    lastPublished=pending->publishedAtNs;
    {
      std::lock_guard lock(mutex);
      pending->serial=update.serial+1;
      update=std::move(*pending);
    }
    pending.reset();
  };
  int failures=0;
  bool terminal=false;
  const auto started=monotonicNs();
  for(;;) {
    if(stopping && !draining) break;
    const auto sample=source->waitNext();
    if(sample.image.isNull()) {
      if(stopping) break;
      if(pending && monotonicNs()-lastPublished>=33000000) publish();
      continue;
    }
    if(terminal || sample.image==previous) continue;
    previous=sample.image;
    const auto before=monotonicNs();
    const auto age=before-sample.capturedAtNs;
    maximumAge=qMax(maximumAge,age); totalAge+=age;
    const auto result=accept(sample.image,sample.direction);
    const auto elapsed=monotonicNs()-before;
    ++processed; totalNs+=elapsed; maximumNs=qMax(maximumNs,elapsed);
    if(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch) ++failures;
    pending=Update{result,{},0,sample.capturedAtNs,before+elapsed,0};
    if(!lastPublished || monotonicNs()-lastPublished>=33000000 ||
       result==ScrollStitcher::CacheError || result==ScrollStitcher::LimitReached) publish();
    if(result==ScrollStitcher::CacheError || result==ScrollStitcher::LimitReached) {
      terminal=true; source->requestStop();
    }
  }
  publish();
  if(assembling) {
    auto image=getImage();
    std::lock_guard lock(mutex);
    finishedImage=std::move(image);
  }
  if(finishCache) finishCache();
  qInfo()<<"Scroll session: elapsed_ms"<<(monotonicNs()-started)/1000000
         <<"engine"<<engineName<<"backend"<<source->backendName()
         <<"sample_polls"<<source->frameCount()<<"sample_poll_hz"<<source->captureRate()<<"changed_frames"<<source->changedFrameCount()
         <<"desktop_updates"<<source->desktopUpdateCount()<<"readback_max_ms"<<source->maximumReadbackMs()
         <<"capture_interval_max_ms"<<source->maximumCaptureIntervalMs()
         <<"unprocessed_overflow"<<source->droppedCount()<<"matched"<<processed
         <<"queue_peak_mib"<<source->maximumQueuedBytes()/1048576.
         <<"alignment_failures"<<failures<<"match_mean_ms"<<(processed?totalNs/processed/1000000.:0.)
         <<"match_max_ms"<<maximumNs/1000000.<<"queue_age_mean_ms"<<(processed?totalAge/processed/1000000.:0.)
         <<"queue_age_max_ms"<<maximumAge/1000000.<<"preview_build_total_ms"<<previewTotal/1000000.
         <<"preview_build_max_ms"<<previewMaximum/1000000.;
}
