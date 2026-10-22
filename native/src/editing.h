#pragma once
#include "annotation.h"
#include "toolbar.h"
#include "text_editor.h"
#include "image_text.h"
#include "image_translation.h"
#include <memory>

class EditingSurface : public QWidget {
  Q_OBJECT
public:
  explicit EditingSurface(Store *store, QWidget *parent = nullptr,
                          Qt::WindowFlags flags = {});
  ~EditingSurface() override;
  std::unique_ptr<Annotation> annotation;
  Toolbar *toolbar = nullptr;
  ImageTextOverlay *imageText = nullptr;
  ImageTranslationOverlay *imageTranslation = nullptr;
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
  void startImageText(const QImage &image = {}, bool recognize = true);
  void startImageTranslation(const QImage &image = {}, bool recognize = true);
  QString editTarget(QPoint point) const;

protected:
  bool event(QEvent *event) override;
  void ensureTextEditor();
  int numberLabelIndex = -1;
  void positionBar(QWidget *bar);
  void updateDrawingCursor(QPoint point);
  QPoint pointer;
  QString dragHandle;
  std::optional<Mark> dragOriginal;
  QPointF dragOrigin;
  bool suppressDraw = false;
  InlineTextEditor *textEntry = nullptr;
};
