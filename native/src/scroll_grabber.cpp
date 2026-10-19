#include "scroll_grabber.h"
#include <Windows.h>
#include <chrono>
#include <memory>
#include <algorithm>

namespace {
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
  auto image=std::move(frames.front().image); frames.pop_front(); return image;
}
void ScrollFramePump::run() {
  std::unique_ptr<RegionGrabber> grabber;
  if(!provider) grabber=std::make_unique<RegionGrabber>(region);
  for(;;) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(12);
    { std::lock_guard lock(mutex); if(stopping) break; }
    auto frame=provider?provider():grabber->grab();
    {
      std::lock_guard lock(mutex);
      if(!frame.isNull()) {
        ++captured;
        const size_t capacity=size_t(std::clamp<qint64>(32*1024*1024/qMax<qint64>(1,frame.sizeInBytes()),2,8));
        if(latest!=frame) {
          frames.push_back({quint64(captured.load()),frame}); latest=std::move(frame);
          // Remove the most densely sampled interior frame, preserving both
          // ends and spreading samples over the pending interval. Repeatedly
          // deleting the middle would leave a growing, unbridgeable gap.
          if(frames.size()>capacity) {
            size_t remove=1;
            for(size_t n=2;n+1<frames.size();++n)
              if(frames[n+1].sequence-frames[n-1].sequence <
                 frames[remove+1].sequence-frames[remove-1].sequence) remove=n;
            frames.erase(frames.begin()+remove); ++dropped;
          }
        }
      }
    }
    std::unique_lock lock(mutex);
    if(wake.wait_until(lock,deadline,[&] { return stopping; })) break;
  }
}
