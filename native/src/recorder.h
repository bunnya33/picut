#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>

struct AudioDevice {
  QString id, name;
};
QList<AudioDevice> audioDevices(bool capture);
using RecordingPainter = std::function<void(QImage &)>;
class RecordingLiveState {
public:
  struct Snapshot { QRect region; RecordingPainter painter; QRect controls; };
  explicit RecordingLiveState(QRect region, RecordingPainter painter = {}):value{region,std::move(painter)} {}
  Snapshot snapshot() const { std::lock_guard lock(mutex); return value; }
  void setRegion(QRect region) { std::lock_guard lock(mutex); value.region=region; }
  void setPainter(RecordingPainter painter) { std::lock_guard lock(mutex); value.painter=std::move(painter); }
  void setControls(QRect controls) { std::lock_guard lock(mutex); value.controls=controls; }
private:
  mutable std::mutex mutex;
  Snapshot value;
};
struct RecordOptions {
  QRect region;
  QString output, speakerId, micId;
  int fps = 60, quality = 14;
  bool cursor = true, systemAudio = false, microphone = false, clickFeedback = false;
  RecordingPainter painter;
  std::shared_ptr<RecordingLiveState> live;
  quintptr window = 0;
};
class ActiveClock {
public:
  void start();
  void pause();
  void resume();
  double elapsed() const;
  void finish();

private:
  mutable std::mutex mutex;
  std::chrono::steady_clock::time_point begun;
  double elapsedAtPause = 0, accumulated = 0;
  bool active = false, paused = false;
};
class Recorder : public QObject {
  Q_OBJECT
public:
  explicit Recorder(QString root, QObject *parent = nullptr);
  ~Recorder() override;
  bool active() const { return running.load(); }
  bool paused() const { return pauseFlag.load(); }
  bool finalizing() const { return stopFlag.load() && running.load(); }
  double elapsed() const { return clock.elapsed(); }
  void start(RecordOptions options, std::function<QImage()> source = {});
  void pause();
  void setTargetUnavailable(bool unavailable);
  void stop();
  void setMuted(bool system, bool value);
  static QString ffmpegPath();
signals:
  void started();
  void status(QString text);
  void warning(QString text);
  void finished(QJsonObject result);
  void failed(QString message);

private:
  QString root;
  std::thread worker;
  ActiveClock clock;
  std::atomic_bool running = false, stopFlag = false, pauseFlag = false,
                   muteSystem = false, muteMic = false, begunFlag = false;
  bool userPaused = false, targetUnavailable = false;
  void syncPause();
  void run(const RecordOptions &options, const std::function<QImage()> &source);
};
