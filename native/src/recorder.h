#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

struct AudioDevice {
  QString id, name;
};
QList<AudioDevice> audioDevices(bool capture);
using RecordingPainter = std::function<void(QImage &)>;
struct RecordOptions {
  QRect region;
  QString output, speakerId, micId;
  int fps = 30, quality = 22;
  bool cursor = true, systemAudio = false, microphone = false;
  RecordingPainter painter;
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
  void run(const RecordOptions &options, const std::function<QImage()> &source);
};
