#pragma once
#include "recorder.h"
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
#include <memory>

class RecordingFrame;
class RecordingControls;

class RecordingOverlay : public QObject {
  Q_OBJECT
public:
  RecordingOverlay(Recorder *recorder, QRect logicalRegion, bool systemAudio,
                   bool microphone, QObject *parent = nullptr);
  ~RecordingOverlay() override;
  bool show();
  void hide();
  void refresh();
  void setCountdown(int seconds);
  QWidget *boundary() const;
  QWidget *controls() const;
  QRect region() const { return logicalRegion; }
  bool captureExcluded() const { return excluded; }
signals:
  void countdownCanceled();

private:
  QPointer<Recorder> recorder;
  QRect logicalRegion;
  std::unique_ptr<RecordingFrame> frame;
  std::unique_ptr<RecordingControls> panel;
  QTimer timer;
  bool begun = false, excluded = false;
  int countdown = -1;
};
