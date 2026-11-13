#pragma once
#include "editing.h"
#include "recorder.h"
#include <QtCore/QTimer>

class RecordingEditor : public EditingSurface {
  Q_OBJECT
public:
  RecordingEditor(Store *store, QRect region, QSize canvas,
                  const Annotation *initial, std::shared_ptr<RecordingLiveState> live,
                  bool deferToolbar = false);
  QRect contentRect() const override { return region.translated(-pos()); }
  bool complete(const QString &action) override;
  void setRegion(QRect region);
  void begin();
  void finish();
  void publish();
signals:
  void editingFinished();
  void artworkChanged(QImage image);
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
private:
  QRect region;
  std::shared_ptr<RecordingLiveState> live;
  QByteArray lastDocument;
  QImage preview;
  QTimer publishTimer;
  int markAt(QPoint point) const;
  bool toolbarConfigured = false;
  void configureToolbar();
};
