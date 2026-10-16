#pragma once
#include "annotation.h"
#include "toolbar.h"
#include "text_editor.h"
#include <memory>

class EditingSurface : public QWidget {
  Q_OBJECT
public:
  explicit EditingSurface(Store *store, QWidget *parent = nullptr,
                          Qt::WindowFlags flags = {});
  std::unique_ptr<Annotation> annotation;
  Toolbar *toolbar = nullptr;
  Store *store;
  bool editing = false;
  virtual QRect contentRect() const = 0;
  virtual bool complete(const QString &action) = 0;
  QPointF imagePoint(QPoint point) const;
  void chooseTool(QString tool);
  void setPreference(QString key, QVariant value);
  void showToolbar();
  void positionToolbar();
  void paintEditing(QPainter &painter);
  bool annotationPress(QMouseEvent *event);
  bool annotationMove(QMouseEvent *event);
  bool annotationRelease(QMouseEvent *event);
  bool annotationKey(QKeyEvent *event);
  void annotationWheel(QWheelEvent *event);
  void commitText();
  void showMore();
  QString editTarget(QPoint point) const;

protected:
  void positionBar(QWidget *bar);
  void updateDrawingCursor(QPoint point);
  QPoint pointer;
  QString dragHandle;
  std::optional<Mark> dragOriginal;
  QPointF dragOrigin;
  bool suppressDraw = false;
  InlineTextEditor *textEntry = nullptr;
};
