#pragma once
#include <QtCore/QThread>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include "scroll_capture.h"

// Screen acquisition continues while a separate worker validates a seam. The
// bounded queue retains intermediate frames rather than just the latest frame.
class ScrollFramePump : public QThread {
public:
  explicit ScrollFramePump(QRect physicalRegion, std::function<QImage()> provider = {});
  ~ScrollFramePump() override;
  QImage take();
  struct Sample { QImage image; int direction=0; qint64 capturedAtNs=0; };
  Sample waitNext();
  void enqueue(const QImage &image);
  void noteDirection(int value);
  void stop();
  int frameCount() const { return captured.load(); }
  int droppedCount() const { return dropped.load(); }
  int changedFrameCount() const { return changed.load(); }
  QString backendName() const;
  QString captureError() const;
  void setExternalBackend(const QString &name);
  double captureRate() const { const auto span=lastCapture.load()-firstCapture.load(); return span>0?(captured.load()-1)*1e9/span:0.; }
  double maximumCaptureIntervalMs() const { return maximumCaptureInterval.load()/1000000.; }
protected:
  void run() override;
private:
  QRect region;
  std::function<QImage()> provider;
  mutable std::mutex mutex;
  std::condition_variable wake;
  struct Frame { quint64 sequence; Sample sample; };
  std::deque<Frame> frames;
  QImage latest;
  std::atomic<int> captured{0}, dropped{0};
  std::atomic<int> changed{0};
  std::atomic<int> direction{0};
  std::atomic<qint64> directionAt{0};
  std::atomic<qint64> firstCapture{0},lastCapture{0},maximumCaptureInterval{0};
  bool stopping=false;
  QString backend="not started",error;
};

// The stitcher is owned by the session and accessed exclusively by this worker
// until stop() joins it. UI painting uses immutable thumbnail snapshots only.
class ScrollMatchPump : public QThread {
public:
  struct Update {
    ScrollStitcher::Result result=ScrollStitcher::Unchanged;
    ScrollStitcher::Preview preview;
    quint64 serial=0;
  };
  ScrollMatchPump(ScrollFramePump *source,ScrollStitcher *target);
  ScrollMatchPump(ScrollFramePump *source,ScrollStitcherV2 *target);
  ~ScrollMatchPump() override;
  void stop(bool drain=false);
  std::optional<Update> takeUpdate(quint64 after);
protected:
  void run() override;
private:
  ScrollFramePump *source;
  std::function<ScrollStitcher::Result(const QImage &,int)> accept;
  std::function<ScrollStitcher::Preview()> getPreview;
  std::function<quint64()> getRevision;
  QString engineName;
  std::mutex mutex;
  Update update;
  std::atomic<bool> stopping{false},draining{false};
};
