#include "recording_editor.h"
#include "recording_markup.h"
#include "native_windows.h"
#include "compact_ui.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QApplication>

RecordingEditor::RecordingEditor(Store *store, QRect rectangle, QSize canvas,
    const Annotation *initial, std::shared_ptr<RecordingLiveState> state, bool deferToolbar)
    : EditingSurface(store,nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint),
      region(rectangle),live(std::move(state)) {
  setObjectName("recordingEditor"); setAttribute(Qt::WA_TranslucentBackground);
  QRect desktop;
  for (auto screen:QGuiApplication::screens()) desktop=desktop.united(screen->geometry());
  setGeometry(desktop);
  QImage blank(canvas,QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::transparent);
  annotation=std::make_unique<Annotation>(blank,store);
  if (initial) {
    annotation->marks=initial->marks;
    // Normalize imported screenshot coordinates once to the fixed video canvas.
    const double sx=double(canvas.width())/initial->original.width(),sy=double(canvas.height())/initial->original.height();
    for (auto &mark:annotation->marks) {
      for (auto &point:mark.points) point={point.x()*sx,point.y()*sy};
      mark.width*=(sx+sy)/2; mark.fontSize*=(sx+sy)/2;
      mark.radius=qRound(mark.radius*(sx+sy)/2); mark.intensity=qRound(mark.intensity*(sx+sy)/2);
      if (mark.outlineWidth) mark.outlineWidth=qMax(1,qRound(mark.outlineWidth*(sx+sy)/2));
    }
  }
  annotation->chooseTool("pen"); annotation->refresh();
  editing=true;
  if (!deferToolbar) { showToolbar(); configureToolbar(); }
  hide();
  publishTimer.setInterval(16);
  connect(&publishTimer,&QTimer::timeout,this,&RecordingEditor::publish);
  publish();
}
void RecordingEditor::configureToolbar() {
  if (toolbarConfigured || !toolbar) return;
  toolbarConfigured=true;
  for (const auto &name:{"select_text","translate","scroll","record","pin","save","copy"})
    toolbar->findChild<QToolButton *>("action_"+QString(name))->hide();
  auto close=toolbar->findChild<QToolButton *>("action_cancel");
  close->setToolTip("关闭标记工具栏，恢复操作桌面 · Esc");
  auto add=[this](const QString &name,const QString &title,const QString &icon,auto callback) {
    auto button=new QToolButton(toolbar->mainBar); button->setObjectName(name);
    button->setToolTip(title); button->setIcon(recordingIcon(icon)); button->setFixedSize(32,32);
    toolbar->mainBar->layout()->addWidget(button);
    connect(button,&QToolButton::clicked,this,callback);
  };
  add("recordingDeleteMark","删除选中标记 · Delete","trash",[this] {
    if (annotation->selected>=0) {
      annotation->checkpoint(); annotation->marks.removeAt(annotation->selected);
      annotation->confirm(); annotation->refresh(); publish(); update();
    }
  });
  add("recordingClearMarks","清空全部标记","clear",[this] { complete("clear"); });
  toolbar->mainBar->layout()->removeWidget(close); toolbar->mainBar->layout()->addWidget(close);
  // Keep only recording-related actions in this editor's overflow menu.
  disconnect(toolbar,&Toolbar::actionRequested,this,nullptr);
  connect(toolbar,&Toolbar::actionRequested,this,[this](const QString &action) {
    if (action=="undo") { commitText(); annotation->undo(); }
    else if (action=="redo") { commitText(); annotation->redo(); }
    else if (action=="more") {
      QMenu menu(this);
      menu.addAction("清空全部标记",this,[this] {complete("clear");});
      menu.addAction("关闭标记工具栏 · Esc",this,&RecordingEditor::finish);
      menu.exec(QCursor::pos());
    } else complete(action);
    toolbar->sync(*annotation); publish(); update();
  });
  positionToolbar();
}
void RecordingEditor::setRegion(QRect value) {
  if (region==value) return;
  commitText(); region=value;
  if (isVisible()) { positionToolbar(); update(); }
}
void RecordingEditor::begin() {
  const bool windows=QGuiApplication::platformName()=="windows";
  if (windows && !excludeCapture(this)) { emit editingFinished(); return; }
  qApp->installEventFilter(this);
  editing=true; show(); showToolbar(); configureToolbar(); positionToolbar();
  if (windows && !excludeCapture(this)) {
    qApp->removeEventFilter(this); hide(); emit editingFinished(); return;
  }
  raise(); activateWindow(); setFocus(); publishTimer.start(); update();
}
void RecordingEditor::finish() {
  commitText();
  if (annotation->draft) { auto mark=*annotation->draft; annotation->draft.reset(); annotation->add(mark); }
  annotation->confirm(); dragOriginal.reset(); dragHandle.clear();
  publish(); annotation->savePreferences(store); publishTimer.stop(); qApp->removeEventFilter(this); hide(); emit editingFinished();
}
bool RecordingEditor::eventFilter(QObject *watched,QEvent *event) {
  // Menus and combo popups have their own native windows; the editor's
  // capture affinity does not automatically exclude them.
  if (event->type()==QEvent::Show && QGuiApplication::platformName()=="windows") {
    auto widget=qobject_cast<QWidget *>(watched);
    if (widget && widget!=this && widget->isWindow() && isAncestorOf(widget)) excludeCapture(widget);
  }
  return EditingSurface::eventFilter(watched,event);
}
bool RecordingEditor::complete(const QString &action) {
  if (action=="cancel") { finish(); return true; }
  if (action=="clear") {
    commitText(); annotation->checkpoint(); annotation->marks.clear(); annotation->draft.reset();
    annotation->confirm(); annotation->refresh(); if (toolbar) toolbar->sync(*annotation); publish(); update(); return true;
  }
  return false;
}
void RecordingEditor::publish() {
  if (toolbarConfigured) {
    toolbar->findChild<QToolButton *>("recordingDeleteMark")->setEnabled(annotation->selected>=0);
    toolbar->findChild<QToolButton *>("recordingClearMarks")->setEnabled(!annotation->marks.isEmpty() || annotation->draft.has_value());
  }
  auto marks=annotation->marks;
  if (annotation->draft) marks.append(*annotation->draft);
  if (textEntry && textEntry->isVisible()) {
    const auto value=textEntry->value();
    if (numberLabelIndex>=0 && numberLabelIndex<marks.size()) marks[numberLabelIndex]=value;
    else if (!value.text.isEmpty()) marks.append(value);
  }
  QJsonArray serialized;
  for (const auto &mark:marks) serialized.append(mark.json());
  const auto document=QJsonDocument(serialized).toJson(QJsonDocument::Compact);
  if (document==lastDocument) return;
  lastDocument=document;
  Annotation snapshot(annotation->original); snapshot.marks=std::move(marks);
  live->setPainter(recordingPainter(snapshot));
  preview=snapshot.render(); emit artworkChanged(preview); update();
}
int RecordingEditor::markAt(QPoint point) const {
  const auto image=imagePoint(point);
  const double tolerance=5.0*annotation->original.width()/qMax(1,contentRect().width());
  for (int n=int(annotation->marks.size())-1;n>=0;--n)
    if (paintedHit(annotation->marks[n],image,tolerance)) return n;
  return -1;
}
void RecordingEditor::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setCompositionMode(QPainter::CompositionMode_Source); p.fillRect(rect(),Qt::transparent);
  // A nonzero alpha lets blank parts of the editing area receive input.
  p.fillRect(contentRect(),QColor(0,0,0,1)); p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  p.setClipRect(contentRect()); p.drawImage(contentRect(),preview); p.setClipping(false);
  paintEditing(p);
}
void RecordingEditor::mousePressEvent(QMouseEvent *event) {
  if (event->button()==Qt::RightButton) { annotation->confirm(); update(); return; }
  if (event->button()!=Qt::LeftButton) return;
  setFocus();
  if (!contentRect().contains(event->position().toPoint())) { commitText(); publish(); return; }
  // Recording marks remain editable after confirmation, unlike screenshot marks.
  if (editTarget(event->position().toPoint()).isEmpty() && annotation->tool!="eraser") {
    const int index=markAt(event->position().toPoint());
    if (index>=0) {
      commitText(); const auto mark=annotation->marks[index];
      annotation->chooseTool(mark.kind); annotation->preferences=mark; annotation->selected=index;
      toolbar->sync(*annotation);
      // Freehand strokes have no resize box, but can be moved as a whole.
      if (mark.kind=="pen" && !mark.straight) {
        dragHandle="move"; dragOrigin=imagePoint(event->position().toPoint());
        dragOriginal=mark; annotation->checkpoint(); update(); return;
      }
    }
  }
  annotationPress(event); publish(); update();
}
void RecordingEditor::mouseMoveEvent(QMouseEvent *event) { annotationMove(event); publish(); }
void RecordingEditor::mouseReleaseEvent(QMouseEvent *event) { annotationRelease(event); publish(); }
void RecordingEditor::wheelEvent(QWheelEvent *event) { annotationWheel(event); publish(); }
void RecordingEditor::keyPressEvent(QKeyEvent *event) {
  if (event->key()==Qt::Key_Escape) { finish(); return; }
  if (event->key()==Qt::Key_F4 || event->key()==Qt::Key_F10) return;
  annotationKey(event); publish();
}
