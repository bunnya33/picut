#include "scroll_capture.h"
#include "scroll_grabber.h"
#include "capture.h"
#include "toolbar.h"
#include "compact_ui.h"
#include "native_windows.h"
#include <QtCore/QScopeGuard>
#include <QtGui/QCloseEvent>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolButton>
#include <chrono>
#include <utility>
#include <dwmapi.h>

namespace {
qint64 monotonicNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool supportsCaptureExclusion() {
  // Before 2004 WDA_EXCLUDEFROMCAPTURE behaves like WDA_MONITOR (black),
  // even if Set/GetWindowDisplayAffinity accepts the flag value.
  using VersionFunction=LONG(WINAPI *)(OSVERSIONINFOW *);
  auto version=reinterpret_cast<VersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
  OSVERSIONINFOW info{}; info.dwOSVersionInfoSize=sizeof(info);
  return version && version(&info)==0 && (info.dwMajorVersion>10 || (info.dwMajorVersion==10 && info.dwBuildNumber>=19041));
}
ScrollCapture *inputSession = nullptr;
LRESULT CALLBACK scrollMouseHook(int code, WPARAM kind, LPARAM raw) {
  if (code == HC_ACTION && kind == WM_MOUSEWHEEL && inputSession) {
    const auto event = reinterpret_cast<MSLLHOOKSTRUCT *>(raw);
    if (inputSession->acceptsScrollInput({event->pt.x,event->pt.y}) &&
        !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
      const int delta = short(HIWORD(event->mouseData));
      if (delta) inputSession->noteScrollInput(delta < 0 ? 1 : -1,
                                              !(event->flags & LLMHF_INJECTED));
    }
  }
  return CallNextHookEx(nullptr,code,kind,raw);
}
LRESULT CALLBACK scrollKeyboardHook(int code, WPARAM kind, LPARAM raw) {
  if (code == HC_ACTION && (kind == WM_KEYDOWN || kind == WM_SYSKEYDOWN) && inputSession &&
      inputSession->acceptsScrollInput({},true)) {
    const auto event = reinterpret_cast<KBDLLHOOKSTRUCT *>(raw);
    int direction = 0;
    if (event->vkCode == VK_DOWN || event->vkCode == VK_NEXT || event->vkCode == VK_END) direction=1;
    if (event->vkCode == VK_UP || event->vkCode == VK_PRIOR || event->vkCode == VK_HOME) direction=-1;
    if (direction) inputSession->noteScrollInput(direction,!(event->flags & LLKHF_INJECTED));
  }
  return CallNextHookEx(nullptr,code,kind,raw);
}
class LongPreview : public QWidget {
public:
  explicit LongPreview(ScrollStitcher::Preview *value, QWidget *parent)
      : QWidget(parent), stitcher(value) { setObjectName("scrollPreview"); }
  std::function<void(qint64)> painted;
protected:
  void paintEvent(QPaintEvent *) override {
    const auto started=monotonicNs();
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    stitcher->paint(painter, rect().adjusted(1, 1, -1, -1));
    if(stitcher->viewportY>=0 && stitcher->size.height()>0) {
      const double scale=double(height()-2)/stitcher->size.height();
      painter.setPen(QPen(QColor("#f59b23"),2));
      painter.drawRect(QRectF(1,1+stitcher->viewportY*scale,width()-2,stitcher->viewportHeight*scale));
    }
    painter.setPen(QColor("#2080f0")); painter.drawRect(rect().adjusted(0,0,-1,-1));
    if(painted) painted(monotonicNs()-started);
  }
private:
  ScrollStitcher::Preview *stitcher;
};
}

ScrollCapture::ScrollCapture(Store *value, QRect source, quintptr window, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                          Qt::WindowDoesNotAcceptFocus),
      store(value), region(source), sourceWindow(window) {
  setObjectName("scrollCapture");
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_DeleteOnClose);
  QRect desktop;
  for (auto screen : QGuiApplication::screens()) desktop = desktop.united(screen->geometry());
  setGeometry(desktop);
  auto screen = QGuiApplication::screenAt(region.center());
  available = (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry()
                  .translated(-pos()).adjusted(8,8,-8,-8);
  region.translate(-pos());
  preview = new LongPreview(&displayed, this);
  static_cast<LongPreview *>(preview)->painted=[this](qint64 duration) {
    if(!paintSerial || paintSerial==measuredPaintSerial) return;
    measuredPaintSerial=paintSerial; ++paintedUpdates;
    paintTotalNs+=duration; paintMaximumNs=qMax(paintMaximumNs,duration);
    if(paintCapturedAtNs) displayMaximumNs=qMax(displayMaximumNs,monotonicNs()-paintCapturedAtNs);
    if(paintPublishedAtNs) uiWaitMaximumNs=qMax(uiWaitMaximumNs,monotonicNs()-paintPublishedAtNs);
  };
  bar = new QFrame(this); bar->setObjectName("scrollToolbar");
  bar->setStyleSheet("QFrame#scrollToolbar{background:#f8f8f8;border:1px solid #d4d4d4;"
                    "border-radius:7px;}QToolButton{border:0;background:transparent;}"
                    "QToolButton:hover{background:#dedede;border-radius:4px;}"
                    "QToolButton:checked{background:#d9eaff;border-radius:4px;}");
  auto row = new QHBoxLayout(bar); row->setContentsMargins(6,5,6,5); row->setSpacing(4);
  auto button = [&](QString action, QString icon, QString tip, QColor color = QColor("#444444")) {
    auto b = new QToolButton(bar); b->setObjectName("scroll_" + action);
    b->setFixedSize(36,32); b->setIconSize({20,20});
    b->setIcon(icon == "play" ? recordingIcon(icon) : toolIcon(icon,color));
    b->setToolTip(tip); row->addWidget(b); return b;
  };
  automatic = button("automatic", "play", "自动向下滚动 / 暂停");
  automatic->setCheckable(true);
  automatic->hide(); // The V2 trial validates manual capture first.
  connect(automatic, &QToolButton::clicked, this, &ScrollCapture::setAutomatic);
  connect(button("edit", "pen", "完成并标注"), &QToolButton::clicked,
          this, [this] { finish("edit"); });
  connect(button("save", "save", "保存长截图"), &QToolButton::clicked,
          this, [this] { finish("save"); });
  connect(button("cancel", "cancel", "取消滚动截屏 · Esc", QColor("#ff3849")),
          &QToolButton::clicked, this, [this] { finish("cancel"); });
  connect(button("done", "done", "完成并复制 · Enter", QColor("#00b981")),
          &QToolButton::clicked, this, [this] { finish("copy"); });
  status = new QLabel("手动纵向滚动正文 · 避开固定栏和滚动条",this);
  status->setObjectName("scrollStatus");
  status->setWordWrap(true);
  status->setStyleSheet("background:rgba(28,28,28,210);color:white;padding:5px 8px;border-radius:3px;");
  frameProvider = [this] { return captureDesktopRegion(region.translated(pos())); };
  timer.setTimerType(Qt::PreciseTimer);
  connect(&timer, &QTimer::timeout, this, &ScrollCapture::sample);
  place();
}
ScrollCapture::~ScrollCapture() {
  stopPumps(false); releaseHotkeys(); releaseInputHooks();
  qInfo()<<"Scroll preview UI: painted_updates"<<paintedUpdates
         <<"paint_mean_ms"<<(paintedUpdates?paintTotalNs/paintedUpdates/1000000.:0.)
         <<"paint_max_ms"<<paintMaximumNs/1000000.
         <<"capture_to_paint_max_ms"<<displayMaximumNs/1000000.
         <<"ui_wait_max_ms"<<uiWaitMaximumNs/1000000.;
}
void ScrollCapture::stopPumps(bool drain) {
  if(framePump) framePump->stop();
  if(matchPump) {
    matchPump->stop(drain);
    if(auto update=matchPump->takeUpdate(matchSerial)) {
      matchSerial=update->serial; displayed=std::move(update->preview);
    }
    matchPump.reset();
  }
  framePump.reset();
}
void ScrollCapture::begin(bool enableLive) {
  live = enableLive && QGuiApplication::platformName() == "windows";
  show();
  if (live && QGuiApplication::platformName() == "windows") {
    excluded = supportsCaptureExclusion() && excludeCapture(this);
    if (!excluded) SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_NONE);
    place();
    for (const auto binding : {QPair<int,UINT>{2201,VK_ESCAPE}, {2202,VK_RETURN}})
      if (RegisterHotKey(reinterpret_cast<HWND>(winId()), binding.first, MOD_NOREPEAT, binding.second))
        hotkeys.append(binding.first);
    installInputHooks();
    QTimer::singleShot(0,this,[this] {
      if (closing) return;
      if (sourceWindow && IsWindow(reinterpret_cast<HWND>(sourceWindow)))
        SetForegroundWindow(reinterpret_cast<HWND>(sourceWindow));
      DwmFlush(); sample();
      // A single-monitor hole has a stable physical pixel mapping. On older
      // Windows, outside controls never enter the capture image.
      const QRect global=region.translated(pos());
      const auto screen=QGuiApplication::screenAt(global.center());
      if(!screen || !screen->geometry().contains(global)) {
        status->setText("试验版请在同一显示器内框选滚动正文；可完成或重新框选"); place(); return;
      }
      framePump=std::make_unique<ScrollFramePump>(physicalRect(global));
      framePump->noteDirection(scrollDirection());
      const bool outside=(!preview->isVisible() || !preview->geometry().intersects(region)) &&
                         !bar->geometry().intersects(region) && !status->geometry().intersects(region);
      backgroundAcquisition=excluded || outside;
      if(backgroundAcquisition) framePump->start();
      else {
        framePump->setExternalBackend("GDI UI fallback (controls overlap selection)");
        qInfo()<<"Scroll capture backend:"<<framePump->backendName();
      }
      matchSerial=0;
      matchPump=std::make_unique<ScrollMatchPump>(framePump.get(),&stitcher); matchPump->start();
      timer.start(backgroundAcquisition ? 8 : 16);
    });
  }
}
void ScrollCapture::installInputHooks() {
  releaseInputHooks(); inputSession=this;
  mouseHook=reinterpret_cast<quintptr>(SetWindowsHookExW(WH_MOUSE_LL,scrollMouseHook,GetModuleHandleW(nullptr),0));
  keyboardHook=reinterpret_cast<quintptr>(SetWindowsHookExW(WH_KEYBOARD_LL,scrollKeyboardHook,GetModuleHandleW(nullptr),0));
  if (!mouseHook || !keyboardHook)
    qWarning("Scroll input hook unavailable; continuous image sampling remains active");
}
void ScrollCapture::releaseInputHooks() {
  if (mouseHook) UnhookWindowsHookEx(reinterpret_cast<HHOOK>(mouseHook));
  if (keyboardHook) UnhookWindowsHookEx(reinterpret_cast<HHOOK>(keyboardHook));
  mouseHook=keyboardHook=0;
  if (inputSession==this) inputSession=nullptr;
}
bool ScrollCapture::acceptsScrollInput(QPoint point, bool keyboard) const {
  if (!live || closing || finishing) return false;
  if (keyboard) return sourceWindow && GetForegroundWindow()==reinterpret_cast<HWND>(sourceWindow);
  if (!physicalRect(region.translated(pos())).contains(point)) return false;
  POINT native{point.x(),point.y()};
  return !sourceWindow || GetAncestor(WindowFromPoint(native),GA_ROOT)==reinterpret_cast<HWND>(sourceWindow);
}
void ScrollCapture::noteScrollInput(int direction, bool manual) {
  if (!direction || closing || finishing) return;
  inputDirection=direction > 0 ? 1 : -1; inputTime.restart();
  if(framePump) framePump->noteDirection(inputDirection);
  if (manual && autoScrolling)
    QTimer::singleShot(0,this,[this] { if (!closing && !finishing) setAutomatic(false); });
  if (live && !inputTickQueued) {
    inputTickQueued=true;
    QTimer::singleShot(8,this,[this] { inputTickQueued=false; sample(); });
  }
}
void ScrollCapture::releaseHotkeys() {
  for (int id : hotkeys) UnregisterHotKey(reinterpret_cast<HWND>(winId()), id);
  hotkeys.clear();
}
void ScrollCapture::setAutomatic(bool enabled) {
  autoScrolling = enabled;
  automatic->setChecked(enabled);
  automatic->setIcon(recordingIcon(enabled ? "pause" : "play"));
  idleTime.restart();
  stalledScrolls = 0;
  if (enabled) {
    status->setText("自动滚动中 · 再次点击暂停，可随时完成");
    if (live) {
      if (sourceWindow) SetForegroundWindow(reinterpret_cast<HWND>(sourceWindow));
      auto center = physicalRect(region.translated(pos())).center();
      SetCursorPos(center.x(),center.y());
    }
  } else if (!imageSize().isEmpty())
    status->setText(QString("%1 × %2 · 滚动页面继续截取").arg(imageSize().width()).arg(imageSize().height()));
  place();
}
QImage ScrollCapture::takeFrame() {
  // On older Win10, only temporarily remove UI that actually overlaps the
  // capture hole. The exterior border and all outside controls remain visible.
  const bool hidePreview = !excluded && preview->isVisible() && preview->geometry().intersects(region);
  const bool hideBar = !excluded && bar->isVisible() && bar->geometry().intersects(region);
  const bool hideStatus = !excluded && status->isVisible() && status->geometry().intersects(region);
  if (hidePreview) preview->hide();
  if (hideBar) bar->hide();
  if (hideStatus) status->hide();
  if (hidePreview || hideBar || hideStatus) { updateMask(); DwmFlush(); }
  auto restore = qScopeGuard([&] {
    if (hidePreview) preview->show();
    if (hideBar) bar->show();
    if (hideStatus) status->show();
    if (hidePreview || hideBar || hideStatus) updateMask();
  });
  return frameProvider ? frameProvider() : QImage();
}
void ScrollCapture::sample() {
  if (capturing || closing || finishing) return;
  capturing = true;
  auto guard = qScopeGuard([this] { capturing = false; });
  if(matchPump) {
    const auto acquisitionError=framePump->captureError();
    if(acquisitionError.isEmpty() && !backgroundAcquisition) framePump->enqueue(takeFrame());
    if(auto update=matchPump->takeUpdate(matchSerial)) {
      matchSerial=update->serial;
      paintSerial=update->serial;
      paintCapturedAtNs=update->capturedAtNs;
      paintPublishedAtNs=update->publishedAtNs;
      displayResult(update->result,std::move(update->preview));
      if(update->result!=ScrollStitcher::Unchanged) idleTime.restart();
    } else if(pendingFailure!=ScrollStitcher::Unchanged) reportAlignmentFailure(pendingFailure);
    if(!acquisitionError.isEmpty() &&
       (status->text()!=acquisitionError || (!framePump->isRunning() && !status->isVisible()))) {
      setAutomatic(false); status->setText(acquisitionError); place();
    }
  } else {
    const QImage frame = takeFrame();
    if (frame.isNull()) {
      setAutomatic(false); status->setText("未能读取画面，请完成或重新框选区域"); place(); return;
    }
    if (candidate.isNull()) {
      candidate = frame; idleTime.restart();
      acceptFrame(frame); return;
    }
    if (frame != candidate) {
      candidate=frame;
      const auto result=acceptFrame(frame,scrollDirection());
      if(result!=ScrollStitcher::Unchanged) idleTime.restart();
      if (result==ScrollStitcher::CacheError || result==ScrollStitcher::LimitReached) return;
    } else if (pendingFailure != ScrollStitcher::Unchanged) {
      reportAlignmentFailure(pendingFailure);
    }
  }
  if (autoScrolling && pendingFailure==ScrollStitcher::Unchanged && idleTime.elapsed()>=120) {
    if (stalledScrolls >= 3) {
      setAutomatic(false);
      status->setText("页面未继续滚动，自动滚动已停止 · 可完成或继续手动滚动");
      place(); return;
    }
    const auto global = region.translated(pos());
    const auto physical = physicalRect(global);
    POINT cursor{}; GetCursorPos(&cursor);
    const HWND target = GetAncestor(WindowFromPoint(cursor), GA_ROOT);
    if (!physical.contains(QPoint(cursor.x,cursor.y)) ||
        (sourceWindow && target != reinterpret_cast<HWND>(sourceWindow))) {
      setAutomatic(false); status->setText("自动滚动已暂停，请将鼠标移回截图内容后继续"); place(); return;
    }
    // One notch leaves generous overlap, including small-height selections.
    INPUT input{}; input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = DWORD(-WHEEL_DELTA);
    if (SendInput(1,&input,sizeof(INPUT)) != 1) {
      setAutomatic(false); status->setText("此窗口无法自动滚动，请手动滚动内容"); place();
    }
    ++stalledScrolls;
    idleTime.restart();
  }
}
void ScrollCapture::reportAlignmentFailure(ScrollStitcher::Result result) {
  if (pendingFailure!=result) failureReported=false;
  pendingFailure=result;
  if (!failureTime.isValid()) failureTime.start();
  if (failureTime.elapsed()<180) {
    status->setText("正在对齐滚动画面…"); return;
  }
  if (failureReported) return;
  failureReported=true;
  setAutomatic(false);
  status->setText(result==ScrollStitcher::BoundaryMismatch
      ? "画面内容不一致，请回滚重试，或避开固定栏重新框选；已截内容保留"
      : "暂未找到连续重叠，请向回滚动到已截内容后继续；完成只导出已确认部分");
  place();
}
ScrollStitcher::Result ScrollCapture::acceptFrame(const QImage &image, int direction) {
  Q_ASSERT(!matchPump);
  const auto result = stitcher.accept(image,direction);
  displayResult(result,stitcher.preview());
  return result;
}
void ScrollCapture::displayResult(ScrollStitcher::Result result,ScrollStitcher::Preview next) {
  const bool recovering=pendingFailure!=ScrollStitcher::Unchanged;
  const auto revision=displayed.revision;
  const auto previousSize=displayed.size;
  displayed=std::move(next);
  if (result==ScrollStitcher::Started || result==ScrollStitcher::Appended ||
      result==ScrollStitcher::Revisited || result==ScrollStitcher::Unchanged) {
    pendingFailure=ScrollStitcher::Unchanged; failureTime.invalidate(); failureReported=false;
  }
  if (result == ScrollStitcher::Started || result == ScrollStitcher::Appended ||
      result == ScrollStitcher::Revisited || (result==ScrollStitcher::Unchanged && recovering)) {
    stalledScrolls = 0;
    status->setText(QString("%1 × %2 · %3").arg(imageSize().width()).arg(imageSize().height())
                       .arg(autoScrolling ? "自动滚动中" : "滚动页面截取更多内容"));
  }
  else if (result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch) {
    reportAlignmentFailure(result);
  } else if (result != ScrollStitcher::Unchanged) {
    pendingFailure=ScrollStitcher::Unchanged; failureTime.invalidate(); failureReported=false;
    setAutomatic(false); timer.stop();
    status->setText(result == ScrollStitcher::BoundaryMismatch
        ? "滚动内容未到达框选的上下边缘，请重新框选实际滚动内容"
        : result == ScrollStitcher::LimitReached ? "已达到长图长度上限，请保存当前结果"
        : result == ScrollStitcher::CacheError ? "临时缓存无法继续写入，请完成当前结果并检查磁盘空间"
        : "画面无法可靠拼接，请缓慢滚动或返回上一处；已截内容保留");
  }
  if(displayed.size!=previousSize || recovering ||
     (result!=ScrollStitcher::Unchanged && result!=ScrollStitcher::Revisited && result!=ScrollStitcher::Appended)) place();
  if(displayed.revision!=revision || result!=ScrollStitcher::Unchanged || recovering) preview->update();
}
void ScrollCapture::place() {
  bar->adjustSize();
  int x = region.right()+12;
  int room = available.right()-x+1;
  const int leftRoom = region.left()-12-available.left();
  if (room < 96 && leftRoom > room) { room = leftRoom; x = available.left(); }
  const bool inside = room < 64;
  const int maximumWidth = inside ? qMin(240,available.width()/3) : qMin(280,room);
  QSize image = imageSize().isEmpty() ? region.size() : imageSize();
  const QSize size = ScrollStitcher::previewSize(image,{qMax(16,maximumWidth),available.height()-4});
  if (inside) x = available.right()-size.width()+1;
  else if (x == available.left()) x = region.left()-12-size.width();
  const int y = qBound(available.top(),region.top(),qMax(available.top(),available.bottom()-size.height()+1));
  preview->setGeometry({{x,y},size});
  status->setFixedWidth(qMin(450,qMax(180,region.width()))); status->adjustSize();
  int controlsX = qBound(available.left(),region.right()-bar->width()+1,
                               qMax(available.left(),available.right()-bar->width()+1));
  int controlsY = region.bottom()+9;
  if (controlsY+bar->height()>available.bottom()) controlsY=region.top()-bar->height()-9;
  controlsY=qBound(available.top(),controlsY,qMax(available.top(),available.bottom()-bar->height()+1));
  if (preview->isVisible() && QRect(QPoint(controlsX,controlsY),bar->size()).intersects(preview->geometry()))
    controlsX=qBound(available.left(),preview->x()-bar->width()-8,
                     qMax(available.left(),available.right()-bar->width()+1));
  bar->move(controlsX,controlsY);
  int statusY = region.top()-status->height()-6;
  if (statusY<available.top()) statusY=region.top()+6;
  status->move(qBound(available.left(),region.left(),qMax(available.left(),available.right()-status->width()+1)),statusY);
  const bool acquiring=backgroundAcquisition && framePump && framePump->isRunning();
  preview->setVisible(!live || excluded || !preview->geometry().intersects(region));
  bar->setVisible(excluded || !acquiring || !bar->geometry().intersects(region));
  status->setVisible(excluded || !acquiring || !status->geometry().intersects(region));
  updateMask(); update();
}
void ScrollCapture::updateMask() {
  QRegion visible(rect()); visible -= QRegion(region);
  for (auto widget : {preview,static_cast<QWidget *>(bar),static_cast<QWidget *>(status)})
    if (!widget->isHidden()) visible += widget->geometry();
  setMask(visible);
}
QRect ScrollCapture::previewGeometry() const { return preview->geometry().translated(pos()); }
void ScrollCapture::paintEvent(QPaintEvent *) {
  QPainter painter(this); painter.fillRect(rect(),QColor(0,0,0,80));
  painter.setPen(QPen(QColor("#2080f0"),2));
  painter.drawRect(region.adjusted(-2,-2,2,2));
}
void ScrollCapture::finish(const QString &action) {
  if (closing || finishing) return;
  if (action == "cancel" && !matchPump) { close(); return; }
  finishing = true; timer.stop(); setAutomatic(false);
  if(matchPump) {
    pendingAction=action;
    releaseHotkeys(); releaseInputHooks();
    if(action=="cancel") hide();
    else { status->setText("正在完成长截图…"); place(); }
    framePump->requestStop();
    matchPump->requestStop(action!="cancel",action!="cancel");
    QTimer::singleShot(0,this,&ScrollCapture::finishStopped);
    return;
  }
  // Include the latest stable screen if completion was clicked immediately
  // after a manual scroll; a bad alignment never replaces captured strips.
  if (live) {
    QImage first = takeFrame(); DwmFlush();
    if (!first.isNull() && first == takeFrame())
      acceptFrame(first,scrollDirection());
  }
  const QImage image = stitcher.image();
  if (image.isNull()) {
    finishing = false; status->setText("没有可用的截图，请重新框选区域"); place();
    if (live) timer.start(16); return;
  }
  hide(); releaseHotkeys(); releaseInputHooks();
  if (output && !output(image,action)) {
    finishing = false; begin(live); return;
  }
  closing = true; close(); emit finished(true);
}
void ScrollCapture::finishStopped() {
  if((framePump && framePump->isRunning()) || (matchPump && matchPump->isRunning())) {
    QTimer::singleShot(16,this,&ScrollCapture::finishStopped);
    return;
  }
  const auto action=std::exchange(pendingAction,QString());
  QImage image;
  if(matchPump) image=matchPump->finalImage();
  stopPumps(false);
  if(action=="cancel") { close(); return; }
  if(image.isNull()) {
    finishing=false; status->setText("没有可用的截图，请重新框选区域");
    begin(live); return;
  }
  hide();
  if(output && !output(image,action)) {
    finishing=false; begin(live); return;
  }
  closing=true; close(); emit finished(true);
}
void ScrollCapture::closeEvent(QCloseEvent *event) {
  if((matchPump && matchPump->isRunning()) || (framePump && framePump->isRunning())) {
    if(!finishing) finish("cancel");
    else {
      pendingAction="cancel";
      if(framePump) framePump->requestStop();
      if(matchPump) matchPump->requestStop(false,false);
      hide();
    }
    event->ignore(); return;
  }
  timer.stop(); stopPumps(false); releaseHotkeys(); releaseInputHooks();
  if (!closing) { closing = true; emit finished(false); }
  event->accept();
}
bool ScrollCapture::nativeEvent(const QByteArray &, void *raw, qintptr *result) {
  auto message=static_cast<MSG *>(raw);
  if (message->message==WM_HOTKEY && hotkeys.contains(int(message->wParam))) {
    finish(message->wParam==2201 ? "cancel" : "copy"); *result=0; return true;
  }
  return false;
}
