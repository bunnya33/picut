#include "recording_overlay.h"
#include "native_windows.h"
#include "compact_ui.h"
#include "recording_capture.h"
#include <QtCore/QSignalBlocker>
#include <QtGui/QCloseEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QRegion>
#include <limits>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#ifndef Q_OS_MACOS
#include <dwmapi.h>
#endif

class RecordingFrame : public QWidget {
public:
  RecordingFrame(QRect region)
      : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                             Qt::WindowStaysOnTopHint |
                             Qt::WindowTransparentForInput |
                             Qt::WindowDoesNotAcceptFocus) {
    setObjectName("recordingBoundary");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setGeometry(region);
    captureRegion = region;
  }
  void setExterior(bool value) {
    if (exterior == value) return;
    exterior = value;
    setGeometry(value ? captureRegion.adjusted(-4,-4,4,4) : captureRegion);
    update();
  }
  void setPaused(bool paused) {
    QColor next = paused ? QColor("#f5b84b") : QColor("#ff4c5e");
    if (next != color) {
      color = next;
      update();
    }
  }

  void setRegion(QRect value) {
    captureRegion=value; setGeometry(exterior?value.adjusted(-4,-4,4,4):value); update();
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (exterior) p.setClipRegion(QRegion(rect()).subtracted(QRegion(captureRegion.translated(-pos()))));
    p.setPen(QPen(color, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    p.setPen(QPen(color, 4));
    const int corner = qMin(16, qMin(width(), height()) / 3);
    for (auto origin :
         {QPoint(2, 2), QPoint(width() - 3, 2), QPoint(2, height() - 3),
          QPoint(width() - 3, height() - 3)}) {
      int dx = origin.x() < width() / 2 ? corner : -corner;
      int dy = origin.y() < height() / 2 ? corner : -corner;
      p.drawLine(origin, origin + QPoint(dx, 0));
      p.drawLine(origin, origin + QPoint(0, dy));
    }
  }

private:
  QColor color = QColor("#ff4c5e");
  QRect captureRegion;
  bool exterior = false;
};

class RecordingControls : public QWidget {
public:
  RecordingControls(QRect region, bool systemAudio, bool microphone)
      : QWidget(nullptr,
                Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setObjectName("recordingControls");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(compactToolbarStyle()+
        "QLabel{color:#333;border:0;background:transparent;font-family:'Microsoft YaHei UI';font-size:12px;}");
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    moveRegion = new QToolButton;
    moveRegion->setObjectName("moveRecordingRegion"); moveRegion->setIcon(recordingIcon("move",Qt::black));
    moveRegion->setIconSize({20,20}); moveRegion->setFixedSize(32,32); moveRegion->setCursor(Qt::SizeAllCursor);
    moveRegion->setToolTip("拖动以移动录屏区域"); moveRegion->hide(); layout->addWidget(moveRegion);
    auto grip = new QLabel;
    grip->setPixmap(recordingIcon("grip").pixmap(16,16));
    grip->setAlignment(Qt::AlignCenter); grip->setFixedSize(24,32);
    grip->setObjectName("recordingGrip");
    grip->setCursor(Qt::SizeAllCursor);
    grip->setToolTip("拖动控制条");
    layout->addWidget(grip);
    state = new QLabel("准备录制");
    state->setObjectName("recordingState");
    state->setContentsMargins(0,0,8,0); state->setMinimumWidth(64);
    layout->addWidget(state);
    time = new QLabel("00:00:00");
    time->setObjectName("recordingTime");
    time->setStyleSheet("font-family:Consolas;font-size:14px;color:#333;");
    time->setContentsMargins(4,0,8,0); time->setMinimumWidth(88);
    layout->addWidget(time);
    auto separator=new QFrame; separator->setFixedSize(1,14);
    separator->setStyleSheet("background:#d5dce7;border:0;"); layout->addWidget(separator);
    annotate = new QToolButton;
    annotate->setObjectName("editRecordingMarks"); annotate->setCheckable(true);
    annotate->setIcon(recordingIcon("pen")); annotate->setIconSize({16,16}); annotate->setFixedSize(32,32);
    annotate->setToolTip("标记：展开工具栏；关闭后恢复操作桌面"); annotate->hide(); layout->addWidget(annotate);
    pause = new QToolButton;
    pause->setObjectName("pauseRecording");
    pause->setIcon(recordingIcon("pause")); pause->setIconSize({16,16}); pause->setFixedSize(32,32);
    pause->setToolTip("暂停录屏 · Ctrl+Alt+P");
    pause->setEnabled(false);
    layout->addWidget(pause);
    stop = new QToolButton;
    stop->setObjectName("stopRecording");
    stop->setIcon(recordingIcon("stop")); stop->setIconSize({16,16}); stop->setFixedSize(32,32);
    stop->setToolTip("结束录屏 · Ctrl+Alt+S");
    stop->setEnabled(false);
    layout->addWidget(stop);
    for (bool system : {true, false}) {
      if (!(system ? systemAudio : microphone))
        continue;
      auto mute = new QToolButton;
      mute->setCheckable(true);
      mute->setObjectName(system ? "muteSystemAudio" : "muteMicrophone");
      mute->setIcon(recordingIcon(system?"speaker":"microphone"));
      mute->setIconSize({16,16}); mute->setFixedSize(32,32);
      mute->setToolTip(system ? "系统声音静音" : "麦克风静音");
      layout->addWidget(mute);
    }
    adjustSize();
    auto screen = QGuiApplication::screenAt(region.center());
    if (!screen)
      screen = QGuiApplication::primaryScreen();
    const QRect screenArea = screen->availableGeometry();
    // Use exterior space when it can hold the entire panel.
    dragBounds = region.width() >= width() && region.height() >= height()
                     ? region
                     : screenArea;
    int x = region.center().x() - width() / 2;
    int y = region.top() + 10;
    if (!region.contains(QRect(x, y, width(), height())))
      y = region.top() - height() - 8;
    move(clamped(QPoint(x, y)));
    placeOutside(region);
    for (auto label : {grip, state, time})
      label->installEventFilter(this);
  }
  QLabel *state, *time;
  QToolButton *pause, *stop, *annotate, *moveRegion;
  void beginRegionMove(QRect value,bool exterior) {
    followingRegion=true;
    followingExterior=exterior;
    followingOffset=pos()-value.topLeft();
  }
  void endRegionMove(QRect value) {
    followingRegion=false;
    followRegion(value);
  }
  void followRegion(QRect value) {
    auto screen=QGuiApplication::screenAt(value.center());
    if (!screen) screen=QGuiApplication::primaryScreen();
    dragBounds=screen->availableGeometry();
    if (followingRegion) {
      // Keep the initial offset throughout the drag. Clamping must not change
      // that offset, otherwise reversing direction leaves the panel displaced.
      const auto point=value.topLeft()+followingOffset;
      move(followingExterior?point:clamped(point));
      return;
    }
    move(clamped({value.center().x()-width()/2,value.top()-height()-8}));
    placeOutside(value);
  }
  bool placeOutside(QRect capture) {
    QRect bestScreen;
    QPoint bestPoint;
    qint64 distance=std::numeric_limits<qint64>::max();
    const auto home=QGuiApplication::screenAt(capture.center());
    for (auto screen : QGuiApplication::screens()) {
      const auto available=screen->availableGeometry();
      for (const int gap:{8,0}) {
        const int top=qBound(available.top(),capture.top()-gap,available.bottom()+1);
        const int bottom=qBound(available.top(),capture.bottom()+1+gap,available.bottom()+1);
        const int left=qBound(available.left(),capture.left()-gap,available.right()+1);
        const int right=qBound(available.left(),capture.right()+1+gap,available.right()+1);
        const QList<QRect> spaces=available.intersects(capture)?QList<QRect>{
            {available.left(),available.top(),available.width(),top-available.top()},
            {available.left(),bottom,available.width(),available.bottom()+1-bottom},
            {available.left(),available.top(),left-available.left(),available.height()},
            {right,available.top(),available.right()+1-right,available.height()}}:QList<QRect>{available};
        for (auto area:spaces) {
          if (area.width()<width() || area.height()<height()) continue;
          const QPoint point(qBound(area.left(),x(),area.right()-width()+1),
                             qBound(area.top(),y(),area.bottom()-height()+1));
          const qint64 next=(point-pos()).manhattanLength()+(screen==home?0:1000000)+(gap?0:100000);
          if (next<distance) { distance=next; bestScreen=available; bestPoint=point; }
        }
      }
    }
    if (bestScreen.isEmpty()) return false;
    dragBounds=bestScreen; move(bestPoint); return true;
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#d9dfe8"));
    p.setBrush(QColor("#fbfcfe"));
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 10, 10);
  }
  void closeEvent(QCloseEvent *event) override {
    event->ignore(); // The active controller stays available until recording
                     // finishes.
  }
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseMove)
      return dragEvent(static_cast<QMouseEvent *>(event));
    return false;
  }
  void mousePressEvent(QMouseEvent *event) override { dragEvent(event); }
  void mouseMoveEvent(QMouseEvent *event) override { dragEvent(event); }
  void mouseReleaseEvent(QMouseEvent *event) override { dragEvent(event); }

private:
  QRect dragBounds;
  QPoint dragOffset, followingOffset;
  bool dragging = false, followingRegion = false, followingExterior = false;
  QPoint clamped(QPoint point) const {
    return {qBound(dragBounds.left(), point.x(),
                   qMax(dragBounds.left(), dragBounds.right() - width() + 1)),
            qBound(dragBounds.top(), point.y(),
                   qMax(dragBounds.top(), dragBounds.bottom() - height() + 1))};
  }
  bool dragEvent(QMouseEvent *event) {
    if (event->type() == QEvent::MouseButtonPress &&
        event->button() == Qt::LeftButton) {
      dragging = true;
      dragOffset = event->globalPosition().toPoint() - pos();
    } else if (event->type() == QEvent::MouseMove && dragging) {
      move(clamped(event->globalPosition().toPoint() - dragOffset));
    } else if (event->type() == QEvent::MouseButtonRelease) {
      dragging = false;
    } else {
      return false;
    }
    return true;
  }
};

class RecordingArtwork : public QWidget {
public:
  RecordingArtwork(QRect region,QImage pixels)
      : QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|
                        Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus),image(std::move(pixels)) {
    setObjectName("recordingArtwork"); setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating); setGeometry(region);
  }
  void setImage(const QImage &value) { image=value; update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this); p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(),Qt::transparent); p.drawImage(rect(),image);
  }
private:
  QImage image;
};

RecordingOverlay::RecordingOverlay(Recorder *recorder, QRect region,
                                   bool systemAudio, bool microphone,
                                   QObject *parent, std::function<bool(QWidget *)> exclusion)
    : QObject(parent), recorder(recorder), logicalRegion(region),
      frame(std::make_unique<RecordingFrame>(region)),
      panel(std::make_unique<RecordingControls>(region, systemAudio,
                                                microphone)), exclusion(std::move(exclusion)) {
  connect(&timer, &QTimer::timeout, this, &RecordingOverlay::refresh);
  timer.setInterval(100);
  connect(recorder, &Recorder::started, this, [this] {
    begun = true;
    refresh();
  });
  connect(recorder, &Recorder::finished, this, &RecordingOverlay::hide);
  connect(recorder, &Recorder::failed, this, &RecordingOverlay::hide);
  connect(recorder, &QObject::destroyed, this, &RecordingOverlay::hide);
  connect(panel->pause, &QToolButton::clicked, this, [this] {
    if (this->recorder) {
      this->recorder->pause();
      refresh();
    }
  });
  connect(panel->stop, &QToolButton::clicked, this, [this] {
    if ((preparing || countdown>=0) && !begun) { emit countdownCanceled(); return; }
    if (this->recorder) {
      this->recorder->stop();
      refresh();
    }
  });
  for (bool system : {true, false})
    if (auto mute = panel->findChild<QToolButton *>(system ? "muteSystemAudio"
                                                           : "muteMicrophone"))
      connect(mute, &QToolButton::toggled, this, [this, system](bool value) {
        if (this->recorder)
          this->recorder->setMuted(system, value);
      });
}
RecordingOverlay::~RecordingOverlay() {
  // Cocoa can deliver focus events synchronously while closing the controls.
  panel->moveRegion->removeEventFilter(this);
  hide();
}
QWidget *RecordingOverlay::boundary() const { return frame.get(); }
QWidget *RecordingOverlay::controls() const { return panel.get(); }
void RecordingOverlay::setArtwork(const QImage &image) {
  if (markLayer && !image.isNull()) {
    static_cast<RecordingArtwork *>(markLayer.get())->setImage(image); return;
  }
  markLayer.reset(); artworkExcluded=false;
  if (!image.isNull()) markLayer=std::make_unique<RecordingArtwork>(logicalRegion,image);
  if (shown) { showArtwork(); refresh(); }
}
void RecordingOverlay::showArtwork() {
  if (!markLayer || compatibility || QGuiApplication::platformName()=="offscreen") return;
  auto exclude=[this](QWidget *widget) { return exclusion?exclusion(widget):excludeCapture(widget); };
  artworkExcluded=exclude(markLayer.get());
  if (artworkExcluded) {
    markLayer->show(); artworkExcluded=exclude(markLayer.get());
    if (artworkExcluded) { markLayer->raise(); panel->raise(); }
  }
  if (!artworkExcluded) {
    markLayer->hide();
#ifndef Q_OS_MACOS
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(markLayer->winId()),WDA_NONE);
#endif
  }
}
void RecordingOverlay::configureLive(std::shared_ptr<RecordingLiveState> state, quintptr window) {
  live=std::move(state); targetWindow=window;
  panel->annotate->show(); panel->moveRegion->setVisible(!window); panel->adjustSize();
  panel->followRegion(logicalRegion);
  panel->moveRegion->installEventFilter(this);
  connect(panel->annotate,&QToolButton::toggled,this,[this](bool value) { setEditing(value); emit editingRequested(value); });
  timer.setInterval(16);
}
void RecordingOverlay::setEditing(bool value) {
  editing=value;
  QSignalBlocker block(panel->annotate); panel->annotate->setChecked(value);
  panel->annotate->setIcon(recordingIcon("pen",value?QColor(Qt::white):QColor("#333333")));
  refresh();
}
void RecordingOverlay::setRegion(QRect value) {
  if (value.isEmpty() || logicalRegion==value) return;
  logicalRegion=value; frame->setRegion(value);
  if (markLayer) markLayer->setGeometry(value);
  panel->followRegion(value);
  if (compatibility) {
    outsideControls=!value.intersects(panel->geometry());
    if (!movingRegion) outsideControls=panel->placeOutside(value);
  }
  if (live) live->setRegion(physicalRect(value), value.size());
  emit regionChanged(value);
}
bool RecordingOverlay::eventFilter(QObject *watched,QEvent *event) {
  if (!panel || watched!=panel->moveRegion || targetWindow) return false;
  if (event->type()!=QEvent::MouseButtonPress && event->type()!=QEvent::MouseMove && event->type()!=QEvent::MouseButtonRelease) return false;
  auto mouse=static_cast<QMouseEvent *>(event);
  if (event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton) {
    movingRegion=true; moveOrigin=mouse->globalPosition().toPoint(); originalRegion=logicalRegion;
    panel->beginRegionMove(logicalRegion,compatibility); return true;
  }
  if (!movingRegion) return false;
  if (event->type()==QEvent::MouseMove) {
    auto next=originalRegion.translated(mouse->globalPosition().toPoint()-moveOrigin);
    auto screen=QGuiApplication::screenAt(next.center());
    if (screen) {
      const auto bounds=screen->geometry();
      const auto captureSize=physicalRect(originalRegion).size();
      next.setSize(QSize(qRound(captureSize.width()/screen->devicePixelRatio()),qRound(captureSize.height()/screen->devicePixelRatio())));
      next.moveLeft(qBound(bounds.left(),next.x(),qMax(bounds.left(),bounds.right()-next.width()+1)));
      next.moveTop(qBound(bounds.top(),next.y(),qMax(bounds.top(),bounds.bottom()-next.height()+1)));
      setRegion(next);
    }
    return true;
  }
  if (event->type()==QEvent::MouseButtonRelease && mouse->button()==Qt::LeftButton) {
    movingRegion=false; panel->endRegionMove(logicalRegion);
    if (compatibility) outsideControls=panel->placeOutside(logicalRegion);
    refresh(); return true;
  }
  return false;
}
bool RecordingOverlay::show() {
  if (compatibility) {
    shown=true; frame->show(); refresh(); timer.start(); return true;
  }
#ifdef Q_OS_MACOS
  const bool nativeDesktop = QGuiApplication::platformName() == "cocoa";
#else
  const bool nativeDesktop = QGuiApplication::platformName() == "windows";
#endif
  auto exclude = [this](QWidget *widget) { return exclusion ? exclusion(widget) : excludeCapture(widget); };
  excluded = nativeDesktop && !compatibility && exclude(frame.get()) && exclude(panel.get());
  frame->show();
  frame->raise();
  panel->show();
  panel->raise();
  if (nativeDesktop && !compatibility) {
    excluded = exclude(frame.get()) && exclude(panel.get());
    showArtwork();
#ifndef Q_OS_MACOS
    DwmFlush();
#endif
  }
  if ((nativeDesktop || exclusion) && !excluded && !compatibility) {
    compatibility=true;
    // Never leave WDA_MONITOR's black rectangle over the captured pixels.
#ifndef Q_OS_MACOS
    if (nativeDesktop) {
      SetWindowDisplayAffinity(reinterpret_cast<HWND>(frame->winId()), WDA_NONE);
      SetWindowDisplayAffinity(reinterpret_cast<HWND>(panel->winId()), WDA_NONE);
    }
#endif
    outsideControls=panel->placeOutside(logicalRegion);
  }
  shown=true;
  refresh();
  timer.start();
  return true;
}
void RecordingOverlay::hide() {
  shown=false;
  timer.stop();
  frame->hide();
  panel->hide();
  if (markLayer) markLayer->hide();
}
void RecordingOverlay::setPreparing() { preparing=true; countdown=-1; refresh(); }
void RecordingOverlay::setCountdown(int seconds) { preparing=false; countdown=seconds; refresh(); }
void RecordingOverlay::refresh() {
  if (!recorder)
    return;
  const bool saving = recorder->finalizing();
  bool paused = recorder->paused();
  bool targetHidden = false;
  const bool counting=(preparing || countdown>=0) && !begun;
  if (targetWindow && !saving) {
    const auto info=recordingWindowInfo(targetWindow);
    if (!info.valid && begun) { recorder->stop(); return; }
    if (begun) recorder->setTargetUnavailable(info.minimized);
    paused=recorder->paused();
    targetHidden=info.minimized;
    if (info.valid && !info.minimized) setRegion(info.logical);
  }
  if (targetWindow) frame->setVisible(shown && !targetHidden);
  if (markLayer) markLayer->setVisible(shown && artworkExcluded && !compatibility && !saving && !editing && !targetHidden);
  if (targetHidden && editing) { setEditing(false); emit editingRequested(false); }
  if (saving && editing) { setEditing(false); emit editingRequested(false); }
  if (shown && compatibility) {
    frame->setExterior(!counting);
    panel->setVisible(counting || outsideControls);
    panel->setToolTip("系统不支持捕获排除，控制条放在录制范围外；全屏无空余位置时使用托盘或录屏快捷键控制。");
  }
  qint64 seconds = begun ? qint64(recorder->elapsed()) : 0;
  panel->time->setText(QString("%1:%2:%3")
                           .arg(seconds / 3600, 2, 10, QChar('0'))
                           .arg(seconds / 60 % 60, 2, 10, QChar('0'))
                           .arg(seconds % 60, 2, 10, QChar('0')));
  if (counting) panel->time->setText(preparing?QString("…"):QString("%1 秒").arg(countdown));
  panel->state->setText(preparing ? "准备中"
                        : counting ? "倒计时"
                        : saving   ? "正在保存"
                        : !begun ? "准备录制"
                        : paused ? "已暂停"
                                 : "● 录制中");
  const auto stateStyle =
      QString("color:%1;").arg(paused ? "#a66b00" : "#d82b39");
  if (panel->state->styleSheet() != stateStyle)
    panel->state->setStyleSheet(stateStyle);
  panel->pause->setVisible(!counting);
  panel->pause->setIcon(recordingIcon(paused?"play":"pause"));
  panel->pause->setToolTip(paused?"继续录屏 · Ctrl+Alt+P":"暂停录屏 · Ctrl+Alt+P");
  panel->pause->setEnabled(begun && recorder->active() && !saving);
  panel->annotate->setEnabled(begun && recorder->active() && !saving && !compatibility && excluded && !targetHidden);
  if (compatibility) panel->annotate->setToolTip("当前系统不支持捕获排除，录制中编辑标记需要 Windows 10 2004 或更新版本。");
#ifdef Q_OS_MACOS
  if (live) live->setControls(panel->isVisible() ? panel->geometry() : QRect());
#else
  if (live) live->setControls(panel->isVisible()?physicalRect(panel->geometry()):QRect());
#endif
  panel->moveRegion->setEnabled(!saving && !counting);
  panel->stop->setEnabled(counting || (begun && recorder->active() && !saving));
  panel->stop->setIcon(recordingIcon(counting?"cancel":"stop"));
  panel->stop->setToolTip(counting?"取消录屏倒计时 · Esc":"结束录屏 · Ctrl+Alt+S");
  for (auto name : {"muteSystemAudio", "muteMicrophone"})
    if (auto mute = panel->findChild<QToolButton *>(name))
      mute->setEnabled(begun && recorder->active() && !saving);
  frame->setPaused(paused);
}
