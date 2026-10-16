#pragma once
#include "editing.h"
#include "scroll_capture.h"
#include <QtCore/QPointer>
#include <functional>

struct ScreenFrame {
  QRect rect;
  QImage image;
};
QImage captureDesktopRegion(QRect global);
struct DesktopSnapshot {
  quintptr foreground = 0;
  QList<ScreenFrame> frames;
  QRect bounds;
  QList<QRect> windows;
  QImage cursorImage;
  QRectF cursorRect;
  QJsonObject timings;
  static DesktopSnapshot capture();
  QImage crop(QRect global, bool cursor = false) const;
  QColor color(QPoint global) const;
};
class CaptureOverlay : public EditingSurface {
  Q_OBJECT
public:
  explicit CaptureOverlay(Store *store, bool recording = false,
                          std::optional<DesktopSnapshot> snapshot = {});
  ~CaptureOverlay() override;
  DesktopSnapshot desktop;
  QRect selection;
  bool recording;
  QPointer<ScrollCapture> scrollCapture;
  std::function<bool(const QImage &, const QRect &, const QString &)> output;
  QRect contentRect() const override { return selection; }
  QRect globalSelection() const { return selection.translated(pos()); }
  bool complete(const QString &action) override;
  void selectRegion(QRect region);
signals:
  void finished();
  void recordRegion(QRect region);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void showEvent(QShowEvent *) override;

private:
  QPoint origin;
  QRect previous, hover, documentRect, spaceRect;
  QPoint spaceOrigin;
  QString selectionDrag;
  bool dragging = false, rgb = true, alt = false, space = false;
  bool includeCursor = false;
  quintptr sourceWindow = 0;
  int screenSelectCount = 0;
  void rebuildAnnotation();
  void updateSelectionToolbar(bool show = false);
  QString selectionHandle(QPoint point) const;
  void paintMagnifier(QPainter &painter, const QImage &preview);
  void updateRecordingSummary();
  bool confirmExit();
};
