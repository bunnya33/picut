#include "editing.h"
#include "menu.h"
#include "recognition.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>
#include <QtGui/QGuiApplication>
#include <QtGui/QActionGroup>
#include <QtGui/QCursor>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMenu>
#include <cmath>
#include <numbers>

EditingSurface::EditingSurface(Store *s, QWidget *parent, Qt::WindowFlags flags)
    : QWidget(parent, flags), store(s) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
}
EditingSurface::~EditingSurface() {
  if (textEntry) {
    textEntry->disconnect(this);
    delete textEntry;
    textEntry = nullptr;
  }
  if (imageTranslation) {
    imageTranslation->disconnect(this); delete imageTranslation; imageTranslation = nullptr;
  }
  // Remove the parent event filter before annotation members are destroyed.
  // Also stop an in-flight worker before QWidget begins hiding its children.
  if (imageText) {
    imageText->disconnect(this);
    delete imageText;
    imageText = nullptr;
  }
}
QPointF EditingSurface::imagePoint(QPoint point) const {
  if (!annotation || contentRect().isEmpty())
    return {};
  auto r = contentRect();
  return {
      (point.x() - r.x()) * double(annotation->original.width()) / r.width(),
      (point.y() - r.y()) * double(annotation->original.height()) / r.height()};
}
void EditingSurface::chooseTool(QString tool) {
  if (!annotation)
    return;
  commitText();
  annotation->chooseTool(tool);
  editing = tool != "select";
  annotation->savePreferences(store);
  showToolbar();
  updateDrawingCursor(pointer);
  update();
}
void EditingSurface::showToolbar() {
  if (!annotation)
    return;
  if (!toolbar) {
    toolbar = new Toolbar(this);
    toolbar->findChild<QToolButton *>("action_scroll")->setVisible(property("supportsScrolling").toBool());
    toolbar->findChild<QToolButton *>("action_record")->setVisible(property("supportsRecording").toBool());
    connect(toolbar, &Toolbar::toolChosen, this, &EditingSurface::chooseTool);
    connect(toolbar, &Toolbar::sizeChanged, this, &EditingSurface::positionToolbar);
    connect(toolbar, &Toolbar::preferencesChanged, this,
            &EditingSurface::setPreference);
    connect(toolbar, &Toolbar::actionRequested, this,
            [this](const QString &action) {
              if (action == "undo")
                annotation->undo();
              else if (action == "redo")
                annotation->redo();
              else if (action == "more")
                showMore();
              else if (action == "select_text")
                startImageText();
              else if (action == "translate")
                startImageTranslation();
              else
                complete(action == "save" && QApplication::keyboardModifiers() &
                                                 Qt::ShiftModifier
                             ? "quick_save"
                             : action);
              if (annotation) {
                toolbar->sync(*annotation);
                update();
              }
            });
  }
  toolbar->sync(*annotation);
  positionToolbar();
  toolbar->show();
  toolbar->raise();
}
void EditingSurface::positionToolbar() {
  positionBar(toolbar);
}
void EditingSurface::positionBar(QWidget *bar) {
  if (!bar)
    return;
  bar->adjustSize();
  auto r = contentRect();
  auto screen = QGuiApplication::screenAt(mapToGlobal(r.center()));
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  auto available = QRect(mapFromGlobal(screen->availableGeometry().topLeft()),
                         screen->availableGeometry().size());
  if (!bar->isWindow())
    available = available.intersected(rect());
  available.adjust(4, 4, -4, -4);
  const int anchorWidth = bar == toolbar ? toolbar->mainBar->sizeHint().width()
                                         : bar->width();
  int x =
      qBound(available.left(), r.right() - anchorWidth + 1,
             qMax(available.left(), available.right() - bar->width() + 1));
  int y = r.bottom() + 7;
  if (y + bar->height() - 1 > available.bottom())
    y = r.top() - bar->height() - 7;
  auto location =
      QPoint(x, qBound(available.top(), y,
                       qMax(available.top(),
                            available.bottom() - bar->height() + 1)));
  bar->move(bar->isWindow() ? mapToGlobal(location) : location);
}
void EditingSurface::setPreference(QString key, QVariant value) {
  if (!annotation)
    return;
  if (key == "width" && annotation->tool == "pen" && annotation->draft)
    return;
  auto &p = annotation->preferences;
  if (key == "line_type") {
    auto type = value.toString();
    auto kind = type.section(':', 0, 0);
    if (annotation->tool != kind)
      chooseTool(kind);
    p.arrowHead = type.section(':', 1);
  } else if (key == "color")
    p.color = value.value<QColor>();
  else if (key == "width")
    p.width = qBound(1.0, value.toDouble(), 100.0);
  else if (key == "filled")
    p.filled = value.toBool();
  else if (key == "font_size")
    p.fontSize = qBound(4.0 / 3, value.toDouble(), 1600.0 / 3);
  else if (key == "font_family")
    p.fontFamily = value.toString();
  else if (key == "bold")
    p.bold = value.toBool();
  else if (key == "italic")
    p.italic = value.toBool();
  else if (key == "text_outline_enabled") {
    p.outlineWidth = value.toBool() ? 1 : 0;
    if (p.outlineWidth && !p.textOutline.alpha())
      p.textOutline = p.color.lightness() < 128 ? Qt::white : Qt::black;
  }
  else if (key == "intensity")
    p.intensity = qBound(2, value.toInt(), 40);
  else if (key == "area_shape") {
    p.areaShape = value.toString();
    if ((annotation->tool == "mosaic" || annotation->tool == "blur") &&
        p.areaShape != "ellipse")
      p.areaShape = "rect";
    if (annotation->tool == "marker" && p.areaShape != "rect") {
      p.areaShape = "stroke";
      p.rotation = 0;
    }
  }
  else if (key == "direction_cycle")
    p.arrowMode = p.arrowMode == "forward"   ? "reverse"
                  : p.arrowMode == "reverse" ? "both"
                                             : "forward";
  else if (key == "dash")
    p.dash = value.toString();
  else if (key == "rotation")
    p.rotation = value.toInt();
  else if (key == "radius")
    p.radius = value.toInt();
  annotation->applyPreferences();
  annotation->savePreferences(store);
  if (toolbar)
    toolbar->sync(*annotation);
  if (textEntry && textEntry->isVisible())
    textEntry->setPreferences(p);
  positionToolbar();
  updateDrawingCursor(pointer);
  update();
}
void EditingSurface::paintEditing(QPainter &p) {
  if (!annotation || !editing)
    return;
  auto r = contentRect();
  p.save();
  p.translate(r.topLeft());
  p.scale(double(r.width()) / annotation->original.width(),
          double(r.height()) / annotation->original.height());
  annotation->paintControls(p, bool(dragOriginal));
  p.restore();
  bool brush = annotation->tool == "pen" || annotation->tool == "arrow" ||
               annotation->tool == "line" || annotation->tool == "marker" ||
               annotation->tool == "eraser";
  if (brush && r.contains(pointer) && !annotation->draft && !dragOriginal &&
      editTarget(pointer).isEmpty()) {
    auto color = annotation->preferences.color;
    double radius = annotation->preferences.width * .5 * r.width() /
                    annotation->original.width();
    if (annotation->tool == "marker")
      radius *= 3;
    p.save();
    if (annotation->tool == "pen" || annotation->tool == "arrow" ||
        annotation->tool == "line") {
      p.setRenderHint(QPainter::Antialiasing);
      const auto center = QPointF(pointer);
      const double dot = qMax(.5 / devicePixelRatioF(), radius);
      const double inner = qMax(3.0, dot + 3), outer = inner + 5;
      const QLineF arms[] = {
          {center - QPointF(outer, 0), center - QPointF(inner, 0)},
          {center + QPointF(inner, 0), center + QPointF(outer, 0)},
          {center - QPointF(0, outer), center - QPointF(0, inner)},
          {center + QPointF(0, inner), center + QPointF(0, outer)}};
      // Keep the center clear of the reticle: the dot alone previews the
      // stroke diameter, while a white halo keeps the short arms visible.
      p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::FlatCap));
      p.drawLines(arms, 4);
      p.setPen(QPen(color, 1, Qt::SolidLine, Qt::FlatCap));
      p.drawLines(arms, 4);
      p.setPen(Qt::NoPen);
      p.setBrush(Qt::white);
      p.drawEllipse(center, dot + 1, dot + 1);
      p.setBrush(color);
      p.drawEllipse(center, dot, dot);
    } else {
      p.setPen(QPen(color, 1));
      p.setBrush(color);
      p.drawEllipse(QPointF(pointer), qMax(1.0, radius), qMax(1.0, radius));
    }
    p.restore();
  }
}
QString EditingSurface::editTarget(QPoint point) const {
  if (!annotation || annotation->selected < 0 ||
      annotation->selected >= annotation->marks.size())
    return {};
  auto mark = annotation->marks[annotation->selected];
  if (mark.kind == "pen" && !mark.straight)
    return {};
  auto location = imagePoint(point);
  double tolerance =
      7.0 * annotation->original.width() / qMax(1, contentRect().width());
  auto controls = markControls(mark);
  for (auto i = controls.begin(); i != controls.end(); ++i)
    if (QLineF(i.value(), location).length() <= tolerance)
      return i.key();
  return paintedHit(mark, location, 2) ? "move" : QString();
}
void EditingSurface::updateDrawingCursor(QPoint point) {
  auto target = editTarget(point);
  if (target == "move")
    setCursor(Qt::SizeAllCursor);
  else if (target == "rotate")
    setCursor(annotationRotationCursor());
  else if (QStringList{"n", "s", "w", "e", "nw", "ne", "sw", "se"}.contains(target)) {
    const QPointF direction(target.contains('e') ? 1 : target.contains('w') ? -1 : 0,
                            target.contains('s') ? 1 : target.contains('n') ? -1 : 0);
    const auto transform = markTransform(annotation->marks[annotation->selected]);
    const auto vector = transform.map(direction) - transform.map(QPointF());
    const int angle = (qRound(std::atan2(vector.y(), vector.x()) * 4 / std::numbers::pi) % 4 + 4) % 4;
    const Qt::CursorShape shapes[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                   Qt::SizeVerCursor, Qt::SizeBDiagCursor};
    setCursor(shapes[angle]);
  }
  else if (!target.isEmpty())
    setCursor(Qt::SizeAllCursor);
  else if (editing && annotation) {
    bool brush =
        QStringList{"pen", "line", "arrow", "marker", "eraser"}.contains(
            annotation->tool);
    setCursor(brush ? Qt::BlankCursor
                   : annotation->tool == "text" && contentRect().contains(point)
                       ? Qt::IBeamCursor : Qt::CrossCursor);
  } else
    setCursor(Qt::ArrowCursor);
}
bool EditingSurface::annotationPress(QMouseEvent *e) {
  if (!annotation || !editing || e->button() != Qt::LeftButton)
    return false;
  auto point = e->position().toPoint();
  pointer = point;
  auto target = editTarget(point);
  if (!contentRect().contains(point) && (target.isEmpty() || target == "move")) {
    commitText();
    annotation->confirm();
    update();
    return false;
  }
  auto image = imagePoint(point);
  if (!target.isEmpty()) {
    dragHandle = target;
    dragOrigin = image;
    dragOriginal = annotation->marks[annotation->selected];
    annotation->checkpoint();
    if (e->modifiers() & Qt::ControlModifier) {
      annotation->marks.append(*dragOriginal);
      annotation->selected = int(annotation->marks.size() - 1);
    }
    updateDrawingCursor(point);
    return true;
  }
  suppressDraw = annotation->selected >= 0 && annotation->tool != "number";
  annotation->confirm();
  if (annotation->tool == "text") {
    commitText();
    ensureTextEditor();
    annotation->preferences.rotation = 0;
    auto mark = annotation->preferences;
    mark.kind = "text";
    mark.text.clear();
    mark.points = {image};
    textEntry->begin(mark, contentRect(), annotation->original.size(), true);
    if (toolbar) toolbar->raise();
    return true;
  }
  if (annotation->tool == "polyline" && annotation->draft) {
    annotation->draft->points.append(image);
    return true;
  }
  annotation->preferences.rotation = 0;
  auto mark = annotation->preferences;
  mark.kind = annotation->tool;
  // Choose once per stroke so releasing Shift before the mouse does not
  // convert a straight line into a freehand path.
  mark.straight = mark.kind == "pen" && (e->modifiers() & Qt::ShiftModifier);
  mark.points = {image, image};
  annotation->draft = mark;
  if (mark.kind == "pen" && toolbar)
    toolbar->sync(*annotation);
  update();
  return true;
}
bool EditingSurface::annotationMove(QMouseEvent *e) {
  pointer = e->position().toPoint();
  if (!annotation || !editing)
    return false;
  auto point = imagePoint(pointer);
  if (dragOriginal && annotation->selected >= 0) {
    auto mark = *dragOriginal;
    if (dragHandle == "move") {
      auto delta = point - dragOrigin;
      for (auto &p : mark.points)
        p += delta;
    } else if (dragHandle == "start" || dragHandle == "end") {
      int index = dragHandle == "start" ? 0 : int(mark.points.size() - 1);
      auto fixed = mark.points[index ? 0 : mark.points.size() - 1];
      if (mark.kind != "pen" && (e->modifiers() & Qt::ShiftModifier)) {
        auto d = point - fixed;
        double angle =
            std::round(std::atan2(d.y(), d.x()) / (std::numbers::pi / 4)) *
            (std::numbers::pi / 4);
        double length = std::hypot(d.x(), d.y());
        point =
            fixed + QPointF(std::cos(angle) * length, std::sin(angle) * length);
      }
      mark.points[index] = point;
    } else if (dragHandle == "rotate") {
      auto center = markBounds(mark).center();
      mark.rotation =
          dragOriginal->rotation +
          qRound((std::atan2(point.y() - center.y(), point.x() - center.x()) -
                  std::atan2(dragOrigin.y() - center.y(),
                             dragOrigin.x() - center.x())) *
                 180 / std::numbers::pi);
    } else {
      auto r = QRectF(mark.points.first(), mark.points.last()).normalized();
      auto t = markTransform(mark);
      auto inverse = t.inverted();
      auto delta = inverse.map(point) - inverse.map(dragOrigin);
      if (dragHandle.contains('w'))
        r.setLeft(qMin(r.right() - 2, r.left() + delta.x()));
      if (dragHandle.contains('e'))
        r.setRight(qMax(r.left() + 2, r.right() + delta.x()));
      if (dragHandle.contains('n'))
        r.setTop(qMin(r.bottom() - 2, r.top() + delta.y()));
      if (dragHandle.contains('s'))
        r.setBottom(qMax(r.top() + 2, r.bottom() + delta.y()));
      if (e->modifiers() & Qt::ShiftModifier) {
        auto old = QRectF(mark.points.first(), mark.points.last()).normalized();
        r.setHeight(r.width() * old.height() / qMax(1.0, old.width()));
      }
      auto old = markBounds(mark);
      if (isBoxMark(mark)) {
        auto offset = t.map(r.center()) - r.center();
        mark.points = {r.topLeft() + offset, r.bottomRight() + offset};
      } else {
        for (auto &p : mark.points)
          p = {r.x() + (p.x() - old.x()) * r.width() / qMax(1.0, old.width()),
               r.y() +
                   (p.y() - old.y()) * r.height() / qMax(1.0, old.height())};
      }
    }
    annotation->marks[annotation->selected] = mark;
    annotation->preferences.rotation = mark.rotation;
    annotation->refresh();
    update();
    return true;
  }
  if (annotation->draft && e->buttons() & Qt::LeftButton &&
      annotation->tool != "polyline") {
    auto &mark = *annotation->draft;
    if (mark.kind == "number") {
      // A numbered circle is placed at the press position, not dragged out.
    } else if (mark.kind == "pen" && mark.straight) {
      mark.points.last() = point;
    } else if (QStringList{"pen", "eraser", "mosaic", "blur"}.contains(mark.kind) &&
        mark.areaShape == "stroke") {
      if (mark.points.last() != point)
        mark.points.append(point);
    } else {
      auto first = mark.points.first();
      if (e->modifiers() & Qt::ShiftModifier) {
        auto delta = point - first;
        if (isEndpointMark(mark)) {
          double angle = std::round(std::atan2(delta.y(), delta.x()) /
                                    (std::numbers::pi / 4)) *
                         (std::numbers::pi / 4);
          double length = std::hypot(delta.x(), delta.y());
          point = first +
                  QPointF(std::cos(angle) * length, std::sin(angle) * length);
        } else {
          auto size = qMax(qAbs(delta.x()), qAbs(delta.y()));
          point = first + QPointF(delta.x() < 0 ? -size : size,
                                  delta.y() < 0 ? -size : size);
        }
      }
      mark.points.last() = point;
    }
    update();
    return true;
  }
  updateDrawingCursor(pointer);
  update();
  return false;
}
bool EditingSurface::annotationRelease(QMouseEvent *e) {
  if (!annotation || !editing || e->button() != Qt::LeftButton)
    return false;
  if (dragOriginal || (annotation->draft && annotation->tool != "polyline")) {
    // The final mouse position may arrive only in the release event.
    QMouseEvent finalMove(QEvent::MouseMove, e->position(), e->globalPosition(),
                         Qt::NoButton, Qt::LeftButton, e->modifiers());
    annotationMove(&finalMove);
  }
  if (dragOriginal) {
    annotation->savePreferences(store);
    dragOriginal.reset();
    dragHandle.clear();
    updateDrawingCursor(e->position().toPoint());
    update();
    return true;
  }
  if (annotation->draft && annotation->tool != "polyline") {
    auto mark = *annotation->draft;
    annotation->draft.reset();
    if (suppressDraw &&
        QLineF(mark.points.first(), mark.points.last()).length() < 2) {
      suppressDraw = false;
      update();
      return true;
    }
    annotation->add(mark);
    if (toolbar)
      toolbar->sync(*annotation);
    annotation->savePreferences(store);
    updateDrawingCursor(e->position().toPoint());
    update();
    return true;
  }
  return false;
}
void EditingSurface::annotationWheel(QWheelEvent *e) {
  if (!annotation || !e->angleDelta().y())
    return;
  annotation->adjustWidth(e->angleDelta().y() > 0 ? 1 : -1);
  annotation->savePreferences(store);
  if (toolbar)
    toolbar->sync(*annotation);
  if (textEntry && textEntry->isVisible())
    textEntry->setPreferences(annotation->preferences);
  positionToolbar();
  update();
}
bool EditingSurface::annotationKey(QKeyEvent *e) {
  if (!annotation)
    return false;
  if (e->key() == Qt::Key_F4) {
    startImageText();
    return true;
  }
  auto ctrl = e->modifiers() & Qt::ControlModifier;
  if (!ctrl && e->key()==Qt::Key_Tab && annotation->selected>=0 &&
      annotation->marks[annotation->selected].kind=="number") {
    ensureTextEditor(); numberLabelIndex=annotation->selected;
    auto mark=annotation->marks[numberLabelIndex];
    annotation->marks[numberLabelIndex].text.clear(); annotation->refresh();
    textEntry->begin(mark,contentRect(),annotation->original.size());
    if (toolbar) toolbar->raise();
    update(); return true;
  }
  if (ctrl && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_8) {
    chooseTool(QStringList{"rect", "arrow", "pen", "marker", "mosaic", "text",
                           "eraser", "number"}[e->key() - Qt::Key_1]);
    return true;
  }
  if (e->matches(QKeySequence::Undo)) {
    annotation->undo();
    if (toolbar) toolbar->sync(*annotation);
    update();
    return true;
  }
  if (e->matches(QKeySequence::Redo)) {
    annotation->redo();
    if (toolbar) toolbar->sync(*annotation);
    update();
    return true;
  }
  if (e->key() == Qt::Key_Delete && annotation->selected >= 0) {
    annotation->checkpoint();
    annotation->marks.removeAt(annotation->selected);
    annotation->confirm();
    annotation->refresh();
    update();
    return true;
  }
  if (e->key() == Qt::Key_F10) {
    showMore();
    return true;
  }
  if (e->key() == Qt::Key_B && !ctrl) {
    chooseTool("pen");
    return true;
  }
  if (e->key() == Qt::Key_T && !ctrl) {
    chooseTool("text");
    return true;
  }
  if (e->key() == Qt::Key_Tab &&
      (annotation->tool == "arrow" || annotation->tool == "line")) {
    chooseTool(annotation->tool == "arrow" ? "line" : "arrow");
    return true;
  }
  if (!ctrl && (e->key() == Qt::Key_1 || e->key() == Qt::Key_2)) {
    annotation->adjustWidth(e->key() == Qt::Key_1 ? -1 : 1);
    annotation->savePreferences(store);
    if (toolbar)
      toolbar->sync(*annotation);
    update();
    return true;
  }
  if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
      annotation->draft && annotation->tool == "polyline") {
    auto mark = *annotation->draft;
    annotation->draft.reset();
    annotation->add(mark);
    update();
    return true;
  }
  return false;
}
bool EditingSurface::event(QEvent *event) {
  if (event->type()==QEvent::KeyPress) {
    auto key=static_cast<QKeyEvent *>(event);
    if (key->key()==Qt::Key_Tab && annotation && annotationKey(key)) return true;
  }
  return QWidget::event(event);
}
void EditingSurface::ensureTextEditor() {
    if (!textEntry) {
      textEntry = new InlineTextEditor(this);
      connect(textEntry, &InlineTextEditor::commitRequested,
              this, &EditingSurface::commitText);
      connect(textEntry, &InlineTextEditor::cancelRequested, this, [this] {
        textEntry->hide();
        if (numberLabelIndex>=0) { numberLabelIndex=-1; annotation->confirm(); annotation->refresh(); }
        setFocus();
        update();
      });
      connect(textEntry, &InlineTextEditor::sizeStepRequested, this, [this](int steps) {
        setPreference("font_size", annotation->preferences.fontSize + steps * 4.0 / 3);
      });
      connect(textEntry, &InlineTextEditor::commandRequested, this,
              [this](int key, Qt::KeyboardModifiers modifiers) {
                commitText();
                QKeyEvent event(QEvent::KeyPress, key, modifiers);
                QApplication::sendEvent(this, &event);
              });
      connect(textEntry, &InlineTextEditor::geometryEdited, this,
              [this](double size, int rotation) {
                annotation->preferences.fontSize = size;
                annotation->preferences.rotation = rotation;
                if (toolbar) toolbar->sync(*annotation);
                positionToolbar();
              });
      connect(textEntry, &InlineTextEditor::geometryEditFinished, this, [this] {
        annotation->savePreferences(store);
      });
    }
}
void EditingSurface::commitText() {
  if (!textEntry || !textEntry->isVisible() || !annotation)
    return;
  auto mark = textEntry->value();
  textEntry->hide();
  if (mark.kind=="number" && numberLabelIndex>=0 && numberLabelIndex<annotation->marks.size()) {
    if (mark.text.trimmed().isEmpty()) mark.text.clear();
    annotation->marks[numberLabelIndex]=mark; numberLabelIndex=-1;
    annotation->confirm(); annotation->refresh();
  } else if (!mark.text.trimmed().isEmpty()) {
    annotation->add(mark);
    annotation->confirm();
    if (toolbar)
      toolbar->sync(*annotation);
  }
  setFocus();
  update();
}
void EditingSurface::startImageText(const QImage &input, bool recognize) {
  showImageText(input, recognize, false);
}
void EditingSurface::showImageText(const QImage &input, bool recognize, bool restorePin) {
  if (imageTranslation && imageTranslation->isVisible()) imageTranslation->finish();
  if (imageText && imageText->isVisible()) { imageText->setFocus(); return; }
  commitText();
  const auto image = input.isNull() && annotation ? annotation->render() : input;
  if (image.isNull()) return;
  if (!imageText) {
    imageText = new ImageTextOverlay(this, store, [this] { return contentRect(); });
    connect(imageText, &ImageTextOverlay::translationRequested, this, [this](QImage image,QJsonObject ocr) {
      imageText->finish(); startImageTranslation(image,false); imageTranslation->setOcrResult(ocr);
    });
    connect(imageText, &ImageTextOverlay::closed, this, [this, restorePin] {
      if (restorePin) move(imageText->imageGlobalOrigin() - contentRect().topLeft());
      if (isVisible() && property("imageTextToolbarVisible").toBool() && annotation && toolbar) {
        toolbar->sync(*annotation); positionToolbar(); toolbar->show();
      }
      if (isVisible()) { activateWindow(); setFocus(); }
      if (restorePin && isVisible()) repaint();
      else update();
    });
  }
  setProperty("imageTextToolbarVisible", toolbar && toolbar->isVisible());
  if (toolbar) toolbar->hide();
  imageText->begin(image, recognize);
  if (restorePin) repaint();
}
void EditingSurface::startImageTranslation(const QImage &input, bool recognize) {
  if (imageTranslation && imageTranslation->isVisible()) { imageTranslation->setFocus(); return; }
  if (imageText && imageText->isVisible()) imageText->finish();
  commitText();
  const auto image = input.isNull() && annotation ? annotation->render() : input;
  if (image.isNull()) return;
  if (!imageTranslation) {
    imageTranslation = new ImageTranslationOverlay(this, store, [this] { return contentRect(); });
    connect(imageTranslation, &ImageTranslationOverlay::closed, this, [this] {
      if (isVisible() && property("translationToolbarVisible").toBool() && annotation && toolbar) {
        toolbar->sync(*annotation); positionToolbar(); toolbar->show();
      }
      if (isVisible()) { activateWindow(); setFocus(); }
      update();
    });
  }
  setProperty("translationToolbarVisible", toolbar && toolbar->isVisible());
  if (toolbar) toolbar->hide();
  imageTranslation->begin(image, recognize);
}
void EditingSurface::showMore() {
  QMenu menu(this);
  menu.setObjectName("annotationMenu");
  menu.addAction(menuLabel("撤销", "Ctrl+Z"), this, [this] {
    annotation->undo();
    if (toolbar) toolbar->sync(*annotation);
    update();
  });
  menu.addAction(menuLabel("重做", "Ctrl+Y"), this, [this] {
    annotation->redo();
    if (toolbar) toolbar->sync(*annotation);
    update();
  });
  menu.addSeparator();
  menu.addAction(menuLabel("文字识别", "F4"), this,
                   [this] { startImageText(); });
  menu.addAction("图片翻译（本地）", this, [this] { startImageTranslation(); });
  menu.addAction("二维码 / 条形码", this, [this] {
    commitText();
    showRecognition(annotation->render(), true, this);
  });
  auto tools = menu.addMenu("标注工具");
  auto toolChoices = new QActionGroup(tools);
  auto addTool = [&](const QString &tool, const QString &title) {
    auto action = tools->addAction(title, this, [this, tool] { chooseTool(tool); });
    toolChoices->addAction(action);
    action->setCheckable(true);
    action->setChecked(annotation->tool == tool ||
        (tool == "rect" && annotation->tool == "ellipse") ||
        (tool == "arrow" && annotation->tool == "line") ||
        (tool == "mosaic" && annotation->tool == "blur"));
  };
  QStringList primaryTools{"rect", "arrow", "pen", "marker", "mosaic", "text", "eraser", "number"};
  QStringList titles{"矩形 / 椭圆", "线条 / 箭头", "画笔", "马克笔", "马赛克 / 模糊", "文字", "橡皮擦", "序号"};
  for (int n = 0; n < primaryTools.size(); ++n)
    addTool(primaryTools[n], menuLabel(titles[n], "Ctrl+" + QString::number(n + 1)));
  tools->addSeparator();
  for (const auto &pair :
       QList<QPair<QString, QString>>{{"select", "移动 / 调整选区"},
                                      {"polyline", "折线"},
                                      {"magnify", "放大镜标注"}})
    addTool(pair.first, pair.second);
  auto dash = menu.addMenu("线条样式");
  auto dashChoices = new QActionGroup(dash);
  for (const auto &pair : QList<QPair<QString, QString>>{
           {"solid", "实线"}, {"dash", "虚线"}, {"dot", "点线"}}) {
    auto action = dash->addAction(pair.second, this,
                    [this, pair] { setPreference("dash", pair.first); });
    dashChoices->addAction(action);
    action->setCheckable(true);
    action->setChecked(annotation->preferences.dash == pair.first);
  }
  menu.addAction("圆角…", this, [this] {
    bool ok;
    int v = QInputDialog::getInt(
        this, "圆角", "半径", annotation->preferences.radius, 0, 100, 1, &ok);
    if (ok)
      setPreference("radius", v);
  });
  auto rotation = menu.addAction("旋转角度…", this, [this] {
    bool ok;
    int v = QInputDialog::getInt(this, "旋转", "角度",
                                 annotation->preferences.rotation, -180, 180, 1,
                                 &ok);
    if (ok)
      setPreference("rotation", v);
  });
  rotation->setEnabled(annotation->tool != "pen" &&
                       !(annotation->tool == "marker" &&
                         annotation->preferences.areaShape == "stroke"));
  menu.exec(QCursor::pos());
}
