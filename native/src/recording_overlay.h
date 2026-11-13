#pragma once
#include "recorder.h"
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
#include <memory>
#include <functional>

class RecordingFrame;
class RecordingControls;

class RecordingOverlay : public QObject {
  Q_OBJECT
public:
  RecordingOverlay(Recorder *recorder, QRect logicalRegion, bool systemAudio,
                   bool microphone, QObject *parent = nullptr,
                   std::function<bool(QWidget *)> exclusion = {});
  ~RecordingOverlay() override;
  bool show();
  void hide();
  void refresh();
  void setCountdown(int seconds);
  void setPreparing();
  void setArtwork(const QImage &image);
  QWidget *artwork() const { return markLayer.get(); }
  QWidget *boundary() const;
  QWidget *controls() const;
  QRect region() const { return logicalRegion; }
  bool captureExcluded() const { return excluded; }
  bool compatibilityMode() const { return compatibility; }
  void configureLive(std::shared_ptr<RecordingLiveState> live, quintptr window = 0);
  void setRegion(QRect region);
  void setEditing(bool editing);
signals:
  void countdownCanceled();
  void editingRequested(bool enabled);
  void regionChanged(QRect region);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QPointer<Recorder> recorder;
  QRect logicalRegion;
  std::unique_ptr<RecordingFrame> frame;
  std::unique_ptr<RecordingControls> panel;
  std::unique_ptr<QWidget> markLayer;
  QTimer timer;
  bool begun = false, excluded = false;
  bool compatibility = false, outsideControls = false, shown = false;
  bool artworkExcluded = false;
  std::function<bool(QWidget *)> exclusion;
  int countdown = -1;
  bool preparing = false;
  void showArtwork();
  std::shared_ptr<RecordingLiveState> live;
  quintptr targetWindow = 0;
  bool editing = false, movingRegion = false;
  QPoint moveOrigin;
  QRect originalRegion;
};
