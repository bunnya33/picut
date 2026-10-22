#include "scroll_grabber.h"
#include <Windows.h>
#include <chrono>
#include <memory>
#include <algorithm>
#include <QtCore/QDebug>

namespace {
qint64 monotonicNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// GDI objects are created, used and destroyed on the acquiring thread. A DIB is
// reused for the whole session; only changed frames need an owned pixel copy.
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
}
ScrollFramePump::ScrollFramePump(QRect area,std::function<QImage()> value)
    :region(area),provider(std::move(value)) {}
ScrollFramePump::~ScrollFramePump() { stop(); }
void ScrollFramePump::stop() {
  { std::lock_guard lock(mutex); stopping=true; }
  wake.notify_all(); wait();
}
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
void ScrollFramePump::enqueue(const QImage &frame) {
  if(frame.isNull()) return;
  const auto now=monotonicNs();
  std::lock_guard lock(mutex);
  if(stopping) return;
  if(!captured) firstCapture=now;
  else maximumCaptureInterval=qMax(maximumCaptureInterval.load(),now-lastCapture.load());
  lastCapture=now; ++captured;
  const size_t capacity=size_t(std::clamp<qint64>(32*1024*1024/qMax<qint64>(1,frame.sizeInBytes()),2,8));
  if(latest==frame) return;
  const int hint=now-directionAt.load()<600000000?direction.load():0;
  frames.push_back({quint64(captured.load()),{frame,hint,now}}); latest=frame;
  // Spread retained samples across the pending interval, keeping both ends.
  if(frames.size()>capacity) {
    size_t remove=1;
    for(size_t n=2;n+1<frames.size();++n)
      if(frames[n+1].sequence-frames[n-1].sequence <
         frames[remove+1].sequence-frames[remove-1].sequence) remove=n;
    frames.erase(frames.begin()+remove); ++dropped;
  }
  wake.notify_all();
}
void ScrollFramePump::run() {
  std::unique_ptr<RegionGrabber> grabber;
  if(!provider) grabber=std::make_unique<RegionGrabber>(region);
  for(;;) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(12);
    { std::lock_guard lock(mutex); if(stopping) break; }
    auto frame=provider?provider():grabber->grab();
    enqueue(frame);
    std::unique_lock lock(mutex);
    if(wake.wait_until(lock,deadline,[&] { return stopping; })) break;
  }
}
ScrollMatchPump::ScrollMatchPump(ScrollFramePump *value,ScrollStitcher *stitcher):source(value),target(stitcher) {}
ScrollMatchPump::~ScrollMatchPump() { stop(); }
void ScrollMatchPump::stop(bool drain) {
  draining=drain; stopping=true; wait();
}
std::optional<ScrollMatchPump::Update> ScrollMatchPump::takeUpdate(quint64 after) {
  std::lock_guard lock(mutex);
  if(update.serial<=after) return {};
  return update;
}
void ScrollMatchPump::run() {
  QImage previous;
  qint64 processed=0,totalNs=0,maximumNs=0,maximumAge=0;
  int failures=0;
  const auto started=monotonicNs();
  for(;;) {
    if(stopping && !draining) break;
    const auto sample=source->waitNext();
    if(sample.image.isNull()) { if(stopping) break; continue; }
    if(sample.image==previous) continue;
    previous=sample.image;
    const auto before=monotonicNs();
    maximumAge=qMax(maximumAge,before-sample.capturedAtNs);
    const auto result=target->accept(sample.image,sample.direction);
    const auto elapsed=monotonicNs()-before;
    ++processed; totalNs+=elapsed; maximumNs=qMax(maximumNs,elapsed);
    if(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch) ++failures;
    {
      std::lock_guard lock(mutex);
      update.result=result; ++update.serial;
      if(update.preview.revision!=target->contentRevision()) update.preview=target->preview();
    }
    if(result==ScrollStitcher::CacheError || result==ScrollStitcher::LimitReached) break;
  }
  qInfo()<<"Scroll session: elapsed_ms"<<(monotonicNs()-started)/1000000
         <<"captured"<<source->frameCount()<<"capture_fps"<<source->captureRate()
         <<"capture_interval_max_ms"<<source->maximumCaptureIntervalMs()
         <<"coalesced"<<source->droppedCount()<<"matched"<<processed
         <<"alignment_failures"<<failures<<"match_mean_ms"<<(processed?totalNs/processed/1000000.:0.)
         <<"match_max_ms"<<maximumNs/1000000.<<"queue_age_max_ms"<<maximumAge/1000000.;
}
