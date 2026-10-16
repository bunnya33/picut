#include "recording_overlay.h"
#include "native_windows.h"
#include "compact_ui.h"
#include <QtGui/QCloseEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <dwmapi.h>

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
  }
  void setPaused(bool paused) {
    QColor next = paused ? QColor("#f5b84b") : QColor("#ff4c5e");
    if (next != color) {
      color = next;
      update();
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
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
};

class RecordingControls : public QWidget {
public:
  RecordingControls(QRect region, bool systemAudio, bool microphone)
      : QWidget(nullptr,
                Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setObjectName("recordingControls");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet(compactToolbarStyle()+
        "QLabel{color:#333;border:0;background:transparent;font-family:'Microsoft YaHei UI';font-size:12px;}"
        "QPushButton{background:transparent;border:0;padding:0;}"
        "QPushButton:hover{background:#dedede;}");
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 0, 0);
    layout->setSpacing(4);
    auto grip = new QLabel("⋮⋮");
    grip->setObjectName("recordingGrip");
    grip->setCursor(Qt::SizeAllCursor);
    grip->setToolTip("拖动控制条");
    layout->addWidget(grip);
    state = new QLabel("准备录制");
    state->setObjectName("recordingState");
    state->setMinimumWidth(48);
    layout->addWidget(state);
    time = new QLabel("00:00:00");
    time->setObjectName("recordingTime");
    time->setStyleSheet("font-family:Consolas;font-size:14px;color:#333;");
    time->setMinimumWidth(77);
    layout->addWidget(time);
    pause = new QPushButton;
    pause->setObjectName("pauseRecording");
    pause->setIcon(recordingIcon("pause")); pause->setIconSize({16,16}); pause->setFixedSize(32,32);
    pause->setToolTip("暂停录屏 · Ctrl+Alt+P");
    pause->setEnabled(false);
    layout->addWidget(pause);
    stop = new QPushButton;
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
    // Fullscreen and normal regions keep the controls inside their border.
    dragBounds = region.width() >= width() && region.height() >= height()
                     ? region
                     : screenArea;
    int x = region.center().x() - width() / 2;
    int y = region.top() + 10;
    if (!region.contains(QRect(x, y, width(), height())))
      y = region.top() - height() - 8;
    move(clamped(QPoint(x, y)));
    for (auto label : {grip, state, time})
      label->installEventFilter(this);
  }
  QLabel *state, *time;
  QPushButton *pause, *stop;

protected:
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
  QPoint dragOffset;
  bool dragging = false;
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

RecordingOverlay::RecordingOverlay(Recorder *recorder, QRect region,
                                   bool systemAudio, bool microphone,
                                   QObject *parent)
    : QObject(parent), recorder(recorder), logicalRegion(region),
      frame(std::make_unique<RecordingFrame>(region)),
      panel(std::make_unique<RecordingControls>(region, systemAudio,
                                                microphone)) {
  connect(&timer, &QTimer::timeout, this, &RecordingOverlay::refresh);
  timer.setInterval(100);
  connect(recorder, &Recorder::started, this, [this] {
    begun = true;
    refresh();
  });
  connect(recorder, &Recorder::finished, this, &RecordingOverlay::hide);
  connect(recorder, &Recorder::failed, this, &RecordingOverlay::hide);
  connect(recorder, &QObject::destroyed, this, &RecordingOverlay::hide);
  connect(panel->pause, &QPushButton::clicked, this, [this] {
    if (this->recorder) {
      this->recorder->pause();
      refresh();
    }
  });
  connect(panel->stop, &QPushButton::clicked, this, [this] {
    if (countdown>=0 && !begun) { emit countdownCanceled(); return; }
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
RecordingOverlay::~RecordingOverlay() { timer.stop(); }
QWidget *RecordingOverlay::boundary() const { return frame.get(); }
QWidget *RecordingOverlay::controls() const { return panel.get(); }
bool RecordingOverlay::show() {
  const bool windows = QGuiApplication::platformName() == "windows";
  excluded =
      windows && excludeCapture(frame.get()) && excludeCapture(panel.get());
  if (windows && !excluded) {
    hide();
    return false;
  }
  frame->show();
  frame->raise();
  panel->show();
  panel->raise();
  if (windows) {
    excluded = excludeCapture(frame.get()) && excludeCapture(panel.get());
    if (!excluded) {
      hide();
      return false;
    }
    DwmFlush();
  }
  refresh();
  timer.start();
  return true;
}
void RecordingOverlay::hide() {
  timer.stop();
  frame->hide();
  panel->hide();
}
void RecordingOverlay::setCountdown(int seconds) { countdown=seconds; refresh(); }
void RecordingOverlay::refresh() {
  if (!recorder)
    return;
  const bool saving = recorder->finalizing();
  const bool paused = recorder->paused();
  const bool counting=countdown>=0 && !begun;
  qint64 seconds = begun ? qint64(recorder->elapsed()) : 0;
  panel->time->setText(QString("%1:%2:%3")
                           .arg(seconds / 3600, 2, 10, QChar('0'))
                           .arg(seconds / 60 % 60, 2, 10, QChar('0'))
                           .arg(seconds % 60, 2, 10, QChar('0')));
  if (counting) panel->time->setText(QString("%1 秒").arg(countdown));
  panel->state->setText(counting ? "倒计时"
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
  panel->stop->setEnabled(counting || (begun && recorder->active() && !saving));
  panel->stop->setIcon(recordingIcon(counting?"cancel":"stop"));
  panel->stop->setToolTip(counting?"取消录屏倒计时 · Esc":"结束录屏 · Ctrl+Alt+S");
  for (auto name : {"muteSystemAudio", "muteMicrophone"})
    if (auto mute = panel->findChild<QToolButton *>(name))
      mute->setEnabled(begun && recorder->active() && !saving);
  frame->setPaused(paused);
}
