#pragma once
#include "recognition.h"
#include <QtGui/QPolygonF>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTextEdit>
#include <functional>

class Store;
struct ImageTextGlyph {
  QString text;
  QPolygonF quad;
  int offset = 0;
};
struct ImageTextLine {
  QString text;
  QPolygonF quad;
  QList<ImageTextGlyph> glyphs;
  int offset = 0, row = 0;
};
class ImageTextPage {
public:
  QString text;
  QList<ImageTextLine> lines;
  void load(const QJsonObject &result);
  int caretAt(QPointF point, bool nearest = false) const;
  QList<QPolygonF> highlights(int first, int last) const;
};

struct ImageTextLayout {
  static constexpr int TitleHeight = 30;
  static constexpr int GripHeight = 12;
  enum Placement { Right, Left, Split } placement = Split;
  QRect window, title, frame, viewport, panel, divider;
  static ImageTextLayout arrange(QRect image, QRect available, int panelWidth = 360,
                                 bool collapsed = false, bool pinFrame = false);
};

// Draws selection on the original image; selected text never enters the bitmap.
class ImageTextOverlay : public QWidget {
  Q_OBJECT
public:
  ImageTextOverlay(QWidget *parent, Store *store,
                   std::function<QRect()> imageRect, bool pinFrame = false);
  void begin(const QImage &image, bool recognize = true);
  void finish();
  void setResult(const QJsonObject &result);
  void selectAll();
  QString selectedText() const;
  const ImageTextPage &page() const { return document; }
  const QImage &sourceImage() const { return source; }
  QFrame *controlBar() const { return bar; }
  QTextEdit *resultEditor() const { return editor; }
  QRect imageViewport() const { return layout.viewport; }
  QPoint imageGlobalOrigin() const { return mapToGlobal(layout.viewport.topLeft()); }
  QRectF previewRect() const;
  QPointF imageToView(QPointF point) const;
  ImageTextLayout::Placement placement() const { return layout.placement; }
  void setPanelCollapsed(bool collapsed);
  void fitImage();
  int recognitionStarts() const { return starts; }
  QJsonObject translationSelection() const;
signals:
  void closed();
  void translationRequested(QImage image, QJsonObject ocr);
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
private:
  Store *store;
  std::function<QRect()> imageRect;
  const bool pinFrame;
  PaddleOcrJob job;
  ImageTextPage document;
  QImage source;
  QString profile, acceleration;
  QFrame *bar;
  QTextEdit *editor;
  QWidget *divider;
  bool ready = false, dragging = false, active = false;
  bool syncing = false, collapsed = false, panning = false, space = false;
  bool resizingPanel = false;
  bool movingWindow = false;
  QPoint windowOffset, windowDragOffset;
  QPointF pan, lastPointer;
  int panelWidth = 360, resizeWidth = 360;
  QPoint resizeOrigin;
  double zoom = 1;
  ImageTextLayout layout;
  int anchor = -1, caret = -1, starts = 0;
  void recognize();
  void updateEditor();
  void place(bool resetView = true);
  QRect sourceGlobalRect() const;
  QPointF toImage(QPointF point) const;
  void selectionChanged(bool fromEditor = false);
  void editorSelectionChanged();
  void ensureImageSelectionVisible();
  void clampPan();
  void zoomAt(double factor, QPointF point);
  void moveWindow(QPoint global);
  bool dragArea(QPoint point) const;
  void copySelection();
  void copyAll();
};
