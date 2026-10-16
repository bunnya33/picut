#include "text_editor.h"
#include <QtCore/QSignalBlocker>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QTextCursor>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsTextItem>
#include <cmath>
#include <numbers>

class InlineTextItem : public QGraphicsTextItem {
public:
  QRectF boundingRect() const override {
    return QGraphicsTextItem::boundingRect().united({0, 0, 24, 30});
  }
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget) override {
    QGraphicsTextItem::paint(painter, option, widget);
    QPen border(QColor("#c0c0c0"), 1);
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
    button->setCursor(name == "delete" ? Qt::ArrowCursor
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
void InlineTextEditor::begin(const Mark &mark, QRect display, QSize imageSize) {
  model = mark;
  setGeometry(display);
  setSceneRect(QRectF(QPointF(), imageSize));
  setTransform(QTransform::fromScale(double(display.width()) / imageSize.width(),
                                    double(display.height()) / imageSize.height()));
  styling = true;
  item->setPlainText(model.text);
  styling = false;
  applyModel();
  show();
  raise();
  setFocus(Qt::MouseFocusReason);
  item->setFocus(Qt::MouseFocusReason);
  auto cursor = item->textCursor();
  cursor.movePosition(QTextCursor::End);
  cursor.setCharFormat(annotationTextFormat(model));
  item->setTextCursor(cursor);
}
Mark InlineTextEditor::value() const {
  auto mark = model;
  mark.text = item->toPlainText();
  return mark;
}
void InlineTextEditor::setPreferences(const Mark &preferences) {
  auto text = item->toPlainText();
  auto points = model.points;
  model = preferences;
  model.kind = "text";
  model.text = text;
  model.points = points;
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
  item->setPos(model.points.first());
  item->setRotation(model.rotation);
  styling = false;
  positionHandles();
}
void InlineTextEditor::positionHandles() {
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
  // The text item's border is a thin neutral outline; controls stay 14 px at
  // every image zoom and DPI, independently of the editable text transform.
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
    activeHandle = name;
    dragModel = value();
    dragPoint = scenePoint(mouse->globalPosition().toPoint());
    const auto r = markBounds(dragModel);
    const auto fixed = name == "nw" ? r.bottomRight()
                         : name == "sw" ? r.topRight() : r.topLeft();
    fixedPoint = markTransform(dragModel).map(fixed);
    return true;
  }
  if (event->type() == QEvent::MouseMove && !activeHandle.isEmpty()) {
    auto mouse = static_cast<QMouseEvent *>(event);
    const auto point = scenePoint(mouse->globalPosition().toPoint());
    if (activeHandle == "rotate") {
      auto pivot = dragModel.points.first();
      model.rotation = dragModel.rotation + qRound(
          (std::atan2(point.y() - pivot.y(), point.x() - pivot.x()) -
           std::atan2(dragPoint.y() - pivot.y(), dragPoint.x() - pivot.x())) *
          180 / std::numbers::pi);
      if (mouse->modifiers() & Qt::ShiftModifier)
        model.rotation = qRound(model.rotation / 15.0) * 15;
    } else {
      auto diagonal = dragPoint - fixedPoint;
      auto factor = QPointF::dotProduct(point - fixedPoint, diagonal) /
                    qMax(1.0, QPointF::dotProduct(diagonal, diagonal));
      model.fontSize = qBound(4.0 / 3, dragModel.fontSize * qMax(.05, factor),
                              1600.0 / 3);
      const auto size = textSize(model);
      QPointF offset = activeHandle == "nw" ? QPointF(size.width(), size.height())
                           : activeHandle == "sw" ? QPointF(size.width(), 0)
                                                   : QPointF();
      model.points[0] = fixedPoint - QTransform().rotate(model.rotation).map(offset);
    }
    applyModel();
    emit geometryEdited(model.fontSize, model.rotation);
    return true;
  }
  if (event->type() == QEvent::MouseButtonRelease && !activeHandle.isEmpty()) {
    activeHandle.clear();
    return true;
  }
  return QGraphicsView::eventFilter(watched, event);
}
void InlineTextEditor::mousePressEvent(QMouseEvent *event) {
  auto point = mapToScene(event->position().toPoint());
  const auto local = item->mapFromScene(point);
  if (!item->boundingRect().adjusted(-3, -3, 3, 3).contains(local)) {
    auto parentPoint = mapToParent(event->position().toPoint());
    emit commitRequested();
    QMouseEvent forwarded(event->type(), QPointF(parentPoint), event->globalPosition(),
                           event->button(), event->buttons(), event->modifiers());
    QApplication::sendEvent(parentWidget(), &forwarded);
    return;
  }
  if (event->button() == Qt::LeftButton &&
      !item->boundingRect().adjusted(3, 3, -3, -3).contains(local)) {
    activeHandle = "move";
    dragModel = value();
    dragPoint = point;
    viewport()->setCursor(Qt::SizeAllCursor);
    return;
  }
  QGraphicsView::mousePressEvent(event);
}
void InlineTextEditor::mouseMoveEvent(QMouseEvent *event) {
  if (activeHandle == "move") {
    model.points[0] = dragModel.points.first() +
                      mapToScene(event->position().toPoint()) - dragPoint;
    applyModel();
    return;
  }
  auto local = item->mapFromScene(mapToScene(event->position().toPoint()));
  bool inside = item->boundingRect().contains(local);
  viewport()->setCursor(inside && !item->boundingRect().adjusted(3, 3, -3, -3).contains(local)
                            ? Qt::SizeAllCursor : inside ? Qt::IBeamCursor
                                                        : Qt::CrossCursor);
  QGraphicsView::mouseMoveEvent(event);
}
void InlineTextEditor::mouseReleaseEvent(QMouseEvent *event) {
  if (activeHandle == "move") {
    mouseMoveEvent(event);
    activeHandle.clear();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}
void InlineTextEditor::keyPressEvent(QKeyEvent *event) {
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
void InlineTextEditor::wheelEvent(QWheelEvent *event) {
  if (event->angleDelta().y())
    emit sizeStepRequested(event->angleDelta().y() > 0 ? 1 : -1);
  event->accept();
}
