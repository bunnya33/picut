#include "image_text.h"
#include "store.h"
#include <QtCore/QJsonArray>
#include <QtCore/QMetaMethod>
#include <QtCore/QTextBoundaryFinder>
#include <QtGui/QClipboard>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtCore/QSignalBlocker>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPushButton>
#include <QtGui/QCursor>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

static QPolygonF readQuad(const QJsonValue &value) {
  QPolygonF quad;
  for (const auto &item : value.toArray()) {
    const auto point = item.toArray();
    if (point.size() != 2 || !std::isfinite(point[0].toDouble()) ||
        !std::isfinite(point[1].toDouble())) return {};
    quad << QPointF(point[0].toDouble(), point[1].toDouble());
  }
  return quad.size() == 4 ? quad : QPolygonF();
}
void ImageTextPage::load(const QJsonObject &result) {
  text.clear(); lines.clear();
  for (const auto &value : result["lines"].toArray()) {
    const auto object = value.toObject();
    ImageTextLine line;
    line.text = object["text"].toString();
    line.quad = readQuad(object["quad"]);
    if (line.text.isEmpty() || line.quad.isEmpty()) continue;
    QString reconstructed;
    for (const auto &item : object["characters"].toArray()) {
      const auto token = item.toObject();
      ImageTextGlyph glyph{token["text"].toString(), readQuad(token["quad"])};
      if (glyph.text.isEmpty() || glyph.quad.isEmpty()) continue;
      reconstructed += glyph.text;
      line.glyphs << glyph;
    }
    // Older engines can still select a complete line, without inventing
    // equally spaced character positions for mixed-width scripts.
    if (reconstructed != line.text) line.glyphs = {{line.text, line.quad}};
    lines << line;
  }
  std::stable_sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) {
    return a.quad.boundingRect().center().y() < b.quad.boundingRect().center().y();
  });
  int row = 0;
  for (int start = 0; start < lines.size();) {
    const auto bounds = lines[start].quad.boundingRect();
    int end = start + 1;
    while (end < lines.size()) {
      const auto next = lines[end].quad.boundingRect();
      if (qAbs(next.center().y() - bounds.center().y()) >
          qMin(next.height(), bounds.height()) * .45) break;
      ++end;
    }
    std::stable_sort(lines.begin() + start, lines.begin() + end,
                     [](const auto &a, const auto &b) {
                       return a.quad.boundingRect().left() < b.quad.boundingRect().left();
                     });
    for (int n = start; n < end; ++n) lines[n].row = row;
    ++row; start = end;
  }
  for (int n = 0; n < lines.size(); ++n) {
    auto &line = lines[n];
    if (n) text += line.row == lines[n - 1].row ? '\t' : '\n';
    line.offset = int(text.size());
    int offset = line.offset;
    for (auto &glyph : line.glyphs) {
      glyph.offset = offset;
      offset += int(glyph.text.size());
    }
    text += line.text;
  }
}
int ImageTextPage::caretAt(QPointF point, bool nearest) const {
  const ImageTextLine *line = nullptr;
  double distance = std::numeric_limits<double>::max();
  for (const auto &candidate : lines) {
    if (candidate.quad.containsPoint(point, Qt::OddEvenFill)) {
      line = &candidate; break;
    }
    if (!nearest) continue;
    auto r = candidate.quad.boundingRect();
    const double dx = std::max({r.left() - point.x(), 0., point.x() - r.right()}),
                 dy = std::max({r.top() - point.y(), 0., point.y() - r.bottom()});
    // Keep dragging between rows on the row nearest the pointer.
    const double score = dx * dx + dy * dy * 4;
    if (score < distance) { distance = score; line = &candidate; }
  }
  if (!line) return -1;
  int offset = line->offset;
  distance = std::numeric_limits<double>::max();
  for (const auto &glyph : line->glyphs) {
    const auto start = (glyph.quad[0] + glyph.quad[3]) / 2,
               end = (glyph.quad[1] + glyph.quad[2]) / 2;
    for (bool after : {false, true}) {
      const auto delta = point - (after ? end : start);
      const double score = QPointF::dotProduct(delta, delta);
      if (score < distance) {
        distance = score;
        offset = glyph.offset + (after ? int(glyph.text.size()) : 0);
      }
    }
  }
  return offset;
}
QList<QPolygonF> ImageTextPage::highlights(int first, int last) const {
  if (first > last) std::swap(first, last);
  QList<QPolygonF> result;
  if (first < 0 || first == last) return result;
  for (const auto &line : lines)
    for (const auto &glyph : line.glyphs)
      if (glyph.offset < last && glyph.offset + glyph.text.size() > first)
        result << glyph.quad;
  return result;
}
ImageTextLayout ImageTextLayout::arrange(QRect image, QRect available,
                                         int requestedWidth, bool collapsed) {
  const bool imageFits = available.contains(image);
  available.adjust(0, TitleHeight, 0, 0);
  if (imageFits && image.height() <= available.height() && image.top() < available.top())
    image.moveTop(available.top());
  ImageTextLayout result;
  result.window = available;
  const int width = qBound(260, requestedWidth, qMax(260, available.width() - 160));
  if (collapsed) {
    result.viewport = QRect(QPoint(), available.size()).adjusted(12, 12, -12, -12);
  } else {
    QRect panel;
    if (available.contains(image) && available.right() - image.right() >= width + 12) {
      result.placement = Right;
      panel = {image.right() + 13, available.top(), width,
               qMin(available.height(), qMax(400, image.height()))};
    } else if (available.contains(image) && image.left() - available.left() >= width + 12) {
      result.placement = Left;
      panel = {image.left() - width - 12, available.top(), width,
               qMin(available.height(), qMax(400, image.height()))};
    }
    if (!panel.isEmpty()) {
      panel.moveTop(qBound(available.top(), image.top(),
                          available.bottom() - panel.height() + 1));
      result.window = image.united(panel).adjusted(-8, -8, 8, 8).intersected(available);
      result.viewport = image.translated(-result.window.topLeft());
      result.panel = panel.translated(-result.window.topLeft());
    } else {
      result.panel = {qMax(1, available.width() - width), 0,
                      qMin(width, available.width() - 1), available.height()};
      result.viewport = QRect(12, 12, qMax(1, result.panel.left() - 24),
                              qMax(1, available.height() - 24));
      result.divider = {result.panel.left() - 5, 0, 6, available.height()};
    }
  }
  result.window.adjust(0, -TitleHeight, 0, 0);
  result.title = {0, 0, result.window.width(), TitleHeight};
  result.viewport.translate(0, TitleHeight);
  if (!result.panel.isEmpty()) result.panel.translate(0, TitleHeight);
  if (!result.divider.isEmpty()) result.divider.translate(0, TitleHeight);
  return result;
}
ImageTextOverlay::ImageTextOverlay(QWidget *parent, Store *s,
                                   std::function<QRect()> rect)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      store(s), imageRect(std::move(rect)), job(this) {
  setObjectName("imageTextOverlay");
  setWindowTitle("文字识别");
  setWindowModality(Qt::WindowModal);
  setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
  holdToMove.setSingleShot(true);
  holdToMove.setInterval(240);
  connect(&holdToMove, &QTimer::timeout, this, [this] {
    movingWindow = true;
    setCursor(Qt::ClosedHandCursor);
  });
  if (parent) parent->installEventFilter(this);
  setStyleSheet(
      "QFrame#imageTextControls{background:#f8f8f8;border:1px solid #e2e2e2;}"
      "QTextEdit#imageTextResult{background:white;border:1px solid #d8d8d8;color:#222;"
      "selection-background-color:#b6d8ff;selection-color:#172332;}"
      "QPushButton{color:#222;background:#fafafa;border:1px solid #c5c5c5;border-radius:3px;padding:5px 18px;}"
      "QPushButton:hover{background:#eaf3fc;border-color:#7eb4ea;}");
  bar = new QFrame(this); bar->setObjectName("imageTextControls");
  auto root = new QVBoxLayout(bar);
  root->setContentsMargins(10,10,10,10); root->setSpacing(8);
  editor = new QTextEdit(bar); editor->setObjectName("imageTextResult");
  editor->setAcceptRichText(false);
  editor->setReadOnly(true); editor->setUndoRedoEnabled(false);
  editor->setFrameShape(QFrame::NoFrame);
  editor->setPlaceholderText("正在识别文字…");
  QFont font("Microsoft YaHei UI"); font.setPointSizeF(10.5);
  editor->setFont(font); editor->document()->setDocumentMargin(2);
  editor->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
  auto palette = editor->palette();
  for (auto group : {QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
    palette.setColor(group,QPalette::Highlight,QColor("#b6d8ff"));
    palette.setColor(group,QPalette::HighlightedText,QColor("#172332"));
  }
  editor->setPalette(palette);
  editor->installEventFilter(this); editor->viewport()->installEventFilter(this);
  editor->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(editor,&QTextEdit::customContextMenuRequested,this,[this](QPoint point) {
    const auto global=editor->mapToGlobal(point);
    QContextMenuEvent event(QContextMenuEvent::Mouse,mapFromGlobal(global),global);
    contextMenuEvent(&event);
  });
  root->addWidget(editor,1);
  auto actions = new QHBoxLayout; actions->addStretch();
  auto cancel = new QPushButton("取消",bar); cancel->setObjectName("imageTextCancel");
  cancel->setFocusPolicy(Qt::NoFocus); actions->addWidget(cancel);
  root->addLayout(actions);
  connect(cancel,&QPushButton::clicked,this,&ImageTextOverlay::finish);
  divider = new QWidget(this); divider->setObjectName("imageTextDivider");
  divider->setCursor(Qt::SplitHCursor); divider->setStyleSheet("background:#e1e1e1;");
  divider->installEventFilter(this);
  connect(editor,&QTextEdit::selectionChanged,this,&ImageTextOverlay::editorSelectionChanged);
  connect(&job,&PaddleOcrJob::finished,this,&ImageTextOverlay::setResult);
  hide();
}
void ImageTextOverlay::begin(const QImage &image,bool autoRecognize) {
  if (image.isNull()) return;
  auto selectedProfile=store?store->config["ocr_profile"].toString("common"):QString("common");
  auto selectedAcceleration=store?store->config["ocr_acceleration"].toString("auto"):QString("auto");
  const bool cached=ready && image==source && profile==selectedProfile && acceleration==selectedAcceleration;
  job.cancel();
  source=image; profile=selectedProfile; acceleration=selectedAcceleration;
  anchor=caret=-1; dragging=panning=space=false; active=true; collapsed=false;
  holdToMove.stop(); movingWindow=false; windowOffset={};
  unsetCursor();
  if (!cached) { ready=false; document={}; }
  updateEditor();
  editor->setPlaceholderText("等待识别结果…");
  place(); show(); raise(); activateWindow(); setFocus(Qt::OtherFocusReason);
  if (!cached && autoRecognize) recognize();
  selectionChanged();
  update();
}
void ImageTextOverlay::recognize() {
  job.cancel(); ready=false; document={}; anchor=caret=-1;
  { QSignalBlocker blocker(editor); editor->clear(); }
  editor->setPlaceholderText("正在识别文字…");
  ++starts;
  selectionChanged(); job.start(source,profile,0,acceleration);
}
void ImageTextOverlay::updateEditor() {
  QSignalBlocker blocker(editor);
  editor->setPlainText(document.text);
  QTextCursor cursor(editor->document()); cursor.select(QTextCursor::Document);
  QTextBlockFormat format; format.setBottomMargin(8);
  format.setLineHeight(125,QTextBlockFormat::ProportionalHeight);
  cursor.mergeBlockFormat(format);
  editor->document()->setModified(false);
}
void ImageTextOverlay::setResult(const QJsonObject &result) {
  if (!active) return;
  anchor=caret=-1; dragging=false;
  if (result.contains("error")) {
    ready=false; document={};
    editor->setPlaceholderText(result["error"].toString());
  } else {
    document.load(result); ready=true;
    editor->setPlaceholderText("没有识别到文字");
  }
  updateEditor();
  selectionChanged();
}
void ImageTextOverlay::finish() {
  if (!active) return;
  active=false; dragging=panning=space=resizingPanel=false;
  holdToMove.stop(); movingWindow=false;
  job.cancel(); hide(); emit closed();
}
void ImageTextOverlay::closeEvent(QCloseEvent *event) { finish(); event->accept(); }
QRect ImageTextOverlay::sourceGlobalRect() const {
  auto rect=imageRect?imageRect():QRect();
  if (!rect.isEmpty())
    return parentWidget()?QRect(parentWidget()->mapToGlobal(rect.topLeft()),rect.size()):rect;
  auto screen=QGuiApplication::screenAt(QCursor::pos());
  if (!screen) screen=QGuiApplication::primaryScreen();
  const auto area=screen->availableGeometry();
  return {area.center()-QPoint(source.width()/2,source.height()/2),source.size()};
}
void ImageTextOverlay::place(bool resetView) {
  if (!active) return;
  const auto original=sourceGlobalRect();
  auto screen=QGuiApplication::screenAt(original.center());
  if (!screen) screen=parentWidget()?parentWidget()->screen():QGuiApplication::primaryScreen();
  layout=ImageTextLayout::arrange(original,screen->availableGeometry(),panelWidth,collapsed);
  setGeometry(layout.window.translated(windowOffset));
  bar->setGeometry(layout.panel); bar->setVisible(!collapsed);
  divider->setGeometry(layout.divider); divider->setVisible(!collapsed && !layout.divider.isEmpty());
  if (resetView) { zoom=1; pan={}; }
  clampPan(); update();
}
void ImageTextOverlay::setPanelCollapsed(bool value) {
  collapsed=value; place(); setFocus();
}
void ImageTextOverlay::fitImage() { zoom=1; pan={}; update(); }
QRectF ImageTextOverlay::previewRect() const {
  if (source.isNull()) return {};
  const auto viewport=QRectF(layout.viewport);
  const double scale=qMin(viewport.width()/source.width(),viewport.height()/source.height())*zoom;
  const QSizeF size(source.width()*scale,source.height()*scale);
  return {viewport.center()+pan-QPointF(size.width()/2,size.height()/2),size};
}
QPointF ImageTextOverlay::toImage(QPointF point) const {
  const auto r=previewRect();
  return {(point.x()-r.x())*source.width()/qMax(.001,r.width()),
          (point.y()-r.y())*source.height()/qMax(.001,r.height())};
}
QPointF ImageTextOverlay::imageToView(QPointF point) const {
  const auto r=previewRect();
  return {r.x()+point.x()*r.width()/qMax(1,source.width()),
          r.y()+point.y()*r.height()/qMax(1,source.height())};
}
void ImageTextOverlay::clampPan() {
  const auto r=previewRect(); const auto viewport=layout.viewport;
  const double x=qMax(0.,(r.width()-viewport.width())/2);
  const double y=qMax(0.,(r.height()-viewport.height())/2);
  pan={qBound(-x,pan.x(),x),qBound(-y,pan.y(),y)};
}
void ImageTextOverlay::zoomAt(double factor,QPointF point) {
  const auto before=toImage(point);
  zoom=qBound(.1,zoom*factor,40.);
  pan+=point-imageToView(before); clampPan(); update();
}
bool ImageTextOverlay::eventFilter(QObject *object,QEvent *event) {
  if (!active) return QWidget::eventFilter(object,event);
  if (object==parentWidget()) {
    if (event->type()==QEvent::Move || event->type()==QEvent::Resize) place();
    if (event->type()==QEvent::Hide || event->type()==QEvent::Close) finish();
  }
  if (object==divider) {
    if (event->type()==QEvent::MouseButtonPress) {
      auto mouse=static_cast<QMouseEvent *>(event);
      if (mouse->button()==Qt::LeftButton) {
        resizingPanel=true; resizeOrigin=mouse->globalPosition().toPoint(); resizeWidth=panelWidth;
        return true;
      }
    } else if (event->type()==QEvent::MouseMove && resizingPanel) {
      auto mouse=static_cast<QMouseEvent *>(event);
      panelWidth=qBound(260,resizeWidth+resizeOrigin.x()-qRound(mouse->globalPosition().x()),
                         qMax(260,qMin(600,int(layout.window.width()*.65))));
      place(false); return true;
    } else if (event->type()==QEvent::MouseButtonRelease && resizingPanel) {
      resizingPanel=false; return true;
    } else if (event->type()==QEvent::MouseButtonDblClick) {
      panelWidth=360; place(); return true;
    }
  }
  if (object==editor || object==editor->viewport()) {
    if (event->type()==QEvent::ShortcutOverride) {
      auto key=static_cast<QKeyEvent *>(event);
      if (key->key()==Qt::Key_Escape || key->key()==Qt::Key_F4 ||
          key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll)) {
        key->accept(); return true;
      }
    }
    if (event->type()==QEvent::KeyPress) {
      auto key=static_cast<QKeyEvent *>(event);
      if (key->key()==Qt::Key_Escape || key->key()==Qt::Key_F4) { finish(); return true; }
      if (key->matches(QKeySequence::Copy)) { copySelection(); return true; }
      if (key->matches(QKeySequence::SelectAll)) { selectAll(); return true; }
      if (key->key()==Qt::Key_0 && key->modifiers()&Qt::ControlModifier) { fitImage(); return true; }
      if (key->key()>=Qt::Key_F1 && key->key()<=Qt::Key_F12) return true;
    }
  }
  return QWidget::eventFilter(object,event);
}
void ImageTextOverlay::paintEvent(QPaintEvent *) {
  QPainter p(this); p.fillRect(rect(),QColor("#f1f1f1"));
  p.fillRect(layout.title,QColor("#f8f8f8"));
  p.setPen(QColor("#222222"));
  p.drawText(layout.title.adjusted(12,0,-12,0),Qt::AlignLeft | Qt::AlignVCenter,windowTitle());
  p.setPen(QColor("#dddddd"));
  p.drawLine(layout.title.bottomLeft(),layout.title.bottomRight());
  p.setPen(QColor("#dedede")); p.drawRect(rect().adjusted(0,0,-1,-1));
  p.setClipRect(layout.viewport);
  const auto r=previewRect();
  p.setRenderHint(QPainter::SmoothPixmapTransform,r.width()<source.width());
  p.drawImage(r,source);
  if (!ready || source.isNull()) return;
  p.translate(r.topLeft()); p.scale(r.width()/source.width(),r.height()/source.height());
  p.setPen(Qt::NoPen); p.setBrush(QColor(38,133,245,18));
  for (const auto &line:document.lines) p.drawPolygon(line.quad);
  p.setBrush(QColor(38,133,245,94));
  for (const auto &quad:document.highlights(anchor,caret)) p.drawPolygon(quad);
}
void ImageTextOverlay::selectionChanged(bool fromEditor) {
  if (syncing) return;
  syncing=true;
  {
    QSignalBlocker blocker(editor);
    auto cursor=editor->textCursor();
    const int end=int(document.text.size());
    cursor.setPosition(qBound(0,anchor,end));
    cursor.setPosition(qBound(0,caret,end),QTextCursor::KeepAnchor);
    if (!fromEditor) { editor->setTextCursor(cursor); editor->ensureCursorVisible(); }
    QList<QTextEdit::ExtraSelection> selections;
    if (cursor.hasSelection()) {
      QTextEdit::ExtraSelection selected;
      selected.cursor=cursor; selected.format.setBackground(QColor("#b6d8ff"));
      selected.format.setForeground(QColor("#172332")); selections<<selected;
    }
    editor->setExtraSelections(selections);
  }
  syncing=false; update();
}
void ImageTextOverlay::editorSelectionChanged() {
  if (syncing || !ready) return;
  const auto cursor=editor->textCursor();
  anchor=cursor.anchor(); caret=cursor.position();
  selectionChanged(true); ensureImageSelectionVisible();
}
void ImageTextOverlay::ensureImageSelectionVisible() {
  const auto quads=document.highlights(anchor,caret);
  if (quads.isEmpty()) return;
  const auto quad=caret>=anchor?quads.last():quads.first();
  const auto point=imageToView(quad.boundingRect().center());
  auto area=QRectF(layout.viewport);
  if (area.width()>60 && area.height()>60) area.adjust(20,20,-20,-20);
  pan+=QPointF(qBound(area.left(),point.x(),area.right())-point.x(),
                qBound(area.top(),point.y(),area.bottom())-point.y());
  clampPan(); update();
}
void ImageTextOverlay::selectAll() {
  if (!ready) return;
  anchor=0; caret=int(document.text.size()); selectionChanged();
}
QString ImageTextOverlay::selectedText() const {
  return anchor<0 || caret<0?QString():document.text.mid(qMin(anchor,caret),qAbs(anchor-caret));
}
void ImageTextOverlay::copySelection() {
  const auto text=selectedText();
  if (text.isEmpty()) return;
  QApplication::clipboard()->setText(text);
}
void ImageTextOverlay::copyAll() {
  if (document.text.isEmpty()) return;
  QApplication::clipboard()->setText(document.text);
}
void ImageTextOverlay::moveWindow(QPoint global) {
  auto next = global - windowDragOffset;
  auto screen = QGuiApplication::screenAt(global);
  if (!screen) screen = this->screen();
  const auto area = screen->availableGeometry();
  const int visibleWidth = qMin(120, width());
  next.setX(qBound(area.left() + visibleWidth - width(), next.x(), area.right() - visibleWidth + 1));
  next.setY(qBound(area.top(), next.y(), area.bottom() - ImageTextLayout::TitleHeight + 1));
  move(next);
  windowOffset = pos() - layout.window.topLeft();
}
void ImageTextOverlay::mousePressEvent(QMouseEvent *event) {
  if (event->button()!=Qt::LeftButton && event->button()!=Qt::MiddleButton) return;
  setFocus();
  if (event->button()==Qt::LeftButton && layout.title.contains(event->position().toPoint())) {
    windowDragOffset=event->globalPosition().toPoint()-pos();
    movingWindow=true;
    setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (!layout.viewport.contains(event->position().toPoint())) return;
  lastPointer=event->position();
  if (space || event->button()==Qt::MiddleButton) {
    panning=true; setCursor(Qt::ClosedHandCursor); return;
  }
  const auto position=ready?document.caretAt(toImage(event->position())):-1;
  if (position<0 && previewRect().contains(event->position())) {
    windowDragOffset=event->globalPosition().toPoint()-pos();
    holdToMove.start();
    return;
  }
  if (!(event->modifiers()&Qt::ShiftModifier) || anchor<0) anchor=position;
  caret=position; dragging=position>=0; selectionChanged();
}
void ImageTextOverlay::mouseMoveEvent(QMouseEvent *event) {
  if (movingWindow) {
    moveWindow(event->globalPosition().toPoint());
    return;
  }
  if (holdToMove.isActive()) return;
  if (panning) {
    pan+=event->position()-lastPointer; lastPointer=event->position();
    clampPan(); update(); return;
  }
  const auto point=toImage(event->position());
  if (dragging) { caret=document.caretAt(point,true); selectionChanged(); }
  if (space || layout.title.contains(event->position().toPoint()))
    setCursor(Qt::OpenHandCursor);
  else if (ready && layout.viewport.contains(event->position().toPoint()) && document.caretAt(point)>=0)
    setCursor(Qt::IBeamCursor);
  else
    unsetCursor();
}
void ImageTextOverlay::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button()==Qt::LeftButton && movingWindow) {
    moveWindow(event->globalPosition().toPoint());
    movingWindow=false;
    setCursor(layout.title.contains(event->position().toPoint()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    return;
  }
  if (event->button()==Qt::LeftButton && holdToMove.isActive()) {
    holdToMove.stop(); anchor=caret=-1; selectionChanged();
    return;
  }
  if (panning) { panning=false; setCursor(space?Qt::OpenHandCursor:Qt::ArrowCursor); return; }
  if (event->button()!=Qt::LeftButton || !dragging) return;
  caret=document.caretAt(toImage(event->position()),true);
  dragging=false; selectionChanged();
}
void ImageTextOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
  if (!layout.viewport.contains(event->position().toPoint())) return;
  holdToMove.stop();
  const auto point=toImage(event->position());
  int offset=-1;
  for (const auto &line:document.lines)
    for (const auto &glyph:line.glyphs)
      if (glyph.quad.containsPoint(point,Qt::OddEvenFill)) {
        if (line.glyphs.size()==1) {
          anchor=glyph.offset; caret=anchor+int(glyph.text.size());
          dragging=false; selectionChanged(); return;
        }
        offset=glyph.offset; break;
      }
  if (offset<0) return;
  QTextBoundaryFinder finder(QTextBoundaryFinder::Word,document.text);
  finder.setPosition(qMin(offset,int(document.text.size())-1));
  if (!finder.isAtBoundary()) finder.toPreviousBoundary();
  anchor=finder.position(); caret=finder.toNextBoundary(); dragging=false; selectionChanged();
}
void ImageTextOverlay::keyPressEvent(QKeyEvent *event) {
  if (event->key()==Qt::Key_Escape || event->key()==Qt::Key_F4) finish();
  else if (event->matches(QKeySequence::Copy)) copySelection();
  else if (event->matches(QKeySequence::SelectAll)) selectAll();
  else if (event->key()==Qt::Key_Space && !event->isAutoRepeat()) { space=true; setCursor(Qt::OpenHandCursor); }
  else if (event->key()==Qt::Key_0 && event->modifiers()&Qt::ControlModifier) fitImage();
  event->accept();
}
void ImageTextOverlay::keyReleaseEvent(QKeyEvent *event) {
  if (event->key()==Qt::Key_Space && !event->isAutoRepeat()) {
    space=false; panning=false; setCursor(Qt::ArrowCursor);
  }
  event->accept();
}
void ImageTextOverlay::contextMenuEvent(QContextMenuEvent *event) {
  QMenu menu(this); menu.setObjectName("imageTextMenu");
  menu.addAction("复制选中\tCtrl+C",this,&ImageTextOverlay::copySelection)->setEnabled(!selectedText().isEmpty());
  menu.addAction("复制全文",this,&ImageTextOverlay::copyAll)->setEnabled(!document.text.isEmpty());
  menu.addAction("全选\tCtrl+A",this,&ImageTextOverlay::selectAll)->setEnabled(!document.text.isEmpty());
  if (isSignalConnected(QMetaMethod::fromSignal(&ImageTextOverlay::translationRequested))) {
    auto translate=menu.addAction(selectedText().isEmpty()?"翻译图片文字（本地）":"翻译选中文字（本地）",this,[this] {
      emit translationRequested(source,translationSelection());
    });
    translate->setEnabled(ready && !document.text.isEmpty());
  }
  auto panel = menu.addAction("显示文字面板",this,[this]{setPanelCollapsed(!collapsed);});
  panel->setCheckable(true);
  panel->setChecked(!collapsed);
  menu.addAction("适应窗口\tCtrl+0",this,&ImageTextOverlay::fitImage);
  menu.addAction("100%",this,[this] {
    const auto r=previewRect();
    if (r.width()>0) zoomAt(source.width()/(r.width()*devicePixelRatioF()),layout.viewport.center());
  });
  menu.addAction("重新识别",this,&ImageTextOverlay::recognize);
  menu.addSeparator(); menu.addAction("退出文字识别\tEsc",this,&ImageTextOverlay::finish);
  menu.exec(event->globalPos());
}
QJsonObject ImageTextOverlay::translationSelection() const {
  QJsonArray lines;
  const bool selected=anchor>=0 && caret>=0 && anchor!=caret;
  const int first=qMin(anchor,caret),last=qMax(anchor,caret);
  for (const auto &line:document.lines) {
    QString text; QRectF bounds;
    for (const auto &glyph:line.glyphs) {
      if (selected && (glyph.offset>=last || glyph.offset+glyph.text.size()<=first)) continue;
      text+=glyph.text; bounds=bounds.isEmpty()?glyph.quad.boundingRect():bounds.united(glyph.quad.boundingRect());
    }
    if (text.isEmpty()) continue;
    QJsonArray quad;
    for (const auto point:{bounds.topLeft(),bounds.topRight(),bounds.bottomRight(),bounds.bottomLeft()})
      quad.append(QJsonArray{point.x(),point.y()});
    lines.append(QJsonObject{{"text",text},{"quad",quad}});
  }
  return {{"lines",lines}};
}
void ImageTextOverlay::wheelEvent(QWheelEvent *event) {
  if (layout.viewport.contains(event->position().toPoint()) && event->angleDelta().y())
    zoomAt(std::pow(1.15,event->angleDelta().y()/120.),event->position());
  event->accept();
}
