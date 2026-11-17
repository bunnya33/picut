#include "capture.h"
#include "recording_capture.h"
#include "element_detector.h"
#include "native_windows.h"
#include "compact_ui.h"
#include "recording_settings.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonArray>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <cstring>

static void captureCursor(DesktopSnapshot &snapshot) {
  CURSORINFO cursor{sizeof(CURSORINFO)};
  if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING))
    return;
  ICONINFO icon{};
  if (!GetIconInfo(cursor.hCursor, &icon))
    return;
  BITMAP bitmap{};
  GetObjectW(icon.hbmColor ? icon.hbmColor : icon.hbmMask, sizeof(bitmap),
             &bitmap);
  int width = bitmap.bmWidth,
      height = icon.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  void *bits = nullptr;
  HBITMAP dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (dib && width > 0 && height > 0) {
    auto previous = SelectObject(dc, dib);
    std::memset(bits, 0, size_t(width) * height * 4);
    DrawIconEx(dc, 0, 0, cursor.hCursor, width, height, 0, nullptr, DI_NORMAL);
    QImage black =
        QImage(static_cast<uchar *>(bits), width, height, QImage::Format_RGB32)
            .copy();
    std::memset(bits, 255, size_t(width) * height * 4);
    DrawIconEx(dc, 0, 0, cursor.hCursor, width, height, 0, nullptr, DI_NORMAL);
    QImage white(static_cast<uchar *>(bits), width, height,
                 QImage::Format_RGB32);
    snapshot.cursorImage =
        QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        QRgb b = black.pixel(x, y), w = white.pixel(x, y);
        int alpha = qBound(0, 255 - (qRed(w) - qRed(b)), 255);
        snapshot.cursorImage.setPixel(
            x, y, qRgba(qRed(b), qGreen(b), qBlue(b), alpha));
      }
    QPoint physical(cursor.ptScreenPos.x, cursor.ptScreenPos.y);
    for (auto screen : QGuiApplication::screens()) {
      auto area = physicalRect(screen->geometry());
      if (!area.contains(physical))
        continue;
      double scale = screen->devicePixelRatio();
      QPointF point = screen->geometry().topLeft() +
                      QPointF(physical - area.topLeft() -
                              QPoint(icon.xHotspot, icon.yHotspot)) /
                          scale;
      snapshot.cursorRect =
          QRectF(point, QSizeF(width / scale, height / scale));
      break;
    }
    SelectObject(dc, previous);
  }
  if (dib)
    DeleteObject(dib);
  if (dc)
    DeleteDC(dc);
  if (icon.hbmColor)
    DeleteObject(icon.hbmColor);
  if (icon.hbmMask)
    DeleteObject(icon.hbmMask);
}

static QImage captureScreen(const QRect &area) {
  HDC source = GetDC(nullptr), target = CreateCompatibleDC(source);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = area.width();
  info.bmiHeader.biHeight = -area.height();
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  void *pixels = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(source, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  QImage image;
  if (source && target && bitmap) {
    auto previous = SelectObject(target, bitmap);
    if (BitBlt(target, 0, 0, area.width(), area.height(), source, area.x(),
               area.y(), SRCCOPY | CAPTUREBLT) ||
        BitBlt(target, 0, 0, area.width(), area.height(), source, area.x(),
               area.y(), SRCCOPY))
      image = QImage(static_cast<uchar *>(pixels), area.width(), area.height(),
                     QImage::Format_RGB32)
                  .copy();
    SelectObject(target, previous);
  }
  if (bitmap)
    DeleteObject(bitmap);
  if (target)
    DeleteDC(target);
  if (source)
    ReleaseDC(nullptr, source);
  return image;
}

QImage captureDesktopRegion(QRect global) {
  DesktopSnapshot snapshot;
  for (auto screen : QGuiApplication::screens()) {
    auto part = global.intersected(screen->geometry());
    if (part.isEmpty()) continue;
    auto image = captureScreen(physicalRect(part));
    if (image.isNull()) return {};
    snapshot.frames.append({part, image});
    snapshot.bounds = snapshot.bounds.united(part);
  }
  return snapshot.crop(global);
}

DesktopSnapshot DesktopSnapshot::capture() {
  QElapsedTimer timer;
  timer.start();
  DesktopSnapshot desktop;
  struct WindowContext {
    QList<QRect> *windows;
    QList<QPair<QRect, QRect>> screens;
  } context{&desktop.windows, {}};
  desktop.foreground = reinterpret_cast<quintptr>(GetForegroundWindow());
  bool physicalCoordinates = true;
  for (auto screen : QGuiApplication::screens()) {
    auto physical = physicalRect(screen->geometry());
    context.screens.append({screen->geometry(), physical});
    desktop.bounds = desktop.bounds.united(screen->geometry());
    physicalCoordinates = physicalCoordinates && physical == screen->geometry();
  }
  // One native capture avoids paying the compositor cost once per monitor.
  auto mappings =
      physicalCoordinates
          ? QList<QPair<QRect, QRect>>{{desktop.bounds, desktop.bounds}}
          : context.screens;
  for (const auto &mapping : mappings) {
    auto image = captureScreen(mapping.second);
    image.setDevicePixelRatio(1);
    if (image.isNull())
      throw std::runtime_error("无法读取屏幕，请检查当前桌面会话。");
    desktop.frames.append({mapping.first, image});
  }
  captureCursor(desktop);
  desktop.timings["screen_and_cursor_ms"] = timer.nsecsElapsed() / 1000000.0;
  timer.restart();
  EnumWindows(
      [](HWND hwnd, LPARAM parameter) -> BOOL {
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd))
          return TRUE;
        DWORD process = 0;
        GetWindowThreadProcessId(hwnd, &process);
        if (process == GetCurrentProcessId())
          return TRUE;
        RECT r{};
        GetWindowRect(hwnd, &r);
        if (r.right - r.left < 6 || r.bottom - r.top < 6)
          return TRUE;
        auto context = reinterpret_cast<WindowContext *>(parameter);
        QPoint center((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        for (const auto &mapping : context->screens) {
          const auto &physical = mapping.second, &logical = mapping.first;
          if (!physical.contains(center))
            continue;
          double sx = double(logical.width()) / physical.width(),
                 sy = double(logical.height()) / physical.height();
          context->windows->append(
              QRect(logical.x() + qRound((r.left - physical.x()) * sx),
                    logical.y() + qRound((r.top - physical.y()) * sy),
                    qRound((r.right - r.left) * sx),
                    qRound((r.bottom - r.top) * sy)));
          break;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&context));
  desktop.timings["window_detection_ms"] = timer.nsecsElapsed() / 1000000.0;
  return desktop;
}
QImage DesktopSnapshot::crop(QRect region, bool cursor) const {
  region = region.intersected(bounds);
  if (region.isEmpty())
    return {};
  double scale = 1;
  for (const auto &frame : frames)
    if (frame.rect.intersects(region))
      scale = qMax(scale, double(frame.image.width()) / frame.rect.width());
  QImage image(qRound(region.width() * scale), qRound(region.height() * scale),
               QImage::Format_ARGB32);
  image.fill(Qt::black);
  QPainter p(&image);
  for (const auto &frame : frames) {
    auto overlap = frame.rect.intersected(region);
    if (overlap.isEmpty())
      continue;
    double sx = double(frame.image.width()) / frame.rect.width(),
           sy = double(frame.image.height()) / frame.rect.height();
    p.drawImage(QRectF((overlap.x() - region.x()) * scale,
                       (overlap.y() - region.y()) * scale,
                       overlap.width() * scale, overlap.height() * scale),
                frame.image,
                QRectF((overlap.x() - frame.rect.x()) * sx,
                       (overlap.y() - frame.rect.y()) * sy,
                       overlap.width() * sx, overlap.height() * sy));
  }
  if (cursor && !cursorImage.isNull())
    p.drawImage(QRectF((cursorRect.x() - region.x()) * scale,
                       (cursorRect.y() - region.y()) * scale,
                       cursorRect.width() * scale, cursorRect.height() * scale),
                cursorImage);
  return image;
}
QColor DesktopSnapshot::color(QPoint global) const {
  for (const auto &frame : frames)
    if (frame.rect.contains(global)) {
      auto p = global - frame.rect.topLeft();
      return frame.image.pixelColor(
          qBound(
              0,
              qRound(p.x() * double(frame.image.width()) / frame.rect.width()),
              frame.image.width() - 1),
          qBound(0,
                 qRound(p.y() * double(frame.image.height()) /
                        frame.rect.height()),
                 frame.image.height() - 1));
    }
  return Qt::black;
}
CaptureOverlay::CaptureOverlay(Store *s, bool record,
                               std::optional<DesktopSnapshot> snapshot)
    : EditingSurface(s, nullptr,
                     Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                         Qt::Tool),
      desktop(snapshot ? std::move(*snapshot) : DesktopSnapshot::capture()),
      recording(record) {
  sourceWindow = desktop.foreground;
  setProperty("supportsScrolling", true);
  setProperty("supportsRecording", true);
  setAttribute(Qt::WA_DeleteOnClose);
  setGeometry(desktop.bounds);
  setCursor(Qt::CrossCursor);
  pointer = mapFromGlobal(QCursor::pos());
  includeCursor = store && store->config["capture_cursor"].toBool();
  rgb = !store || store->config["capture_color_format"].toString("RGB")!="HEX";
  {
    auto bar = new QFrame(this);
    bar->setStyleSheet(compactToolbarStyle());
    auto row = new QHBoxLayout(bar);
    row->setContentsMargins(6,6,6,6); row->setSpacing(3);
    for (auto name:{QString("back"),QString("area"),QString("window"),QString("settings"),QString("record")}) {
      auto button=new QToolButton; button->setObjectName("record_"+name);
      button->setFixedSize(32,32); button->setIconSize({16,16}); button->setFocusPolicy(Qt::NoFocus);
      button->setCursor(Qt::PointingHandCursor);
      button->setIcon(name=="area"?toolIcon("rect"):recordingIcon(name));
      button->setToolTip(name=="settings"?"录屏设置":name=="record"?"开始录制 · Enter":
          name=="area"?"区域录屏":name=="window"?"窗口录屏：点击要录制的窗口":"返回截图 · Esc");
      if (name=="area" || name=="window") { button->setCheckable(true); button->setChecked(name=="area"); }
      row->addWidget(button);
      connect(button,&QToolButton::clicked,this,[this,name] {
        if (name=="area" || name=="window") {
          targetRecordingWindow=0; pickingRecordingWindow=name=="window";
          findChild<QToolButton *>("record_area")->setChecked(!pickingRecordingWindow);
          findChild<QToolButton *>("record_window")->setChecked(pickingRecordingWindow);
          findChild<QToolButton *>("record_record")->setEnabled(!pickingRecordingWindow);
          updateRecordingSummary(); setCursor(Qt::CrossCursor); setFocus(); update();
        } else if (name=="settings") {
          RecordingSettingsDialog dialog(store,globalSelection(),false,this);
          dialog.exec(); updateSelectionToolbar(); activateWindow(); setFocus(); update();
        } else complete(name=="record"?"record":"back");
      });
    }
    bar->setObjectName("recordToolbar");
    bar->hide();
    auto summary=new QLabel(this); summary->setObjectName("recordSummary");
    summary->setAttribute(Qt::WA_TransparentForMouseEvents);
    summary->setStyleSheet("background:rgba(0,0,0,175);color:white;padding:5px 8px;"
                          "font-family:'Microsoft YaHei UI';font-size:12px;border:0;");
    summary->setWordWrap(true); summary->hide();
  }
}
void CaptureOverlay::setRecordingMode(bool enabled) {
  if (recording==enabled) return;
  commitText();
  if (annotation) {
    if (annotation->draft) {
      auto mark=*annotation->draft; annotation->draft.reset(); annotation->add(mark);
    }
    annotation->confirm(); annotation->savePreferences(store);
  }
  dragOriginal.reset(); dragHandle.clear(); suppressDraw=false;
  recording=enabled;
  if (!enabled) {
    targetRecordingWindow=0; pickingRecordingWindow=false;
    findChild<QToolButton *>("record_area")->setChecked(true);
    findChild<QToolButton *>("record_window")->setChecked(false);
    findChild<QToolButton *>("record_record")->setEnabled(true);
  }
  editing=!recording && annotation && annotation->tool!="select";
  if (toolbar) toolbar->hide();
  if (auto bar=findChild<QFrame *>("recordToolbar")) bar->hide();
  if (auto summary=findChild<QLabel *>("recordSummary")) summary->hide();
  if (imageText) imageText->hide();
  if (imageTranslation) imageTranslation->hide();
  updateSelectionToolbar(!selection.isEmpty());
  setCursor(recording?Qt::CrossCursor:Qt::ArrowCursor);
  activateWindow(); setFocus(); update();
}
bool CaptureOverlay::bindRecordingWindow(quintptr window) {
  const auto info=recordingWindowInfo(window);
  if (!info.valid || info.minimized || info.logical.isEmpty()) return false;
  targetRecordingWindow=window; pickingRecordingWindow=false;
  selectRegion(info.logical.translated(-pos()));
  findChild<QToolButton *>("record_area")->setChecked(false);
  findChild<QToolButton *>("record_window")->setChecked(true);
  findChild<QToolButton *>("record_record")->setEnabled(true);
  updateRecordingSummary(); return true;
}
void CaptureOverlay::selectRegion(QRect region) {
  selection = region.intersected(rect());
  rebuildAnnotation();
  updateSelectionToolbar(true);
  update();
}
CaptureOverlay::~CaptureOverlay() {
  if (scrollCapture) { scrollCapture->disconnect(this); delete scrollCapture; }
}
void CaptureOverlay::updateSelectionToolbar(bool show) {
  if (!recording) {
    if (show)
      showToolbar();
    else
      positionToolbar();
  } else if (auto bar = findChild<QFrame *>("recordToolbar")) {
    positionBar(bar);
    if (show)
      bar->show();
    updateRecordingSummary();
  }
}
void CaptureOverlay::updateRecordingSummary() {
  auto summary=findChild<QLabel *>("recordSummary");
  if (!summary) return;
  if (selection.isEmpty()) { summary->hide(); return; }
  auto screen=QGuiApplication::screenAt(globalSelection().center());
  if (!screen) screen=QGuiApplication::primaryScreen();
  auto available=QRect(mapFromGlobal(screen->availableGeometry().topLeft()),screen->availableGeometry().size()).intersected(rect()).adjusted(4,4,-4,-4);
  summary->setText((pickingRecordingWindow?QString("点击要录制的窗口 · Esc 取消选择　"):
      targetRecordingWindow?QString("窗口：%1　").arg(recordingWindowInfo(targetRecordingWindow).title):QString())+recordingSummary(store->config));
  summary->setFixedWidth(qMin(available.width(),summary->fontMetrics().horizontalAdvance(summary->text())+20));
  summary->adjustSize();
  int x=qBound(available.left(),selection.left(),qMax(available.left(),available.right()-summary->width()+1));
  int y=selection.bottom()+7;
  if (auto bar=findChild<QFrame *>("recordToolbar");bar && bar->isVisible() &&
      QRect(x,y,summary->width(),summary->height()).intersects(bar->geometry())) y=bar->geometry().bottom()+5;
  if (y+summary->height()-1>available.bottom()) y=selection.bottom()-summary->height()-6;
  y=qBound(available.top(),y,qMax(available.top(),available.bottom()-summary->height()+1));
  summary->move(x,y); summary->show(); summary->raise();
}
void CaptureOverlay::rebuildAnnotation() {
  if (selection.width() < 2 || selection.height() < 2)
    return;
  auto image = desktop.crop(globalSelection(), includeCursor);
  if (!annotation)
    annotation = std::make_unique<Annotation>(image, store);
  else {
    auto old = documentRect;
    double sx = double(annotation->original.width()) / qMax(1, old.width()),
           sy = double(annotation->original.height()) / qMax(1, old.height()),
           nx = double(image.width()) / selection.width(),
           ny = double(image.height()) / selection.height();
    auto delta = old.size() != selection.size()
                     ? old.topLeft() - selection.topLeft()
                     : QPoint();
    auto transform = [&](QList<Mark> &marks) {
      for (auto &mark : marks)
        for (auto &point : mark.points)
          point = {(point.x() / sx + delta.x()) * nx,
                   (point.y() / sy + delta.y()) * ny};
    };
    transform(annotation->marks);
    for (auto &state : annotation->undoStates)
      transform(state);
    for (auto &state : annotation->redoStates)
      transform(state);
    annotation->original = image;
    annotation->refresh();
  }
  documentRect = selection;
  if (toolbar)
    toolbar->sync(*annotation);
}
QString CaptureOverlay::selectionHandle(QPoint point) const {
  if (selection.isEmpty())
    return {};
  auto r = selection;
  QMap<QString, QPoint> handles{{"nw", r.topLeft()},
                                {"ne", r.topRight()},
                                {"sw", r.bottomLeft()},
                                {"se", r.bottomRight()},
                                {"n", {r.center().x(), r.top()}},
                                {"s", {r.center().x(), r.bottom()}},
                                {"w", {r.left(), r.center().y()}},
                                {"e", {r.right(), r.center().y()}}};
  for (auto i = handles.begin(); i != handles.end(); ++i)
    if ((i.value() - point).manhattanLength() <= 8)
      return i.key();
  return {};
}
void CaptureOverlay::paintEvent(QPaintEvent *) {
  QPainter p(this);
  for (const auto &frame : desktop.frames)
    p.drawImage(frame.rect.translated(-pos()), frame.image);
  if (includeCursor)
    p.drawImage(desktop.cursorRect.translated(-pos()), desktop.cursorImage);
  p.fillRect(rect(), QColor(0, 0, 0, 110));
  auto r = selection.isEmpty() || pickingRecordingWindow ? hover : selection;
  QImage preview;
  if (!r.isEmpty()) {
    p.save();
    p.setClipRect(r);
    for (const auto &frame : desktop.frames)
      p.drawImage(frame.rect.translated(-pos()), frame.image);
    if (includeCursor)
      p.drawImage(desktop.cursorRect.translated(-pos()), desktop.cursorImage);
    p.restore();
    if (annotation && !pickingRecordingWindow) {
      preview = annotation->draft ? annotation->render() : annotation->cached;
      p.drawImage(selection, preview);
    }
    p.setPen(QPen(QColor("#1688ef"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    if (!selection.isEmpty() && !pickingRecordingWindow && !targetRecordingWindow) {
      p.setBrush(Qt::white);
      for (auto point : QList<QPoint>{r.topLeft(),
                                      r.topRight(),
                                      r.bottomLeft(),
                                      r.bottomRight(),
                                      {r.center().x(), r.top()},
                                      {r.center().x(), r.bottom()},
                                      {r.left(), r.center().y()},
                                      {r.right(), r.center().y()}})
        p.drawEllipse(point, 3, 3);
    }
    QRect caption(r.x(), r.y() - 26, 130, 23);
    if (caption.y() < 0)
      caption.moveTop(r.y() + 4);
    p.fillRect(caption, QColor(0, 0, 0, 180));
    p.setPen(Qt::white);
    p.drawText(
        caption, Qt::AlignCenter,
        QString::number(annotation ? annotation->original.width() : r.width()) +
            " × " +
            QString::number(annotation ? annotation->original.height()
                                       : r.height()));
  }
  const bool selectingText = imageText && imageText->isVisible();
  if (!selectingText && !recording) paintEditing(p);
  bool brush = annotation &&
               QStringList{"pen", "line", "arrow", "marker", "eraser"}.contains(
                   annotation->tool) &&
               selection.contains(pointer);
  if (!recording && !selectingText && (alt || (!brush && (!annotation || (!annotation->draft && !dragOriginal)))))
    paintMagnifier(p, preview);
  p.setPen(Qt::white);
  if (!recording || selection.isEmpty()) p.drawText(
      QRect(15, height() - 46, 560, 36), Qt::AlignLeft | Qt::AlignVCenter,
      recording ? "拖动框选录屏区域 · Esc 取消"
                : "Ctrl+1～8 标注工具 · Enter 复制 · F3 原位置贴图 · Esc 退出");
}
void CaptureOverlay::paintMagnifier(QPainter &p, const QImage &preview) {
  if (store && !store->config["magnifier_enabled"].toBool(true) && !alt)
    return;
  if (childAt(pointer) || !rect().contains(pointer))
    return;
  int pixels =
      store ? qBound(3, store->config["magnifier_pixels"].toInt(11), 31) : 11;
  if (pixels % 2 == 0)
    ++pixels;
  auto global = pointer + pos();
  auto color = desktop.color(global);
  QImage sample;
  if (!preview.isNull() && selection.contains(pointer)) {
    auto location = imagePoint(pointer).toPoint();
    sample = preview.copy(QRect(location - QPoint(pixels / 2, pixels / 2),
                                QSize(pixels, pixels)));
    color = preview.pixelColor(qBound(0, location.x(), preview.width() - 1),
                               qBound(0, location.y(), preview.height() - 1));
  } else {
    for (const auto &frame : desktop.frames)
      if (frame.rect.contains(global)) {
        auto local = global - frame.rect.topLeft();
        double scale = double(frame.image.width()) / frame.rect.width();
        sample = frame.image.copy(
            QRect(QPoint(qRound(local.x() * scale), qRound(local.y() * scale)) -
                      QPoint(pixels / 2, pixels / 2),
                  QSize(pixels, pixels)));
        break;
      }
  }
  if (sample.isNull())
    return;
  QRect box(pointer + QPoint(24, 24), QSize(210, 254));
  if (box.right() > width() - 4)
    box.moveRight(pointer.x() - 24);
  if (box.bottom() > height() - 4)
    box.moveBottom(pointer.y() - 24);
  box.moveLeft(qMax(0, box.x()));
  box.moveTop(qMax(0, box.y()));
  if (toolbar && toolbar->isVisible() && box.intersects(toolbar->geometry()))
    box.moveTop(qMax(0, toolbar->y() - box.height() - 6));
  p.save();
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  QRect imageBox(box.topLeft(), QSize(210, 154));
  p.drawImage(imageBox, sample);
  double cw = 210.0 / pixels, ch = 154.0 / pixels;
  QRectF pixel(imageBox.x() + pixels / 2 * cw, imageBox.y() + pixels / 2 * ch,
               cw, ch);
  auto cross = QColor(80, 165, 218, 115);
  p.fillRect(QRectF(imageBox.x(), pixel.y(), 210, ch), cross);
  p.fillRect(QRectF(pixel.x(), imageBox.y(), cw, 154), cross);
  p.fillRect(pixel, color);
  p.setPen(Qt::black);
  p.setBrush(Qt::NoBrush);
  p.drawRect(pixel.adjusted(.5, .5, -.5, -.5));
  QRect info(box.x(), box.y() + 154, 210, 100);
  p.fillRect(info, QColor(0, 0, 0, 185));
  QFont font("Consolas");
  font.setPixelSize(15);
  p.setFont(font);
  p.setPen(Qt::white);
  p.drawText(QRect(info.x(), info.y() + 3, 210, 22), Qt::AlignCenter,
             QString("(%1, %2)").arg(global.x()).arg(global.y()));
  auto value = rgb ? QString("%1, %2, %3")
                         .arg(color.red())
                         .arg(color.green())
                         .arg(color.blue())
                   : color.name().toUpper();
  p.drawText(QRect(info.x() + 24, info.y() + 26, 180, 23), Qt::AlignCenter,
             value);
  p.fillRect(QRect(info.x() + 12, info.y() + 30, 14, 14), color);
  font.setFamily("Microsoft YaHei UI");
  font.setPixelSize(12);
  p.setFont(font);
  p.drawText(QRect(info.x(), info.y() + 53, 210, 20), Qt::AlignCenter,
             "按 C 复制颜色值");
  p.drawText(QRect(info.x(), info.y() + 76, 210, 20), Qt::AlignCenter,
             "按 Shift 切换 RGB/HEX");
  p.setPen(QColor(0, 0, 0, 200));
  p.drawRect(box.adjusted(0, 0, -1, -1));
  p.restore();
}
void CaptureOverlay::mousePressEvent(QMouseEvent *e) {
  pointer = e->position().toPoint();
  if (recording && pickingRecordingWindow && e->button()==Qt::LeftButton) {
    bindRecordingWindow(recordingWindowAt(pointer+pos())); return;
  }
  if (recording && targetRecordingWindow && e->button()==Qt::LeftButton) return;
  if (e->button() == Qt::MiddleButton) {
    complete(recording ? "record" : "pin");
    return;
  }
  if (e->button() == Qt::RightButton) {
    if (annotation && annotation->selected >= 0) {
      annotation->confirm();
      update();
    } else
      complete("cancel");
    return;
  }
  if (e->button() != Qt::LeftButton)
    return;
  setFocus();
  if (!recording && editing && !editTarget(pointer).isEmpty() && annotationPress(e))
    return;
  auto handle = selectionHandle(pointer);
  if (!handle.isEmpty() ||
      (!selection.isEmpty() && selection.contains(pointer) &&
       (recording || !annotation || annotation->tool == "select"))) {
    commitText();
    dragging = true;
    selectionDrag = handle.isEmpty() ? "move" : handle;
    origin = pointer;
    previous = selection;
    if (annotation)
      annotation->confirm();
    return;
  }
  if (annotation && editing && !selection.contains(pointer)) {
    commitText();
    annotation->confirm();
    update();
    return;
  }
  if (!recording && annotationPress(e))
    return;
  if (annotation)
    annotation->confirm();
  dragging = true;
  selectionDrag = "new";
  origin = pointer;
  previous = selection;
  selection = QRect(pointer, QSize(1, 1));
  if (toolbar)
    toolbar->hide();
  if (auto bar = findChild<QFrame *>("recordToolbar"))
    bar->hide();
  if (auto summary=findChild<QLabel *>("recordSummary")) summary->hide();
  update();
}
void CaptureOverlay::mouseMoveEvent(QMouseEvent *e) {
  pointer = e->position().toPoint();
  if (recording && pickingRecordingWindow) {
    const auto info=recordingWindowInfo(recordingWindowAt(pointer+pos()));
    hover=info.logical.translated(-pos()); setCursor(Qt::CrossCursor); update(); return;
  }
  if (dragging) {
    auto delta = pointer - origin;
    if (space)
      selection = spaceRect.translated(pointer - spaceOrigin);
    else if (selectionDrag == "new")
      selection = QRect(origin, pointer).normalized();
    else if (selectionDrag == "move")
      selection = previous.translated(delta);
    else {
      selection = previous;
      if (selectionDrag.contains('w'))
        selection.setLeft(
            qMin(selection.right() - 2, previous.left() + delta.x()));
      if (selectionDrag.contains('e'))
        selection.setRight(
            qMax(selection.left() + 2, previous.right() + delta.x()));
      if (selectionDrag.contains('n'))
        selection.setTop(
            qMin(selection.bottom() - 2, previous.top() + delta.y()));
      if (selectionDrag.contains('s'))
        selection.setBottom(
            qMax(selection.top() + 2, previous.bottom() + delta.y()));
    }
    selection = selection.intersected(rect());
    if (selectionDrag != "new") {
      rebuildAnnotation();
      updateSelectionToolbar();
    }
    update();
    return;
  }
  if (!recording && annotationMove(e))
    return;
  if (!recording && editing && !editTarget(pointer).isEmpty())
    return;
  if (selection.isEmpty()) {
    auto global = pointer + pos();
    hover = {};
    for (auto window : desktop.windows)
      if (window.contains(global)) {
        hover = window.intersected(geometry()).translated(-pos());
        break;
      }
    setCursor(Qt::CrossCursor);
  } else {
    auto handle = selectionHandle(pointer);
    if (!handle.isEmpty()) {
      setCursor(handle == "n" || handle == "s"     ? Qt::SizeVerCursor
                : handle == "w" || handle == "e"   ? Qt::SizeHorCursor
                : handle == "nw" || handle == "se" ? Qt::SizeFDiagCursor
                                                   : Qt::SizeBDiagCursor);
    } else if (!editing)
      setCursor(selection.contains(pointer) ? Qt::SizeAllCursor
                                            : Qt::CrossCursor);
  }
  update();
}
void CaptureOverlay::mouseReleaseEvent(QMouseEvent *e) {
  if (dragging && e->button() == Qt::LeftButton) {
    if (pointer != e->position().toPoint())
      mouseMoveEvent(e);
    dragging = false;
    if (selectionDrag == "new" && selection.width() < 3 &&
        selection.height() < 3)
      selection = hover;
    if (selection.width() > 1 && selection.height() > 1) {
      rebuildAnnotation();
      updateSelectionToolbar(true);
    }
    update();
    return;
  }
  if (!recording) annotationRelease(e);
}
void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *e) {
  if (selection.contains(e->position().toPoint()) && !editing)
    complete(recording ? "record" : "copy");
}
void CaptureOverlay::wheelEvent(QWheelEvent *e) {
  if (!recording && annotation && editing)
    annotationWheel(e);
}
void CaptureOverlay::keyPressEvent(QKeyEvent *e) {
  if (recording && pickingRecordingWindow && e->key()==Qt::Key_Escape) {
    pickingRecordingWindow=false;
    findChild<QToolButton *>("record_area")->setChecked(true);
    findChild<QToolButton *>("record_window")->setChecked(false);
    findChild<QToolButton *>("record_record")->setEnabled(true);
    updateRecordingSummary(); update(); return;
  }
  if (e->key() == Qt::Key_QuoteLeft || e->key() == Qt::Key_Exclam) {
    includeCursor = !includeCursor;
    rebuildAnnotation();
    update();
    return;
  }
  if (e->key() == Qt::Key_Alt) {
    alt = true;
    update();
    return;
  }
  if (e->key() == Qt::Key_Shift) {
    if (!recording && !e->isAutoRepeat()) {
      rgb = !rgb;
      if (store) {
        store->config["capture_color_format"]=rgb?"RGB":"HEX";
        if (!store->saveConfig()) qWarning("Could not save capture color format.");
      }
    }
    update();
    return;
  }
  if (e->key() == Qt::Key_Space && dragging) {
    space = true;
    spaceRect = selection;
    spaceOrigin = pointer;
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    complete("cancel");
    return;
  }
  if (!recording &&
      (e->key() == Qt::Key_F3 ||
       (e->key() == Qt::Key_T && e->modifiers() & Qt::ControlModifier))) {
    complete("pin");
    return;
  }
  if (!recording && e->matches(QKeySequence::Save)) {
    complete(e->modifiers() & Qt::ShiftModifier ? "quick_save" : "save");
    return;
  }
  if (!recording && e->key() == Qt::Key_C && !(e->modifiers() & Qt::ControlModifier)) {
    auto color = desktop.color(pointer + pos());
    if (annotation && selection.contains(pointer)) {
      auto point = imagePoint(pointer).toPoint();
      color = annotation->cached.pixelColor(
          qBound(0, point.x(), annotation->cached.width() - 1),
          qBound(0, point.y(), annotation->cached.height() - 1));
    }
    QApplication::clipboard()->setText(rgb ? QString("%1, %2, %3")
                                                 .arg(color.red())
                                                 .arg(color.green())
                                                 .arg(color.blue())
                                           : color.name().toUpper());
    return;
  }
  if (e->key() == Qt::Key_A && e->modifiers() & Qt::ControlModifier) {
    auto screen = QGuiApplication::screenAt(pointer + pos());
    selectRegion(screenSelectCount++ % 2 == 0 && screen
                     ? screen->geometry().translated(-pos())
                     : rect());
    return;
  }
  if (e->key() == Qt::Key_R && !recording) {
    auto r = store->config["last_capture_rect"].toArray();
    if (r.size() == 4)
      selectRegion(QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt())
                       .translated(-pos()));
    return;
  }
  if (e->key() == Qt::Key_F5) {
    hide();
    desktop = DesktopSnapshot::capture();
    show();
    rebuildAnnotation();
    update();
    return;
  }
  if (!recording && annotationKey(e))
    return;
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter ||
      e->matches(QKeySequence::Copy)) {
    complete(recording ? "record" : "copy");
    return;
  }
  if (!selection.isEmpty() && e->key() >= Qt::Key_Left &&
      e->key() <= Qt::Key_Down) {
    QPoint delta = e->key() == Qt::Key_Left    ? QPoint(-1, 0)
                   : e->key() == Qt::Key_Right ? QPoint(1, 0)
                   : e->key() == Qt::Key_Up    ? QPoint(0, -1)
                                               : QPoint(0, 1);
    if (e->modifiers() & Qt::ControlModifier)
      selection.setSize(selection.size() +
                        QSize(qAbs(delta.x()), qAbs(delta.y())));
    else if (e->modifiers() & Qt::ShiftModifier)
      selection.setSize(selection.size() -
                        QSize(qAbs(delta.x()), qAbs(delta.y())));
    else
      selection.translate(delta);
    selection = selection.intersected(rect());
    rebuildAnnotation();
    updateSelectionToolbar();
    update();
    return;
  }
  QWidget::keyPressEvent(e);
}
void CaptureOverlay::keyReleaseEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Alt) {
    alt = false;
    update();
  }
  if (e->key() == Qt::Key_Space) {
    space = false;
    origin += pointer - spaceOrigin;
    previous = selection;
  }
}
bool CaptureOverlay::confirmExit() {
  if (!annotation || annotation->marks.isEmpty() ||
      !store->config["confirm_capture_exit"].toBool(true))
    return true;
  QMessageBox prompt(QMessageBox::Question, "picut · 退出截图",
                     "确定要退出截图吗？当前标注将不会保存。",
                     QMessageBox::Yes | QMessageBox::Cancel, this);
  prompt.button(QMessageBox::Yes)->setText("确定");
  prompt.button(QMessageBox::Cancel)->setText("取消");
  prompt.setDefaultButton(QMessageBox::Cancel);
  auto check = new QCheckBox("以后不再显示此消息");
  prompt.setCheckBox(check);
  prompt.ensurePolished();
  prompt.adjustSize();
  prompt.move(globalSelection().center() - prompt.rect().center());
  QTimer::singleShot(0, &prompt, [this, &prompt] {
    auto frame = prompt.frameGeometry();
    prompt.windowHandle()->setFramePosition(globalSelection().center() -
                                            (frame.center() - frame.topLeft()));
  });
  bool accepted = prompt.exec() == QMessageBox::Yes;
  if (accepted && check->isChecked()) {
    store->config["confirm_capture_exit"] = false;
    store->saveConfig();
  }
  return accepted;
}
bool CaptureOverlay::complete(const QString &action) {
  if (action == "pin" && scrollCapture) {
    scrollCapture->finish("pin");
    return true;
  }
  commitText();
  if (action=="record" && !recording) {
    if (selection.width()<2 || selection.height()<2) return false;
    setRecordingMode(true); return true;
  }
  if (recording && (action=="cancel" || action=="back")) {
    setRecordingMode(false); return true;
  }
  if (action == "scroll") {
    if (recording || selection.width() < 12 || selection.height() < 64) return false;
    if (scrollCapture) { scrollCapture->raise(); return true; }
    if (annotation) annotation->savePreferences(store);
    auto region = globalSelection();
    scrollCapture = new ScrollCapture(store,region,sourceWindow);
    scrollCapture->output = [this,region](const QImage &image,const QString &request) {
      auto target = request == "edit" ? QRect() : region;
      if (request == "pin") {
        auto screen = QGuiApplication::screenAt(region.center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        const auto available = screen->availableGeometry();
        const auto anchor = QPoint(qBound(available.left(), region.left(), available.right()),
                                   qBound(available.top(), region.top(), available.bottom()));
        // Keep the complete long image in proportion and visible from the
        // original selection anchor. Exported pixels remain at full resolution.
        const QSize room(qMin(region.width(), available.right() - anchor.x() + 1),
                         available.bottom() - anchor.y() + 1);
        target = QRect(anchor, image.size().scaled(room, Qt::KeepAspectRatio));
      }
      if (output && !output(image,target,request)) return false;
      store->config["last_capture_rect"] = QJsonArray{region.x(),region.y(),region.width(),region.height()};
      store->saveConfig(); return true;
    };
    connect(scrollCapture,&ScrollCapture::finished,this,[this](bool accepted) {
      scrollCapture = nullptr;
      if (accepted) close();
      else if (annotation) { show(); activateWindow(); setFocus(); }
    });
    hide(); scrollCapture->begin(); return true;
  }
  if (action == "cancel") {
    if (!confirmExit())
      return false;
    if (store->config["keep_all_capture_history"].toBool() && annotation &&
        !recording)
      store->addImage(annotation->render());
    close();
    return true;
  }
  if (selection.width() < 2 || selection.height() < 2)
    return false;
  auto region = globalSelection();
  if (recording) {
    if (pickingRecordingWindow) return false;
    rebuildAnnotation();
    if (annotation) { annotation->confirm(); annotation->savePreferences(store); }
    hide();
    emit recordRegion(region);
    return true;
  }
  if (imageTranslation && imageTranslation->isVisible()) {
    const auto image=imageTranslation->displayedImage();
    if (image.isNull()) return false;
    if (output && !output(image,region,action)) return false;
    store->config["last_capture_rect"]=QJsonArray{region.x(),region.y(),region.width(),region.height()};
    store->saveConfig(); close(); return true;
  }
  rebuildAnnotation();
  if (annotation->draft) {
    auto mark = *annotation->draft;
    annotation->draft.reset();
    annotation->add(mark);
  }
  annotation->savePreferences(store);
  auto image = annotation->render();
  hide();
  if (output && !output(image, region, action)) {
    show();
    activateWindow();
    return false;
  }
  store->config["last_capture_rect"] =
      QJsonArray{region.x(), region.y(), region.width(), region.height()};
  store->saveConfig();
  close();
  return true;
}
void CaptureOverlay::closeEvent(QCloseEvent *event) {
  if (scrollCapture) {
    scrollCapture->disconnect(this);
    delete scrollCapture;
    scrollCapture = nullptr;
  }
  if (annotation)
    annotation->savePreferences(store);
  annotation.reset();
  desktop.frames.clear();
  desktop.cursorImage = {};
  output = {};
  emit finished();
  event->accept();
}

void CaptureOverlay::showEvent(QShowEvent *event) {
  EditingSurface::showEvent(event);
  activateWindow();
  setFocus();
  if (recording || !desktop.foreground)
    return;
  QList<QPair<QRect, QRect>> mappings;
  for (auto screen : QGuiApplication::screens())
    mappings.append({screen->geometry(), physicalRect(screen->geometry())});
  auto worker =
      new ElementDetector(reinterpret_cast<HWND>(desktop.foreground), mappings);
  desktop.foreground = 0;
  connect(worker, &ElementDetector::rectanglesReady, this,
          [this](QList<QRect> values) {
            desktop.windows = values + desktop.windows;
            if (selection.isEmpty()) {
              auto global = pointer + pos();
              for (auto r : desktop.windows)
                if (r.contains(global)) {
                  hover = r.intersected(geometry()).translated(-pos());
                  break;
                }
              update();
            }
          });
  worker->start();
}
