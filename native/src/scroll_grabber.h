#pragma once
#include <QtCore/QThread>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

// Screen acquisition continues even while the GUI is validating a seam. The
// bounded queue retains intermediate frames rather than just the latest frame.
class ScrollFramePump : public QThread {
public:
  explicit ScrollFramePump(QRect physicalRegion, std::function<QImage()> provider = {});
  ~ScrollFramePump() override;
  QImage take();
  void stop();
  int frameCount() const { return captured.load(); }
  int droppedCount() const { return dropped.load(); }
protected:
  void run() override;
private:
  QRect region;
  std::function<QImage()> provider;
  std::mutex mutex;
  std::condition_variable wake;
  struct Frame { quint64 sequence; QImage image; };
  std::deque<Frame> frames;
  QImage latest;
  std::atomic<int> captured{0}, dropped{0};
  bool stopping=false;
};
