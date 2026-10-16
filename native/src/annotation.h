#pragma once
#include "store.h"
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QTextDocument>
#include <QtGui/QTextCharFormat>
#include <optional>

struct Mark {
  QString kind = "arrow";
  QList<QPointF> points;
  QColor color{255, 0, 4};
  double width = 4, fontSize = 22;
  QString text, dash = "solid", arrowHead = "filled", arrowMode = "forward",
                areaShape = "stroke";
  QString fontFamily = "Microsoft YaHei UI";
  QColor textBackground = Qt::transparent, textOutline = Qt::transparent;
  int rotation = 0, radius = 0, intensity = 12, number = 1,
      magnification = 2, outlineWidth = 0;
  bool bold = false, italic = false, filled = false;
  QJsonObject json() const;
  static Mark fromJson(const QJsonObject &value);
};
QFont annotationFont(const Mark &mark);
QTextCharFormat annotationTextFormat(const Mark &mark);
void styleTextDocument(QTextDocument *document, const Mark &mark);
QSizeF textSize(const Mark &mark);
QRectF numberLabelRect(const Mark &mark);
QPainterPath arrowPath(const Mark &mark);
QPainterPath markPath(const Mark &mark);
QRectF markBounds(const Mark &mark);
QTransform markTransform(const Mark &mark);
bool isEndpointMark(const Mark &mark);
bool isBoxMark(const Mark &mark);
QMap<QString, QPointF> markControls(const Mark &mark);
bool paintedHit(const Mark &mark, QPointF point, double tolerance = 3);
void paintMark(QImage &image, const Mark &mark);

class Annotation {
public:
  QImage original, cached;
  QList<Mark> marks;
  QList<QList<Mark>> undoStates, redoStates;
  std::optional<Mark> draft;
  Mark preferences;
  QJsonObject toolOptions;
  QString tool = "select";
  int selected = -1;
  explicit Annotation(const QImage &image = {}, Store *store = nullptr);
  void chooseTool(const QString &value);
  void savePreferences(Store *store);
  void checkpoint();
  void refresh();
  void undo();
  void redo();
  void add(Mark mark);
  void confirm();
  void adjustWidth(int steps);
  void applyPreferences();
  QImage render(bool includeDraft = true) const;
  void paintControls(QPainter &painter) const;
};
