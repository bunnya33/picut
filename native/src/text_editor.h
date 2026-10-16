#pragma once
#include "annotation.h"
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QToolButton>

class QGraphicsTextItem;
class InlineTextEditor : public QGraphicsView {
  Q_OBJECT
public:
  explicit InlineTextEditor(QWidget *parent);
  void begin(const Mark &mark, QRect displayRect, QSize imageSize);
  void setPreferences(const Mark &preferences);
  Mark value() const;
  QGraphicsTextItem *textItem() const { return item; }
signals:
  void commitRequested();
  void cancelRequested();
  void commandRequested(int key, Qt::KeyboardModifiers modifiers);
  void geometryEdited(double fontSize, int rotation);
  void sizeStepRequested(int steps);
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool focusNextPrevChild(bool next) override;
  void wheelEvent(QWheelEvent *event) override;
private:
  Mark model, dragModel;
  QGraphicsTextItem *item;
  QMap<QString, QToolButton *> handles;
  QString activeHandle;
  QPointF dragPoint, fixedPoint;
  bool styling = false;
  void applyModel();
  void positionHandles();
  QPointF scenePoint(QPoint global) const;
};
