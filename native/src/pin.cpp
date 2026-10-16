#include "pin.h"
#include "menu.h"
#include "recognition.h"
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QUuid>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QMenu>
#include <cmath>

PinWindow::PinWindow(const QImage &value, Store *s, QRect source, QString key)
    : EditingSurface(s, nullptr,
                     Qt::FramelessWindowHint | Qt::Tool |
                         Qt::WindowStaysOnTopHint),
      image(value),
      id(key.isEmpty() ? QUuid::createUuid().toString(QUuid::Id128) : key) {
  setAttribute(Qt::WA_TranslucentBackground);
  setProperty("isPinWindow", true);
  setAttribute(Qt::WA_DeleteOnClose);
  if (!source.isEmpty()) {
    scale = double(source.width()) / image.width();
    resize(source.size() + QSize(Margin * 2, Margin * 2));
    move(source.topLeft() - QPoint(Margin, Margin));
  } else {
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
      screen = QGuiApplication::primaryScreen();
    auto area = screen->availableGeometry();
    setScale(qMin(1.0, qMin(area.width() * .7 / image.width(),
                            area.height() * .7 / image.height())));
    move(area.center() - rect().center());
  }
}
QRect PinWindow::contentRect() const {
  return rect().adjusted(Margin, Margin, -Margin, -Margin);
}
QImage PinWindow::displayedImage() const {
  QImage result = editing && annotation ? annotation->render() : image;
  if (!thumbnail.isEmpty())
    result = result.copy(thumbnail);
  if (gray)
    result = result.convertToFormat(QImage::Format_Grayscale8)
                 .convertToFormat(QImage::Format_ARGB32);
  if (inverted) {
    result = result.copy();
    result.invertPixels(QImage::InvertRgb);
  }
  return result;
}
void PinWindow::setScale(double value) {
  scale = qBound(.002, value, 5.0);
  auto size = thumbnail.isEmpty() ? image.size() : thumbnail.size();
  resize(qMax(32, qRound(size.width() * scale)) + Margin * 2,
         qMax(24, qRound(size.height() * scale)) + Margin * 2);
  positionToolbar();
  update();
  emit changed();
}
void PinWindow::setAlpha(double value) {
  alpha = qBound(.05, value, 1.0);
  setWindowOpacity(alpha);
  emit changed();
}
void PinWindow::setThrough(bool value) {
  through = value;
  setWindowFlag(Qt::WindowTransparentForInput, value);
  if (state == "visible")
    show();
  emit changed();
}
void PinWindow::startEditing() {
  if (editing)
    return;
  if (gray || inverted || !thumbnail.isEmpty()) {
    image = displayedImage();
    gray = inverted = false;
    thumbnail = {};
  }
  annotation = std::make_unique<Annotation>(image, store);
  auto tool = store->config["annotation_last_tool"].toString("arrow");
  editing = true;
  chooseTool(tool);
  if (toolbar) {
    toolbar->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                            Qt::WindowStaysOnTopHint);
    positionToolbar();
    toolbar->show();
  }
  setThrough(false);
  activateWindow();
  setFocus();
}
void PinWindow::endEditing() {
  if (!annotation)
    return;
  commitText();
  annotation->savePreferences(store);
  image = annotation->render();
  annotation.reset();
  editing = false;
  dragOriginal.reset();
  if (toolbar)
    toolbar->hide();
  setCursor(Qt::ArrowCursor);
  update();
  emit changed();
}
bool PinWindow::complete(const QString &action) {
  commitText();
  if (action == "pin" || action == "cancel") {
    endEditing();
    return true;
  }
  emit requested(this, action);
  return true;
}
void PinWindow::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setCompositionMode(QPainter::CompositionMode_Source);
  p.fillRect(rect(), Qt::transparent);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  auto r = contentRect();
  QRectF frame = QRectF(r).adjusted(-.5, -.5, .5, .5);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setBrush(Qt::NoBrush);
  const int alphas[] = {97, 68, 45, 30, 19, 12, 6, 4, 2};
  for (int n = 9; n >= 1; --n) {
    p.setPen(QPen(QColor(96, 166, 244, alphas[n - 1]), 1));
    p.drawRoundedRect(frame.adjusted(-n, -n, n, n), n + 1, n + 1);
  }
  if (background.contains("棋盘")) {
    p.save();
    p.setClipRect(r);
    bool light = background.startsWith("亮");
    for (int y = r.top(); y <= r.bottom(); y += 10)
      for (int x = r.left(); x <= r.right(); x += 10)
        p.fillRect(x, y, 10, 10,
                   ((x - r.x()) / 10 + (y - r.y()) / 10) % 2
                       ? QColor(light ? "#ffffff" : "#303030")
                       : QColor(light ? "#dddddd" : "#222222"));
    p.restore();
  }
  p.drawImage(
      r, editing && annotation
             ? (annotation->draft ? annotation->render() : annotation->cached)
             : displayedImage());
  p.setPen(QPen(QColor(170, 207, 249), 1));
  p.drawRect(frame);
  if (!imageText || !imageText->isVisible()) paintEditing(p);
  if (selected) {
    p.setPen(QColor("#1688ef"));
    p.drawRect(r.adjusted(-1, -1, 0, 0));
  }
}
void PinWindow::mousePressEvent(QMouseEvent *e) {
  pointer = e->position().toPoint();
  if (e->button() == Qt::MiddleButton) {
    thumbnail = {};
    setScale(1);
    setAlpha(1);
    return;
  }
  if (e->button() != Qt::LeftButton)
    return;
  if (annotationPress(e))
    return;
  if (e->modifiers() & Qt::ControlModifier) {
    selected = !selected;
    update();
    return;
  }
  moving = true;
  dragOffset = e->globalPosition().toPoint() - pos();
  setCursor(Qt::SizeAllCursor);
}
void PinWindow::mouseMoveEvent(QMouseEvent *e) {
  pointer = e->position().toPoint();
  if (moving) {
    move(e->globalPosition().toPoint() - dragOffset);
    positionToolbar();
    return;
  }
  annotationMove(e);
}
void PinWindow::mouseReleaseEvent(QMouseEvent *e) {
  if (moving) {
    moving = false;
    setCursor(Qt::ArrowCursor);
    emit changed();
    return;
  }
  annotationRelease(e);
}
void PinWindow::mouseDoubleClickEvent(QMouseEvent *) {
  if (!editing)
    emit requested(this, "close");
}
void PinWindow::wheelEvent(QWheelEvent *e) {
  if (editing) {
    annotationWheel(e);
    return;
  }
  int direction = e->angleDelta().y() > 0 ? 1 : -1;
  if (e->modifiers() & Qt::ControlModifier)
    setAlpha(alpha + direction * .05);
  else {
    auto global = mapToGlobal(e->position().toPoint());
    auto imagePoint =
        QPointF(e->position().toPoint() - contentRect().topLeft()) / scale;
    setScale(scale * std::pow(1.1, direction));
    move(global - (imagePoint * scale).toPoint() - QPoint(Margin, Margin));
  }
}
void PinWindow::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_F4) {
    commitText();
    startImageText(displayedImage());
    return;
  }
  if (editing && annotationKey(e))
    return;
  if (e->key() == Qt::Key_Space) {
    editing ? endEditing() : startEditing();
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    if (editing)
      endEditing();
    else
      emit requested(this,
                     e->modifiers() & Qt::ShiftModifier ? "destroy" : "close");
    return;
  }
  if (e->matches(QKeySequence::Close)) {
    emit requested(this, "close");
    return;
  }
  if (e->matches(QKeySequence::Copy)) {
    emit requested(this, "copy");
    return;
  }
  if (e->matches(QKeySequence::Save)) {
    emit requested(this, "save");
    return;
  }
  if (!editing && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_5) {
    if (e->key() == Qt::Key_5)
      gray = !gray;
    else {
      auto transform = QTransform();
      if (e->key() == Qt::Key_1 || e->key() == Qt::Key_2)
        transform.rotate(e->key() == Qt::Key_1 ? -90 : 90);
      else
        transform.scale(e->key() == Qt::Key_3 ? -1 : 1,
                        e->key() == Qt::Key_4 ? -1 : 1);
      image = displayedImage().transformed(transform);
      gray = inverted = false;
      thumbnail = {};
      setScale(scale);
    }
    update();
    emit changed();
    return;
  }
  if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal ||
      e->key() == Qt::Key_Minus) {
    int direction = e->key() == Qt::Key_Minus ? -1 : 1;
    if (e->modifiers() & Qt::ControlModifier)
      setAlpha(alpha + direction * .05);
    else
      setScale(scale * std::pow(1.1, direction));
    return;
  }
  QWidget::keyPressEvent(e);
}
void PinWindow::contextMenuEvent(QContextMenuEvent *e) {
  if (editing) {
    showMore();
    return;
  }
  QMenu menu(this);
  menu.setObjectName("pinMenu");
  for (const auto &pair :
       QList<QPair<QString, QString>>{{"select_text", "文字识别"},
                                      {"edit", "标注"},
                                      {"copy", "复制"},
                                      {"save", "保存"},
                                      {"size", "输入缩放比例…"},
                                      {"thumbnail", "缩略图"},
                                      {"gray", "灰度"},
                                      {"invert", "反色"},
                                      {"through", "鼠标穿透"},
                                      {"topmost", "置顶开关"},
                                      {"solo", "Solo"},
                                      {"scan", "二维码 / 条形码"},
                                      {"close", "关闭"},
                                      {"destroy", "销毁"}}) {
    auto shortcut = QMap<QString, QString>{{"select_text", "F4"}, {"edit", "Space"}, {"copy", "Ctrl+C"},
        {"save", "Ctrl+S"}, {"gray", "5"}, {"close", "Esc"},
        {"destroy", "Shift+Esc"},
        {"through", store->config["hotkeys"].toObject()["through"].toString()}}[pair.first];
    menu.addAction(menuLabel(pair.second, shortcut), this,
                   [this, pair] { emit requested(this, pair.first); });
  }
  auto bg = menu.addMenu("背景");
  for (auto value : QStringList{"透明", "伪透明", "暗色棋盘", "亮色棋盘"})
    bg->addAction(value, this, [this, value] {
      background = value;
      update();
      emit changed();
    });
  menu.exec(e->globalPos());
}
void PinWindow::moveEvent(QMoveEvent *) { emit changed(); }
QJsonObject PinWindow::layout() const {
  return {
      {"id", id},
      {"x", x()},
      {"y", y()},
      {"scale", scale},
      {"frame_margin", Margin},
      {"alpha", alpha},
      {"state", state},
      {"group", group},
      {"topmost", bool(windowFlags() & Qt::WindowStaysOnTopHint)},
      {"gray", gray},
      {"inverted", inverted},
      {"background", background},
      {"thumbnail", thumbnail.isEmpty()
                        ? QJsonArray()
                        : QJsonArray{thumbnail.x(), thumbnail.y(),
                                     thumbnail.width(), thumbnail.height()}}};
}
PinManager::PinManager(Store *s, QObject *parent) : QObject(parent), store(s) {
  timer.setSingleShot(true);
  timer.setInterval(400);
  connect(&timer, &QTimer::timeout, this, &PinManager::persist);
}
PinManager::~PinManager() {
  timer.stop();
  persist();
  store->flushIo();
  for (auto pin : windows)
    if (pin)
      pin->close();
}
PinWindow *PinManager::create(const QImage &image, QRect source,
                              QJsonObject data) {
  if (image.isNull())
    return nullptr;
  auto pin = new PinWindow(image, store, source, data.value("id").toString());
  windows.append(pin);
  connect(pin, &PinWindow::changed, this, [this] { timer.start(); });
  connect(pin, &PinWindow::requested, this, &PinManager::handle);
  if (!data.isEmpty()) {
    pin->setScale(data["scale"].toDouble(1));
    pin->setAlpha(data["alpha"].toDouble(1));
    int offset = data["frame_margin"].toInt(2) - PinWindow::Margin;
    pin->move(data["x"].toInt(100) + offset, data["y"].toInt(100) + offset);
    pin->state = data["state"].toString("visible");
    pin->group = data["group"].toString("默认");
    pin->gray = data["gray"].toBool();
    pin->inverted = data["inverted"].toBool();
    pin->background = data["background"].toString("暗色棋盘");
    pin->setWindowFlag(Qt::WindowStaysOnTopHint, data["topmost"].toBool(true));
    auto r = data["thumbnail"].toArray();
    if (r.size() == 4)
      pin->thumbnail = {r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt()};
  }
  if (pin->state == "visible" && !hidden)
    pin->show();
  timer.start();
  return pin;
}
void PinManager::handle(PinWindow *pin, const QString &action) {
  if (!pin)
    return;
  if (action == "close") {
    pin->endEditing();
    pin->state = "closed";
    pin->hide();
    if (!closed.contains(pin))
      closed.append(pin);
    while (closed.size() > store->config["closed_pin_limit"].toInt(1))
      handle(closed.takeFirst(), "destroy");
  } else if (action == "destroy") {
    windows.removeAll(pin);
    closed.removeAll(pin);
    pin->close();
    auto file = store->path("pins/" + pin->id);
    submittedImages.remove(pin->id);
    // Ordered after pending saves: destroying a pin must not resurrect its file.
    store->submitIo([file] {
      for (auto extension : {QString(".png"), QString(".json")}) {
        auto name = file + extension;
        if (QFile::exists(name) && !QFile::remove(name))
          return QString("无法删除贴图缓存：%1").arg(name);
      }
      return QString();
    });
  } else if (action == "select_text" || action == "ocr") {
    pin->commitText();
    pin->startImageText(pin->displayedImage());
  } else if (action == "scan")
    showRecognition(pin->displayedImage(), true, pin, store);
  else if (action == "edit")
    pin->startEditing();
  else if (action == "through")
    pin->setThrough(!pin->through);
  else if (action == "gray") {
    pin->gray = !pin->gray;
    pin->update();
  } else if (action == "invert") {
    pin->inverted = !pin->inverted;
    pin->update();
  } else if (action == "thumbnail") {
    if (pin->thumbnail.isEmpty()) {
      pin->thumbnail = pin->image.rect();
      pin->setScale(qMin(
          1.0, qMin(200.0 / pin->image.width(), 150.0 / pin->image.height())));
    } else {
      pin->thumbnail = {};
      pin->setScale(1);
    }
  } else if (action == "size") {
    bool ok;
    auto value = QInputDialog::getDouble(pin, "贴图大小", "缩放百分比",
                                         pin->scale * 100, 5, 500, 1, &ok);
    if (ok)
      pin->setScale(value / 100);
  } else if (action == "topmost") {
    pin->setWindowFlag(Qt::WindowStaysOnTopHint,
                       !(pin->windowFlags() & Qt::WindowStaysOnTopHint));
    pin->show();
  } else if (action == "solo") {
    for (auto other : windows)
      if (other && other != pin && other->state == "visible")
        other->setVisible(!other->isVisible());
  } else if (output)
    output(pin, action);
  timer.start();
}
void PinManager::toggleAll() {
  hidden = !hidden;
  for (auto pin : windows)
    if (pin && pin->state == "visible")
      pin->setVisible(!hidden);
}
void PinManager::restoreClosed() {
  while (!closed.isEmpty()) {
    auto pin = closed.takeLast();
    if (pin) {
      pin->state = "visible";
      pin->show();
      pin->raise();
      timer.start();
      return;
    }
  }
}
void PinManager::restore() {
  for (const auto &value : store->workspace()) {
    auto data = value.toObject();
    auto id = data["id"].toString();
    QImage image(store->path("pins/" + id + ".png"));
    auto marks =
        Store::readJson(store->path("pins/" + id + ".json"), QJsonArray())
            .toArray();
    if (!marks.isEmpty()) {
      Annotation doc(image);
      for (auto mark : marks)
        doc.marks.append(Mark::fromJson(mark.toObject()));
      image = doc.render();
    }
    auto pin = create(image, {}, data);
    if (pin && marks.isEmpty()) submittedImages[id] = image.cacheKey();
  }
}
void PinManager::persist() {
  timer.stop();
  QJsonArray data;
  QList<QPair<QString, QImage>> changed;
  QHash<QString, qint64> keys;
  for (auto pin : windows) {
    if (!pin || pin->state == "closed") continue;
    auto image = pin->editing && pin->annotation ? pin->annotation->render() : pin->image;
    if (submittedImages.value(pin->id) != image.cacheKey()) {
      changed.append({store->path("pins/" + pin->id), image});
      keys[pin->id] = image.cacheKey();
      submittedImages[pin->id] = image.cacheKey();
    }
    data.append(pin->layout());
  }
  if (changed.isEmpty() && data == submittedLayout) return;
  submittedLayout = data;
  auto file = store->path("workspace.json");
  QPointer<PinManager> self(this);
  store->submitIo([changed, data, file] {
    for (const auto &entry : changed) {
      if (!Store::writeImage(entry.first + ".png", entry.second) ||
          !Store::writeJson(entry.first + ".json", QJsonArray()))
        return QString("无法保存贴图缓存，请检查数据目录：%1").arg(entry.first);
    }
    // Only publish layouts whose images are already durable.
    for (const auto &value : data) {
      auto id = value.toObject()["id"].toString();
      if (!QFile::exists(QFileInfo(file).absolutePath() + "/pins/" + id + ".png"))
        return QString("贴图图片尚未保存，已保留上一次布局。");
    }
    if (!Store::writeJson(file, data)) return QString("无法保存贴图布局：%1").arg(file);
    return QString();
  }, [self, keys, data](const QString &error) {
    if (!self || error.isEmpty()) return;
    for (auto key = keys.begin(); key != keys.end(); ++key)
      if (self->submittedImages.value(key.key()) == key.value())
        self->submittedImages.remove(key.key());
    if (self->submittedLayout == data) self->submittedLayout = {};
  });
}
void PinManager::clearThrough() {
  for (auto pin : windows)
    if (pin && pin->through)
      pin->setThrough(false);
}
void PinManager::toggleThrough() {
  auto cursor = QCursor::pos();
  for (auto pin : windows)
    if (pin && pin->isVisible() && pin->geometry().contains(cursor)) {
      pin->setThrough(!pin->through);
      return;
    }
  clearThrough();
}
