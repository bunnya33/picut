#include "annotation.h"
#include "annotation_control_icons.h"
#include <QtCore/QJsonArray>
#include <QtGui/QFontMetricsF>
#include <QtGui/QPainterPathStroker>
#include <QtGui/QTextCursor>
#include <QtGui/QTextCharFormat>
#include <cmath>
#include <numbers>

QJsonObject Mark::json() const {
  QJsonArray data;
  for (const auto &point : points)
    data.append(QJsonArray{point.x(), point.y()});
  return {{"kind", kind},
          {"points", data},
          {"color", color.name(QColor::HexArgb)},
          {"width", width},
          {"text", text},
          {"font_size", fontSize},
          {"rotation", rotation},
          {"dash", dash},
          {"radius", radius},
          {"arrow_head", arrowHead},
          {"arrow_mode", arrowMode},
          {"area_shape", areaShape},
          {"filled", filled},
          {"straight", straight},
          {"font_family", fontFamily},
          {"bold", bold},
          {"italic", italic},
          {"intensity", intensity},
          {"number", number},
          {"magnification", magnification},
          {"outline_width", outlineWidth},
          {"text_background", textBackground.name(QColor::HexArgb)},
          {"text_outline", textOutline.name(QColor::HexArgb)}};
}
Mark Mark::fromJson(const QJsonObject &v) {
  Mark m;
  m.kind = v["kind"].toString("arrow");
  m.color = QColor(v["color"].toString("#ffff0004"));
  m.width = v["width"].toDouble(v["pen_width"].toDouble(4));
  for (const auto &item : v["points"].toArray()) {
    auto p = item.toArray();
    if (p.size() == 2)
      m.points.append({p[0].toDouble(), p[1].toDouble()});
  }
  m.text = v["text"].toString();
  m.fontSize = qBound(4.0 / 3, v["font_size"].toDouble(22), 1600.0 / 3);
  m.rotation = v["rotation"].toInt();
  m.radius = v["radius"].toInt();
  m.dash = v["dash"].toString("solid");
  m.arrowHead = v["arrow_head"].toString("filled");
  m.arrowMode = v["arrow_mode"].toString("forward");
  m.areaShape = v["area_shape"].toString("stroke");
  m.filled = v["filled"].toBool();
  m.straight = v["straight"].toBool();
  m.fontFamily = v["font_family"].toString("Microsoft YaHei UI");
  m.bold = v["bold"].toBool();
  m.italic = v["italic"].toBool();
  m.intensity = v["intensity"].toInt(12);
  m.number = v["number"].toInt(1);
  m.magnification = v["magnification"].toInt(2);
  m.outlineWidth = v["outline_width"].toInt();
  m.textBackground = QColor(v["text_background"].toString("#00000000"));
  m.textOutline = QColor(v["text_outline"].toString("#00000000"));
  return m;
}
QFont annotationFont(const Mark &mark) {
  QFont font(mark.fontFamily);
  font.setPointSizeF(mark.fontSize * .75);
  font.setBold(mark.bold);
  font.setItalic(mark.italic);
  return font;
}
QTextCharFormat annotationTextFormat(const Mark &mark) {
  QTextCharFormat format;
  format.setFont(annotationFont(mark));
  format.setForeground(mark.color);
  format.setTextOutline(mark.outlineWidth && mark.textOutline.alpha()
                            ? QPen(mark.textOutline, mark.outlineWidth,
                                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
                            : QPen(Qt::NoPen));
  return format;
}
void styleTextDocument(QTextDocument *document, const Mark &mark) {
  document->setDocumentMargin(0);
  document->setDefaultFont(annotationFont(mark));
  auto option = document->defaultTextOption();
  option.setWrapMode(QTextOption::NoWrap);
  document->setDefaultTextOption(option);
  QTextCursor cursor(document);
  cursor.select(QTextCursor::Document);
  cursor.setCharFormat(annotationTextFormat(mark));
}
QSizeF textSize(const Mark &mark) {
  QTextDocument document;
  document.setPlainText(mark.text);
  styleTextDocument(&document, mark);
  document.adjustSize();
  auto size = document.size();
  return {qMax(24.0, size.width()), qMax(30.0, size.height())};
}
QRectF numberLabelRect(const Mark &mark) {
  if (mark.points.isEmpty()) return {};
  const auto size=textSize(mark);
  const auto center=mark.points.first();
  return {center+QPointF(mark.fontSize*.65+qMax(4.,mark.fontSize*.18),-size.height()/2),size};
}
bool isEndpointMark(const Mark &mark) {
  return mark.kind == "line" || mark.kind == "arrow" ||
         (mark.kind == "pen" && mark.straight) ||
         (mark.kind == "marker" && mark.areaShape == "stroke");
}
bool isBoxMark(const Mark &mark) {
  return mark.kind == "rect" || mark.kind == "ellipse" ||
         (mark.kind == "marker" && mark.areaShape == "rect") ||
         ((mark.kind == "mosaic" || mark.kind == "blur" || mark.kind == "eraser") &&
          mark.areaShape != "stroke") || mark.kind == "magnify";
}
QTransform markTransform(const Mark &mark) {
  // A freehand stroke keeps the sampled positions. A marker line uses its
  // endpoints directly, just like an arrow, without a moving rotation pivot.
  if (mark.points.isEmpty() || mark.kind == "pen" ||
      (mark.kind == "marker" && mark.areaShape == "stroke"))
    return {};
  auto center =
      QRectF(mark.points.first(), mark.points.last()).normalized().center();
  QTransform t;
  if (mark.kind == "text") {
    t.translate(mark.points.first().x(), mark.points.first().y())
        .rotate(mark.rotation)
        .translate(-mark.points.first().x(), -mark.points.first().y());
    return t;
  }
  t.translate(center.x(), center.y())
      .rotate(mark.rotation)
      .translate(-center.x(), -center.y());
  return t;
}
QPainterPath arrowPath(const Mark &m) {
  QPainterPath p;
  if (m.points.size() < 2)
    return p;
  auto a = m.points.first(), b = m.points.last();
  if (m.arrowMode == "reverse")
    std::swap(a, b);
  auto d = b - a;
  double length = std::hypot(d.x(), d.y());
  if (length < .01) {
    p.addEllipse(a, m.width / 2, m.width / 2);
    return p;
  }
  bool tapered = m.arrowHead == "tapered" || m.arrowHead == "hollow";
  double width = qMin(m.width, length * .25);
  double head = qMin(qMax(12.0, width * (tapered ? 5 : 4.5)),
                     length * (m.arrowMode == "both" ? .4 : .65));
  double half = qMin(qMax(6.0, width * (tapered ? 2.5 : 2)), length * .32);
  double neckHalf = qMin(tapered ? width : width / 2, half * .65),
         neck = head * .82;
  double tail = tapered ? qMin(.5, width / 8) : width / 2;
  p.moveTo(0, tail);
  if (m.arrowMode == "both") {
    p = QPainterPath(QPointF());
    p.lineTo(head, half);
    p.lineTo(neck, neckHalf);
    if (tapered)
      p.lineTo(length / 2, tail);
  }
  p.lineTo(length - neck, neckHalf);
  p.lineTo(length - head, half);
  p.lineTo(length, 0);
  p.lineTo(length - head, -half);
  p.lineTo(length - neck, -neckHalf);
  if (m.arrowMode == "both") {
    if (tapered)
      p.lineTo(length / 2, -tail);
    p.lineTo(neck, -neckHalf);
    p.lineTo(head, -half);
    p.lineTo(0, 0);
  } else {
    p.lineTo(0, -tail);
    p.cubicTo(-tail * 1.34, -tail, -tail * 1.34, tail, 0, tail);
  }
  p.closeSubpath();
  if (m.dash != "solid" && m.arrowHead != "hollow") {
    QPainterPath mask;
    mask.setFillRule(Qt::WindingFill);
    double step = width * (m.dash == "dash" ? 6 : 3),
           painted = width * (m.dash == "dash" ? 4 : 1);
    for (double x = 0; x < length; x += qMax(1.0, step))
      mask.addRect({x, -half - 1, painted, half * 2 + 2});
    mask.addRect({length - head, -half - 1, head + 1, half * 2 + 2});
    if (m.arrowMode == "both")
      mask.addRect({-1, -half - 1, head + 1, half * 2 + 2});
    p = p.intersected(mask);
  }
  QTransform transform;
  transform.translate(a.x(), a.y())
      .rotate(std::atan2(d.y(), d.x()) * 180 / std::numbers::pi);
  return transform.map(p);
}
QPainterPath markPath(const Mark &mark) {
  QPainterPath path;
  if (mark.points.isEmpty())
    return path;
  QRectF r(mark.points.first(), mark.points.last());
  r = r.normalized();
  if (mark.kind == "arrow")
    return arrowPath(mark);
  if (mark.kind == "marker" && mark.areaShape == "stroke") {
    path.moveTo(mark.points.first());
    path.lineTo(mark.points.last());
    if (path.isEmpty())
      path.lineTo(mark.points.first() + QPointF(.01, 0));
    return path;
  }
  if (mark.kind == "rect")
    path.addRoundedRect(r, mark.radius, mark.radius);
  else if (mark.kind == "ellipse")
    path.addEllipse(r);
  else if (mark.kind == "text") {
    path.addRect(markBounds(mark));
  } else if (mark.kind == "number") {
    path.addEllipse(mark.points.first(), mark.fontSize * .65,
                    mark.fontSize * .65);
    if (!mark.text.trimmed().isEmpty()) path.addRect(numberLabelRect(mark));
  }
  else if (mark.kind == "magnify")
    path.addEllipse(r);
  else if (mark.areaShape != "stroke" &&
           (mark.kind == "marker" || mark.kind == "mosaic" ||
            mark.kind == "blur" || mark.kind == "eraser")) {
    if (mark.areaShape == "ellipse")
      path.addEllipse(r);
    else
      path.addRect(r);
  } else {
    path.moveTo(mark.points.first());
    for (int n = 1; n < mark.points.size(); ++n)
      path.lineTo(mark.points[n]);
    if (path.isEmpty())
      path.lineTo(mark.points.first() + QPointF(.01, 0));
  }
  return markTransform(mark).map(path);
}
QRectF markBounds(const Mark &mark) {
  if (mark.kind == "text" && !mark.points.isEmpty()) {
    return {mark.points.first(), textSize(mark)};
  }
  return markPath(mark).boundingRect();
}
QMap<QString, QPointF> markControls(const Mark &mark) {
  if (mark.points.isEmpty() || (mark.kind == "pen" && !mark.straight) ||
      mark.kind == "number")
    return {};
  if (isEndpointMark(mark))
    return {{"start", mark.points.first()}, {"end", mark.points.last()}};
  auto r = isBoxMark(mark)
               ? QRectF(mark.points.first(), mark.points.last()).normalized()
               : markBounds(mark);
  QMap<QString, QPointF> p{{"nw", r.topLeft()},
                           {"ne", r.topRight()},
                           {"sw", r.bottomLeft()},
                           {"se", r.bottomRight()},
                           {"rotate", {r.center().x(), r.top() - 20}}};
  if (isBoxMark(mark)) {
    p["n"] = {r.center().x(), r.top()};
    p["s"] = {r.center().x(), r.bottom()};
    p["w"] = {r.left(), r.center().y()};
    p["e"] = {r.right(), r.center().y()};
    auto transform = markTransform(mark);
    for (auto i = p.begin(); i != p.end(); ++i)
      i.value() = transform.map(i.value());
  }
  return p;
}
static Qt::PenStyle penStyle(const QString &dash) {
  return dash == "dash"  ? Qt::DashLine
         : dash == "dot" ? Qt::DotLine
                         : Qt::SolidLine;
}
bool paintedHit(const Mark &mark, QPointF point, double tolerance) {
  auto path = markPath(mark);
  if (mark.kind == "arrow" && mark.arrowHead != "hollow")
    return path.contains(point);
  if (mark.kind == "text" || mark.kind == "number" ||
      ((mark.kind == "rect" || mark.kind == "ellipse") && mark.filled) ||
      mark.areaShape != "stroke")
    return path.contains(point);
  QPainterPathStroker stroke;
  stroke.setWidth(mark.kind == "arrow" && mark.arrowHead == "hollow"
                      ? qMax(1.0, mark.width * .45)
                      : (mark.kind == "marker" ? mark.width * 3 : mark.width) +
                            tolerance * 2);
  stroke.setCapStyle(Qt::RoundCap);
  stroke.setJoinStyle(Qt::RoundJoin);
  if (mark.dash != "solid")
    stroke.setDashPattern(penStyle(mark.dash));
  return stroke.createStroke(path).contains(point);
}
static QImage blurred(QImage image, int radius) {
  image = image.convertToFormat(QImage::Format_ARGB32);
  if (image.isNull())
    return image;
  // Two separable box passes keep temporary storage bounded by the selected
  // area.
  for (int pass = 0; pass < 2; ++pass) {
    QImage destination(image.size(), image.format());
    bool horizontal = pass == 0;
    int rows = horizontal ? image.height() : image.width(),
        length = horizontal ? image.width() : image.height();
    for (int y = 0; y < rows; ++y) {
      qint64 sums[4]{};
      auto pixel = [&](int x) {
        return horizontal ? image.pixel(qBound(0, x, length - 1), y)
                          : image.pixel(y, qBound(0, x, length - 1));
      };
      auto accumulate = [&](QRgb p, int sign) {
        sums[0] += sign * qRed(p);
        sums[1] += sign * qGreen(p);
        sums[2] += sign * qBlue(p);
        sums[3] += sign * qAlpha(p);
      };
      for (int x = -radius; x <= radius; ++x)
        accumulate(pixel(x), 1);
      int count = radius * 2 + 1;
      for (int x = 0; x < length; ++x) {
        auto p = qRgba(int(sums[0] / count), int(sums[1] / count),
                       int(sums[2] / count), int(sums[3] / count));
        if (horizontal)
          destination.setPixel(x, y, p);
        else
          destination.setPixel(y, x, p);
        accumulate(pixel(x - radius), -1);
        accumulate(pixel(x + radius + 1), 1);
      }
    }
    image = destination;
  }
  return image;
}
void paintMark(QImage &image, const Mark &mark) {
  if (mark.points.isEmpty())
    return;
  auto path = markPath(mark);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(mark.color, mark.width, penStyle(mark.dash), Qt::RoundCap,
                      Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  if (mark.kind == "arrow") {
    if (mark.arrowHead == "hollow")
      painter.setPen(QPen(mark.color, qMax(1.0, mark.width * .45),
                          penStyle(mark.dash), Qt::RoundCap, Qt::RoundJoin));
    else {
      painter.setPen(Qt::NoPen);
      painter.setBrush(mark.color);
    }
    painter.drawPath(path);
  } else if (mark.kind == "marker") {
    auto color = mark.color;
    color.setAlpha(qMin(100, color.alpha()));
    painter.setPen(QPen(color, mark.width * 3, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    if (mark.areaShape != "stroke") {
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
    }
    painter.drawPath(path);
  } else if (mark.kind == "text") {
    painter.setWorldTransform(markTransform(mark), true);
    painter.translate(mark.points.first());
    if (mark.textBackground.alpha())
      painter.fillRect(QRectF(QPointF(), textSize(mark)),
                       mark.textBackground);
    QTextDocument document;
    document.setPlainText(mark.text);
    styleTextDocument(&document, mark);
    document.adjustSize();
    document.drawContents(&painter);
  } else if (mark.kind == "number") {
    const auto center=mark.points.first(); const double radius=mark.fontSize*.65;
    const QRectF circle(center-QPointF(radius,radius),QSizeF(radius*2,radius*2));
    painter.setPen(Qt::NoPen);
    painter.setBrush(mark.color);
    painter.drawEllipse(circle);
    painter.setFont(annotationFont(mark));
    painter.setPen(Qt::white);
    painter.drawText(circle, Qt::AlignCenter,
                     QString::number(mark.number));
    if (!mark.text.trimmed().isEmpty()) {
      auto label=numberLabelRect(mark);
      painter.setPen(QPen(mark.color,qMax(1.,mark.width*.5),Qt::SolidLine,Qt::RoundCap));
      painter.drawPolyline(QPolygonF{{center.x()+radius*.6,label.bottom()-1},
                                     {label.left(),label.bottom()-1},{label.right()+2,label.bottom()-1}});
      QTextDocument document; document.setPlainText(mark.text); styleTextDocument(&document,mark);
      document.adjustSize(); painter.translate(label.topLeft()); document.drawContents(&painter);
    }
  } else if (mark.kind == "mosaic" || mark.kind == "blur" ||
             mark.kind == "magnify") {
    QPainterPath mask = path;
    if (mark.areaShape == "stroke" && mark.kind != "magnify") {
      QPainterPathStroker s;
      s.setWidth(mark.width);
      s.setCapStyle(Qt::RoundCap);
      mask = s.createStroke(path);
    }
    auto area = mask.boundingRect().toAlignedRect().intersected(image.rect());
    if (area.isEmpty())
      return;
    painter.end();
    auto crop = image.copy(area);
    if (mark.kind == "mosaic")
      crop = crop.scaled(qMax(1, crop.width() / qMax(2, mark.intensity)),
                         qMax(1, crop.height() / qMax(2, mark.intensity)),
                         Qt::IgnoreAspectRatio, Qt::FastTransformation)
                 .scaled(area.size());
    else if (mark.kind == "blur")
      crop = blurred(crop, qBound(1, mark.intensity, 40));
    else {
      auto source =
          QRect(area.center() -
                    QPoint(area.width() / qMax(1, mark.magnification) / 2,
                           area.height() / qMax(1, mark.magnification) / 2),
                QSize(area.width() / qMax(1, mark.magnification),
                      area.height() / qMax(1, mark.magnification)));
      crop = image.copy(source).scaled(area.size(), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    }
    painter.begin(&image);
    painter.setClipPath(mask);
    painter.drawImage(area.topLeft(), crop);
  } else {
    if ((mark.kind == "rect" || mark.kind == "ellipse") && mark.filled)
      painter.setBrush(mark.color);
    painter.drawPath(path);
  }
}
Annotation::Annotation(const QImage &image, Store *store)
    : original(image.convertToFormat(QImage::Format_ARGB32)) {
  if (store) {
    toolOptions = store->config["annotation_options"].toObject();
    preferences.color =
        QColor(store->config["annotation_color"].toString("#ffff0004"));
  }
  refresh();
}
void Annotation::chooseTool(const QString &value) {
  if (tool != "select")
    toolOptions[tool] = preferences.json();
  const bool effect = value == "mosaic" || value == "blur";
  if (effect && selected >= 0 && selected < marks.size() &&
      (marks[selected].kind == "mosaic" || marks[selected].kind == "blur") &&
      marks[selected].areaShape != "stroke") {
    // Replace the effect on the editable region, keeping its geometry and
    // intensity. Render again from the source so repeated switches do not stack.
    if (marks[selected].kind != value) {
      checkpoint();
      marks[selected].kind = value;
      refresh();
    }
    preferences = marks[selected];
    tool = value;
    return;
  }
  auto color = preferences.color;
  preferences = Mark::fromJson(toolOptions[value].toObject());
  preferences.color = color;
  preferences.kind = value;
  if (effect) {
    if (preferences.areaShape != "ellipse")
      preferences.areaShape = "rect";
    preferences.intensity = qBound(2, preferences.intensity, 40);
  }
  if (value == "pen") {
    preferences.rotation = 0;
    preferences.areaShape = "stroke";
    preferences.straight = false;
  }
  if (value == "marker") {
    if (preferences.areaShape != "rect")
      preferences.areaShape = "stroke";
    if (preferences.areaShape == "stroke")
      preferences.rotation = 0;
  }
  tool = value;
  confirm();
}
void Annotation::savePreferences(Store *store) {
  if (!store)
    return;
  if (tool != "select")
    toolOptions[tool] = preferences.json();
  store->config["annotation_options"] = toolOptions;
  store->config["annotation_color"] = preferences.color.name(QColor::HexArgb);
  if (tool != "select")
    store->config["annotation_last_tool"] = tool;
  store->saveConfig();
}
void Annotation::checkpoint() {
  undoStates.append(marks);
  while (undoStates.size() > 100)
    undoStates.removeFirst();
  redoStates.clear();
}
void Annotation::refresh() {
  int nextNumber = 1;
  for (const auto &mark : marks)
    if (mark.kind == "number")
      nextNumber = qMax(nextNumber, mark.number + 1);
  if (tool == "number")
    preferences.number = nextNumber;
  auto numberOptions = toolOptions["number"].toObject();
  numberOptions["number"] = nextNumber;
  toolOptions["number"] = numberOptions;
  cached = render(false);
}
void Annotation::undo() {
  confirm();
  if (undoStates.isEmpty())
    return;
  redoStates.append(marks);
  marks = undoStates.takeLast();
  refresh();
}
void Annotation::redo() {
  confirm();
  if (redoStates.isEmpty())
    return;
  undoStates.append(marks);
  marks = redoStates.takeLast();
  refresh();
}
void Annotation::add(Mark mark) {
  checkpoint();
  marks.append(std::move(mark));
  selected = marks.last().kind == "pen" && !marks.last().straight
                 ? -1 : int(marks.size() - 1);
  draft.reset();
  refresh();
}
void Annotation::confirm() { selected = -1; }
void Annotation::adjustWidth(int steps) {
  if (tool == "pen" && draft && draft->kind == "pen")
    return;
  if (tool == "text" || tool == "number")
    preferences.fontSize = qBound(4.0 / 3, preferences.fontSize + steps * 4.0 / 3,
                                  1600.0 / 3);
  else if (tool == "mosaic" || tool == "blur") {
    const int intensity = qBound(2, preferences.intensity + steps, 40);
    if (intensity == preferences.intensity)
      return;
    preferences.intensity = intensity;
  }
  else
    preferences.width = qBound(1.0, preferences.width + steps, 100.0);
  applyPreferences();
}
void Annotation::applyPreferences() {
  if (selected >= 0 && selected < marks.size() && marks[selected].kind != "pen") {
    checkpoint();
    auto points = marks[selected].points;
    auto text = marks[selected].text;
    auto number = marks[selected].number;
    marks[selected] = preferences;
    marks[selected].points = points;
    marks[selected].text = text;
    marks[selected].number = number;
    refresh();
  }
  if (draft && draft->kind != "pen") {
    auto points = draft->points;
    *draft = preferences;
    draft->points = points;
  }
}
QImage Annotation::render(bool includeDraft) const {
  if (marks.isEmpty() && (!includeDraft || !draft))
    return original;
  QImage image = original.copy();
  auto paint = [&](const Mark &mark) {
    if (mark.kind == "eraser") {
      auto path = markPath(mark);
      if (mark.areaShape == "stroke") {
        QPainterPathStroker s;
        s.setWidth(mark.width);
        s.setCapStyle(Qt::RoundCap);
        path = s.createStroke(path);
      }
      QPainter p(&image);
      p.setClipPath(path);
      p.setCompositionMode(QPainter::CompositionMode_Source);
      p.drawImage(0, 0, original);
    } else
      paintMark(image, mark);
  };
  for (const auto &mark : marks)
    paint(mark);
  if (includeDraft && draft)
    paint(*draft);
  return image;
}
void Annotation::paintControls(QPainter &painter, bool manipulating) const {
  auto effectArea = [](const Mark &mark) {
    return (mark.kind == "mosaic" || mark.kind == "blur") && mark.areaShape != "stroke";
  };
  auto outline = [&](const Mark &mark) {
    const auto r = QRectF(mark.points.first(), mark.points.last()).normalized();
    painter.drawPolygon(markTransform(mark).map(
        QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}));
  };
  if (draft && effectArea(*draft)) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#999999"), 1));
    outline(*draft);
    return;
  }
  if (selected < 0 || selected >= marks.size())
    return;
  const auto &mark = marks[selected];
  auto controls = markControls(mark);
  if (controls.isEmpty())
    return;
  if (effectArea(mark)) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(manipulating ? "#999999" : "#555555"), 1,
                        manipulating ? Qt::SolidLine : Qt::DashLine));
    outline(mark);
    if (manipulating) return;
    painter.setPen(QPen(QColor("#666666"), 1));
    painter.setBrush(Qt::white);
    for (auto i = controls.begin(); i != controls.end(); ++i)
      if (i.key() != "rotate")
        painter.drawRect(QRectF(i.value() - QPointF(3, 3), QSizeF(6, 6)));
    paintRotationControl(painter,
        QRectF(controls.value("rotate") - QPointF(7, 7), QSizeF(14, 14)));
    return;
  }
  painter.setBrush(Qt::NoBrush);
  if (isEndpointMark(mark)) {
    for (auto point : controls) {
      painter.setPen(QPen(QColor("#888888"), 1));
      painter.drawRect(QRectF(point - QPointF(3, 3), QSizeF(6, 6)));
      painter.setPen(QPen(Qt::white, 1));
      painter.drawRect(QRectF(point - QPointF(2, 2), QSizeF(4, 4)));
    }
    return;
  }
  painter.setPen(QPen(QColor("#1688ef"), 1, Qt::DashLine));
  if (isBoxMark(mark)) {
    auto r = QRectF(mark.points.first(), mark.points.last()).normalized();
    painter.drawPolygon(markTransform(mark).map(
        QPolygonF{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}));
  } else
    painter.drawRect(markBounds(mark));
  QLineF stem(controls.value("n", markBounds(mark).topRight()), controls.value("rotate"));
  stem.setLength(qMax(0., stem.length() - 7));
  painter.drawLine(stem);
  painter.setPen(QPen(QColor("#1688ef"), 1));
  painter.setBrush(Qt::white);
  for (auto i = controls.begin(); i != controls.end(); ++i)
    if (i.key() != "rotate") painter.drawEllipse(i.value(), 4, 4);
  paintRotationControl(painter,
      QRectF(controls.value("rotate") - QPointF(7, 7), QSizeF(14, 14)));
}
