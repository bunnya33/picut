#pragma once
#include "recognition.h"
#include <QtCore/QElapsedTimer>
#include <QtGui/QPolygonF>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolButton>
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

// Draws selection on the original image; selected text never enters the bitmap.
class ImageTextOverlay : public QWidget {
  Q_OBJECT
public:
  ImageTextOverlay(QWidget *parent, Store *store,
                   std::function<QRect()> imageRect, bool floatingBar);
  void begin(const QImage &image, bool recognize = true);
  void finish();
  void setResult(const QJsonObject &result);
  void selectAll();
  QString selectedText() const;
  const ImageTextPage &page() const { return document; }
  const QImage &sourceImage() const { return source; }
  QFrame *controlBar() const { return bar; }
  int recognitionStarts() const { return starts; }
signals:
  void closed();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
private:
  Store *store;
  std::function<QRect()> imageRect;
  PaddleOcrJob job;
  ImageTextPage document;
  QImage source;
  QString profile, acceleration;
  QFrame *bar;
  QLabel *status;
  QToolButton *copyButton, *allButton, *fullButton;
  QElapsedTimer elapsed;
  QTimer timer;
  QString phase;
  bool ready = false, dragging = false, active = false;
  int anchor = -1, caret = -1, starts = 0;
  void recognize();
  void place();
  QPointF toImage(QPointF point) const;
  void selectionChanged();
  void copySelection();
  void showFullText();
};
