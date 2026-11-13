#pragma once
#include <QtCore/QPointF>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <windows.h>

// Kept off the desktop: feedback is composited once into captured video pixels.
// The input hook runs on its own message thread and never consumes mouse input.
class RecordingClicks {
public:
  using Clock = std::chrono::steady_clock;
  struct Target {
    QRect region, controls;
    bool enabled = false;
    Clock::time_point at = Clock::now();
  };
  static Clock::time_point mediaTime(double seconds) {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds)));
  }
  static constexpr int durationMs = 450;
  ~RecordingClicks() { stop(); }
  bool start(std::function<Target()> target) {
    stop();
    std::promise<bool> ready; auto result=ready.get_future();
    listener=std::thread([this,target=std::move(target),ready=std::move(ready)]() mutable {
      MSG message{}; PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);
      threadId=GetCurrentThreadId(); receiver=this; destination=std::move(target);
      const auto hook=SetWindowsHookExW(WH_MOUSE_LL,&RecordingClicks::mouseHook,GetModuleHandleW(nullptr),0);
      ready.set_value(hook!=nullptr);
      if (hook) {
        while (GetMessageW(&message,nullptr,0,0)>0) {}
        UnhookWindowsHookEx(hook);
      }
      destination={}; receiver=nullptr; threadId=0;
    });
    return result.get();
  }
  void stop() {
    if (const auto id=threadId.load()) PostThreadMessageW(id,WM_QUIT,0,0);
    if (listener.joinable()) listener.join();
  }
  void add(QPoint point,const Target &target,Clock::time_point now=Clock::now()) {
    if (!target.enabled || target.region.isEmpty() || !target.region.contains(point) || target.controls.contains(point)) return;
    const QPointF normalized(double(point.x()-target.region.x())/target.region.width(),
                             double(point.y()-target.region.y())/target.region.height());
    std::lock_guard lock(mutex);
    if (pulses.size()>=24) pulses.pop_front();
    pulses.push_back({normalized,now}); ++accepted;
  }
  void paint(QImage &image,Clock::time_point now=Clock::now()) {
    std::array<Pulse,24> visible;
    size_t count=0;
    {
      std::lock_guard lock(mutex);
      std::erase_if(pulses,[now](const Pulse &pulse) { return now-pulse.begun>=std::chrono::milliseconds(durationMs); });
      for (const auto &pulse:pulses) visible[count++]=pulse;
    }
    if (!count || image.isNull()) return;
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing);
    const double scale=std::max({1.,image.width()/1920.,image.height()/1080.});
    for (size_t i=0;i<count;++i) {
      const auto &pulse=visible[i];
      const double age=std::chrono::duration<double,std::milli>(now-pulse.begun).count();
      if (age<0) continue;
      const double progress=std::clamp(age/durationMs,0.,1.);
      const QPointF center(pulse.point.x()*image.width(),pulse.point.y()*image.height());
      painter.setOpacity((1.-progress)*(1.-progress));
      painter.setPen(QPen(QColor(15,114,230),2.*scale));
      painter.setBrush(QColor(58,164,255,72));
      const double radius=(10.+15.*progress)*scale;
      painter.drawEllipse(center,radius,radius);
      painter.setPen(QPen(QColor(15,114,230),1.*scale));
      painter.setBrush(QColor(230,245,255,230));
      painter.drawEllipse(center,4.*scale,4.*scale);
    }
  }
  quint64 count() const { return accepted.load(); }
private:
  struct Pulse { QPointF point; Clock::time_point begun; };
  std::mutex mutex;
  std::deque<Pulse> pulses;
  std::atomic<quint64> accepted=0;
  std::atomic<DWORD> threadId=0;
  std::thread listener;
  std::function<Target()> destination;
  inline static thread_local RecordingClicks *receiver=nullptr;
  static LRESULT CALLBACK mouseHook(int code,WPARAM action,LPARAM data) {
    if (code==HC_ACTION && receiver && (action==WM_LBUTTONDOWN || action==WM_RBUTTONDOWN || action==WM_MBUTTONDOWN)) {
      try {
        const auto event=reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
        const auto target=receiver->destination();
        receiver->add(QPoint(event->pt.x,event->pt.y),target,target.at);
      } catch (...) {} // A feedback failure must never interfere with input.
    }
    return CallNextHookEx(nullptr,code,action,data);
  }
};
