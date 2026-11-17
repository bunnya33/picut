#include "text_editor.h"
#include <QtCore/QSignalBlocker>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QTextCursor>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QFontMetricsF>
#include <QtGui/QHideEvent>
#include <QtGui/QTextBlock>
#include <QtGui/QTextLayout>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QStyleOptionGraphicsItem>
#include <cmath>
#include <numbers>

QCursor annotationRotationCursor() {
  static const QCursor cursor = [] {
    QPixmap bitmap(32, 32); bitmap.fill(Qt::transparent);
    QPainter painter(&bitmap); painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath arcs;
    QList<QPolygonF> heads;
    for (double start : {30., 210.}) {
      arcs.arcMoveTo(QRectF(8, 8, 16, 16), start);
      arcs.arcTo(QRectF(8, 8, 16, 16), start, 125);
      const auto angle = (start + 125) * std::numbers::pi / 180;
      const QPointF tip(16 + 8 * std::cos(angle), 16 - 8 * std::sin(angle));
      const QPointF tangent(-std::sin(angle), -std::cos(angle));
      const QPointF normal(tangent.y(), -tangent.x());
      heads.append(QPolygonF{tip, tip - tangent * 5 + normal * 3,
                            tip - tangent * 5 - normal * 3});
    }
    painter.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(arcs);
    painter.setPen(QPen(QColor("#333333"), 1.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(arcs);
    painter.setPen(QPen(Qt::white, 1)); painter.setBrush(QColor("#333333"));
    for (const auto &head : heads) painter.drawPolygon(head);
    return QCursor(bitmap, 16, 16);
  }();
  return cursor;
}

class InlineTextItem : public QGraphicsTextItem {
public:
  bool numberLabel=false;
  QColor lineColor;
  double lineWidth=1, leader=4;
  double framePadding = 6;
  bool frameVisible = true;
  void setFramePadding(double padding) {
    if (qFuzzyCompare(framePadding, padding)) return;
    prepareGeometryChange();
    framePadding = padding;
  }
  QRectF boundingRect() const override {
    const auto text = QGraphicsTextItem::boundingRect();
    if (numberLabel) return text.united({0, 0, 24, 30});
    return text.united({0, 0, 2, QFontMetricsF(font()).height()})
        .adjusted(-framePadding, -framePadding, framePadding, framePadding);
  }
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override {
    auto textOption = *option;
    textOption.state &= ~(QStyle::State_HasFocus | QStyle::State_Selected);
    QGraphicsTextItem::paint(painter, &textOption, widget);
    if (numberLabel) {
      painter->setPen(QPen(lineColor,lineWidth,Qt::SolidLine,Qt::RoundCap));
      const auto bounds=boundingRect();
      painter->drawLine(QPointF(-leader,bounds.bottom()-1),QPointF(bounds.right()+2,bounds.bottom()-1));
      return;
    }
    if (!frameVisible) return;
    QPen border(QColor("#303030"), 1);
    border.setCosmetic(true);
    painter->setPen(border);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(boundingRect());
  }
};

InlineTextEditor::InlineTextEditor(QWidget *parent) : QGraphicsView(parent) {
  setObjectName("inlineTextEditor");
  setFrameShape(QFrame::NoFrame);
  setStyleSheet("QGraphicsView#inlineTextEditor{background:transparent;border:0;}");
  setAttribute(Qt::WA_TranslucentBackground);
  viewport()->setAutoFillBackground(false);
  setBackgroundBrush(Qt::NoBrush);
  setAlignment(Qt::AlignLeft | Qt::AlignTop);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  auto canvas = new QGraphicsScene(this);
  setScene(canvas);
  item = new InlineTextItem;
  item->setTextInteractionFlags(Qt::TextEditorInteraction);
  item->setFlag(QGraphicsItem::ItemIsSelectable, false);
  canvas->addItem(item);
  connect(item->document(), &QTextDocument::contentsChanged, this, [this] {
    if (!styling) {
      model.text = item->toPlainText();
      showHandles = true;
      positionHandles();
    }
  });
  connect(item->document()->documentLayout(),
          &QAbstractTextDocumentLayout::documentSizeChanged,
          this, [this] { if (!styling) positionHandles(); });
  for (const auto &name : QStringList{"nw", "delete", "sw", "se", "rotate"}) {
    auto button = new QToolButton(viewport());
    button->setObjectName("text_" + name);
    button->setFixedSize(14, 14);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(QIcon(":/toolbar/text-" + name + ".png"));
    button->setIconSize({14, 14});
    button->setToolTip(name == "delete" ? "删除当前文字"
                       : name == "rotate" ? "旋转文字"
                                          : "缩放文字");
    button->setCursor(name == "delete" ? Qt::PointingHandCursor
                       : name == "rotate" ? Qt::SizeAllCursor
                       : name == "sw" ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor);
    button->setStyleSheet(
        "QToolButton{background:transparent;border:0;padding:0;}"
        "QToolButton:hover{background:rgba(32,128,240,60);}");
    if (name == "delete")
      connect(button, &QToolButton::clicked, this, &InlineTextEditor::cancelRequested);
    else
      button->installEventFilter(this);
    handles[name] = button;
  }
  hide();
}
void InlineTextEditor::begin(const Mark &mark, QRect display, QSize imageSize,
                             bool atInsertionPoint) {
  finishTransform();
  model = mark;
  const double sx = double(display.width()) / imageSize.width();
  const double sy = double(display.height()) / imageSize.height();
  // Cover the editing surface so a frame near the crop edge keeps its handles.
  // Scene (0, 0) still maps to the selected image's top-left corner.
  setGeometry(parentWidget()->rect());
  setSceneRect(-display.x() / sx, -display.y() / sy,
               width() / sx, height() / sy);
  setTransform(QTransform::fromScale(sx, sy));
  showHandles = true;
  styling = true;
  item->setPlainText(model.text);
  styling = false;
  applyModel();
  if (atInsertionPoint && model.kind == "text") {
    item->document()->documentLayout()->documentSize();
    const auto layout = item->document()->begin().layout();
    const auto line = layout && layout->lineCount() ? layout->lineAt(0) : QTextLine();
    const double height = line.isValid() ? line.height() : QFontMetricsF(item->font()).height();
    model.points[0] -= QPointF(0, height / 2);
    applyModel();
  }
  show();
  raise();
  setFocus(Qt::MouseFocusReason);
  item->setFocus(Qt::MouseFocusReason);
  auto cursor = item->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.setCharFormat(annotationTextFormat(model));
  item->setTextCursor(cursor);
  viewport()->setCursor(Qt::IBeamCursor);
}
Mark InlineTextEditor::value() const {
  auto mark = model;
  mark.text = item->toPlainText();
  return mark;
}
void InlineTextEditor::setPreferences(const Mark &preferences) {
  auto text = item->toPlainText();
  auto points = model.points;
  const int number=model.number;
  model = preferences;
  model.kind = static_cast<InlineTextItem *>(item)->numberLabel ? "number" : "text";
  model.text = text;
  model.points = points;
  model.number=number;
  applyModel();
}
void InlineTextEditor::applyModel() {
  styling = true;
  const auto cursor = item->textCursor();
  item->setFont(annotationFont(model));
  styleTextDocument(item->document(), model);
  auto updatedCursor = cursor;
  updatedCursor.setCharFormat(annotationTextFormat(model));
  item->setDefaultTextColor(model.color);
  item->setTextCursor(updatedCursor);
  auto label=static_cast<InlineTextItem *>(item); label->numberLabel=model.kind=="number";
  label->setFramePadding(qMax(4.0 / transform().m11(),
                              QFontMetricsF(item->font()).height() * .25));
  label->lineColor=model.color; label->lineWidth=qMax(1.,model.width*.5);
  label->leader=qMax(4.,model.fontSize*.18)+model.fontSize*.65*.4;
  item->setPos(label->numberLabel?numberLabelRect(model).topLeft():model.points.first());
  item->setRotation(label->numberLabel?0:model.rotation);
  styling = false;
  positionHandles();
}
void InlineTextEditor::positionHandles() {
  auto label = static_cast<InlineTextItem *>(item);
  label->frameVisible = activeHandle.isEmpty();
  if (model.kind=="number" || !activeHandle.isEmpty() || !showHandles) {
    for (auto handle:handles) handle->hide();
    viewport()->update(); return;
  }
  for (auto handle:handles) handle->show();
  const auto r = item->boundingRect();
  QMap<QString, QPointF> points{{"nw", r.topLeft()}, {"delete", r.topRight()},
                                {"sw", r.bottomLeft()}, {"se", r.bottomRight()}};
  for (auto i = points.begin(); i != points.end(); ++i)
    handles[i.key()]->move(mapFromScene(item->mapToScene(i.value())) - QPoint(7, 7));
  auto top = mapFromScene(item->mapToScene(QPointF(r.center().x(), r.top())));
  auto center = mapFromScene(item->mapToScene(r.center()));
  auto direction = QPointF(top - center);
  double length = qMax(1.0, std::hypot(direction.x(), direction.y()));
  handles["rotate"]->move((QPointF(top) + direction * (14.0 / length)).toPoint() -
                          QPoint(7, 7));
  for (auto i = handles.begin(); i != handles.end(); ++i)
    i.value()->setCursor(i.key() == "rotate" ? annotationRotationCursor()
                                            : QCursor(handleCursor(i.key())));
  // Controls stay 14 logical pixels independently of text scale and rotation.
  viewport()->update();
}
QPointF InlineTextEditor::scenePoint(QPoint global) const {
  return mapToScene(viewport()->mapFromGlobal(global));
}
bool InlineTextEditor::eventFilter(QObject *watched, QEvent *event) {
  auto name = handles.key(qobject_cast<QToolButton *>(watched));
  if (name.isEmpty())
    return QGraphicsView::eventFilter(watched, event);
  if (event->type() == QEvent::MouseButtonPress) {
    auto mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton)
      return false;
    beginTransform(name, mouse->globalPosition().toPoint());
    return true;
  }
  if (event->type() == QEvent::MouseMove && !activeHandle.isEmpty()) {
    auto mouse = static_cast<QMouseEvent *>(event);
    updateTransform(mouse->globalPosition().toPoint(), mouse->modifiers());
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease && !activeHandle.isEmpty()) {
    auto mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton) return true;
    updateTransform(mouse->globalPosition().toPoint(), mouse->modifiers());
    finishTransform();
    return true;
  }
  return QGraphicsView::eventFilter(watched, event);
}
Qt::CursorShape InlineTextEditor::handleCursor(const QString &handle) const {
  if (handle == "delete") return Qt::PointingHandCursor;
  if (handle == "move" || handle == "rotate") return Qt::SizeAllCursor;
  const QPointF direction(handle == "sw" ? -1 : 1, 1);
  const auto vector = QTransform().rotate(model.rotation).map(direction);
  const int angle = (qRound(std::atan2(vector.y(), vector.x()) * 4 / std::numbers::pi) % 4 + 4) % 4;
  const Qt::CursorShape cursors[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                 Qt::SizeVerCursor, Qt::SizeBDiagCursor};
  return cursors[angle];
}
void InlineTextEditor::beginTransform(const QString &handle, QPoint global) {
  activeHandle = handle;
  dragModel = value();
  dragPoint = scenePoint(global);
  const auto r = item->boundingRect();
  const auto fixed = handle == "rotate" ? r.center()
                       : handle == "nw" ? r.bottomRight()
                       : handle == "sw" ? r.topRight() : r.topLeft();
  fixedPoint = item->mapToScene(fixed);
  viewport()->setCursor(handle == "rotate" ? annotationRotationCursor()
                                           : QCursor(handleCursor(handle)));
  // Hiding the pressed corner must not cancel its mouse grab. The viewport
  // owns the whole drag, including movements outside the original frame.
  viewport()->grabMouse();
  positionHandles();
}
void InlineTextEditor::updateTransform(QPoint global, Qt::KeyboardModifiers modifiers) {
  if (activeHandle.isEmpty()) return;
  const auto point = scenePoint(global);
  if (activeHandle == "move") {
    model.points[0] = dragModel.points.first() + point - dragPoint;
    applyModel();
    return;
  }
  if (activeHandle == "rotate") {
    model.rotation = dragModel.rotation + qRound(
        (std::atan2(point.y() - fixedPoint.y(), point.x() - fixedPoint.x()) -
         std::atan2(dragPoint.y() - fixedPoint.y(), dragPoint.x() - fixedPoint.x())) *
        180 / std::numbers::pi);
    if (modifiers & Qt::ShiftModifier)
      model.rotation = qRound(model.rotation / 15.0) * 15;
  } else {
    const auto diagonal = dragPoint - fixedPoint;
    const double factor = QPointF::dotProduct(point - fixedPoint, diagonal) /
                          qMax(1.0, QPointF::dotProduct(diagonal, diagonal));
    model.fontSize = qBound(4.0 / 3, dragModel.fontSize * qMax(.05, factor), 1600.0 / 3);
  }
  applyModel();
  const auto r = item->boundingRect();
  const auto fixed = activeHandle == "rotate" ? r.center()
                       : activeHandle == "nw" ? r.bottomRight()
                       : activeHandle == "sw" ? r.topRight() : r.topLeft();
  model.points[0] = fixedPoint - QTransform().rotate(model.rotation).map(fixed);
  applyModel();
  emit geometryEdited(model.fontSize, model.rotation);
}
void InlineTextEditor::finishTransform() {
  if (activeHandle.isEmpty()) return;
  activeHandle.clear();
  if (QWidget::mouseGrabber() == viewport()) viewport()->releaseMouse();
  showHandles = true;
  positionHandles();
  emit geometryEditFinished();
}
void InlineTextEditor::updatePointer(QPoint point) {
  const auto local = item->mapFromScene(mapToScene(point));
  const auto frame = item->boundingRect();
  const double tolerance = 4.0 / qMin(transform().m11(), transform().m22());
  const bool near = frame.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(local);
  const bool border = near && !frame.adjusted(tolerance, tolerance, -tolerance, -tolerance).contains(local);
  bool nearHandle = false;
  for (auto button : handles)
    nearHandle |= button->geometry().adjusted(-3, -3, 3, 3).contains(point);
  showHandles = near || nearHandle;
  viewport()->setCursor(model.kind != "number" && border ? Qt::SizeAllCursor : Qt::IBeamCursor);
  positionHandles();
}
void InlineTextEditor::mousePressEvent(QMouseEvent *event) {
  auto point = mapToScene(event->position().toPoint());
  const auto local = item->mapFromScene(point);
  const double tolerance = 4.0 / qMin(transform().m11(), transform().m22());
  if (!item->boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(local)) {
    auto parentPoint = mapToParent(event->position().toPoint());
    const bool numbered=model.kind=="number";
    emit commitRequested();
    if (numbered) return;
    QMouseEvent forwarded(event->type(), QPointF(parentPoint), event->globalPosition(),
                           event->button(), event->buttons(), event->modifiers());
    QApplication::sendEvent(parentWidget(), &forwarded);
    return;
  }
  if (model.kind!="number" && event->button() == Qt::LeftButton &&
      !item->boundingRect().adjusted(tolerance, tolerance, -tolerance, -tolerance).contains(local)) {
    beginTransform("move", event->globalPosition().toPoint());
    return;
  }
  QGraphicsView::mousePressEvent(event);
}
void InlineTextEditor::mouseMoveEvent(QMouseEvent *event) {
  if (!activeHandle.isEmpty()) {
    updateTransform(event->globalPosition().toPoint(), event->modifiers());
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
  // QGraphicsTextItem installs its I-beam while processing hover events.
  // Apply the frame's move cursor afterward so it is not overwritten.
  updatePointer(event->position().toPoint());
}
void InlineTextEditor::mouseReleaseEvent(QMouseEvent *event) {
  if (!activeHandle.isEmpty() && event->button() == Qt::LeftButton) {
    updateTransform(event->globalPosition().toPoint(), event->modifiers());
    finishTransform();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}
void InlineTextEditor::keyPressEvent(QKeyEvent *event) {
  if (model.kind=="number") {
    if (event->key()==Qt::Key_Tab) { event->accept(); return; }
    if (event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) { emit commitRequested(); return; }
  }
  auto ctrl = event->modifiers() & Qt::ControlModifier;
  if (event->key() == Qt::Key_Escape) {
    emit cancelRequested();
    return;
  }
  if (ctrl && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
    emit commitRequested();
    return;
  }
  if (event->key() == Qt::Key_F3 || event->key() == Qt::Key_F4 || event->matches(QKeySequence::Save) ||
      (ctrl && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_8)) {
    emit commandRequested(event->key(), event->modifiers());
    return;
  }
  QGraphicsView::keyPressEvent(event);
}
bool InlineTextEditor::focusNextPrevChild(bool next) {
  return model.kind=="number" ? false : QGraphicsView::focusNextPrevChild(next);
}
void InlineTextEditor::wheelEvent(QWheelEvent *event) {
  if (!activeHandle.isEmpty()) { event->accept(); return; }
  if (event->angleDelta().y())
    emit sizeStepRequested(event->angleDelta().y() > 0 ? 1 : -1);
  event->accept();
}
void InlineTextEditor::hideEvent(QHideEvent *event) {
  finishTransform();
  QGraphicsView::hideEvent(event);
}
