#include "image_text.h"
#include "store.h"
#include <QtCore/QJsonArray>
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
  ImageTextLayout result;
  result.window = available;
  const int width = qBound(260, requestedWidth, qMax(260, available.width() - 160));
  if (collapsed) {
    result.viewport = QRect(QPoint(), available.size()).adjusted(12, 12, -12, -12);
    return result;
  }
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
  return result;
}
static QIcon textActionIcon(bool all, bool retry = false) {
  QPixmap pixmap(24,24); pixmap.fill(Qt::transparent);
  QPainter p(&pixmap); p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor("#555555"),1.3));
  if (retry) {
    p.drawArc(QRectF(5,5,14,14),35*16,290*16);
    p.drawPolyline(QPolygonF{{18,3},{19,8},{14,7}});
  } else {
    p.drawRoundedRect(QRectF(8,7,10,14),1,1);
    p.drawPolyline(QPolygonF{{14,5},{14,3},{4,3},{4,17},{6,17}});
    if (all) { p.drawLine(10,11,16,11); p.drawLine(10,14,16,14); p.drawLine(10,17,14,17); }
  }
  return QIcon(pixmap);
}
ImageTextOverlay::ImageTextOverlay(QWidget *parent, Store *s,
                                   std::function<QRect()> rect)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      store(s), imageRect(std::move(rect)), job(this) {
  setObjectName("imageTextOverlay");
  setWindowTitle("图片选字");
  setWindowModality(Qt::WindowModal);
  setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
  if (parent) parent->installEventFilter(this);
  setStyleSheet(
      "QFrame#imageTextControls{background:#f8f8f8;border:1px solid #e2e2e2;}"
      "QFrame#imageTextControls QLabel{color:#333;background:transparent;border:0;}"
      "QTextEdit#imageTextResult{background:transparent;border:0;color:#222;"
      "selection-background-color:#b6d8ff;selection-color:#172332;}"
      "QToolButton{color:#444;background:transparent;border:0;padding:5px;}"
      "QToolButton:hover{background:#e9e9e9;}QToolButton:disabled{color:#aaa;}"
      "QToolButton#imageTextExpand{background:#f8f8f8;border:1px solid #ddd;}");
  bar = new QFrame(this); bar->setObjectName("imageTextControls");
  auto root = new QVBoxLayout(bar);
  root->setContentsMargins(16,12,16,14); root->setSpacing(8);
  auto header = new QHBoxLayout; header->setSpacing(3);
  auto title = new QLabel("识别文字");
  auto titleFont = title->font(); titleFont.setPointSizeF(11);
  title->setFont(titleFont);
  header->addWidget(title); header->addStretch();
  auto button = [&](const QString &name, const QString &label, const QString &tip) {
    auto b = new QToolButton(bar); b->setObjectName(name);
    b->setText(label); b->setToolTip(tip); b->setFocusPolicy(Qt::NoFocus);
    return b;
  };
  allButton = button("imageTextSelectAll","全选","全选 · Ctrl+A");
  auto collapse = button("imageTextCollapse","›","收起文字面板");
  auto close = button("imageTextExit","×","退出文字识别 · Esc / F4");
  collapse->setFixedSize(28,28); close->setFixedSize(28,28);
  header->addWidget(allButton); header->addWidget(collapse); header->addWidget(close);
  root->addLayout(header);
  status = new QLabel(bar); status->setObjectName("imageTextStatus");
  status->setWordWrap(true); status->setMinimumHeight(20);
  status->setStyleSheet("color:#777;font-size:12px;");
  root->addWidget(status);
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
  root->addWidget(editor,1);
  auto zoomRow = new QHBoxLayout; zoomRow->setSpacing(4);
  auto hint = new QLabel("图片"); hint->setStyleSheet("color:#888;font-size:12px;");
  auto fit = button("imageTextFit","适应窗口","显示完整图片");
  auto actual = button("imageTextActualSize","100%","按原始像素查看");
  zoomRow->addWidget(hint); zoomRow->addStretch();
  zoomRow->addWidget(fit); zoomRow->addWidget(actual);
  root->addLayout(zoomRow);
  auto actions = new QHBoxLayout; actions->setSpacing(6);
  copyButton = button("imageTextCopy","复制选中","Ctrl+C");
  copyAllButton = button("imageTextCopyAll","复制全文","复制全部识别文字");
  auto retry = button("imageTextRetry","重试","重新识别当前原图");
  for (auto b : {copyButton,copyAllButton,retry}) {
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIcon(textActionIcon(b==copyAllButton,b==retry));
    b->setIconSize({22,22}); actions->addWidget(b,1);
  }
  root->addLayout(actions);
  divider = new QWidget(this); divider->setObjectName("imageTextDivider");
  divider->setCursor(Qt::SplitHCursor); divider->setStyleSheet("background:#e1e1e1;");
  divider->installEventFilter(this);
  expandButton = new QToolButton(this); expandButton->setObjectName("imageTextExpand");
  expandButton->setText("‹ 文字面板"); expandButton->setToolTip("展开文字面板 · Esc 退出");
  expandButton->setFocusPolicy(Qt::NoFocus);
  connect(expandButton,&QToolButton::clicked,this,[this]{setPanelCollapsed(false);});
  connect(collapse,&QToolButton::clicked,this,[this]{setPanelCollapsed(true);});
  connect(close,&QToolButton::clicked,this,&ImageTextOverlay::finish);
  connect(allButton,&QToolButton::clicked,this,&ImageTextOverlay::selectAll);
  connect(copyButton,&QToolButton::clicked,this,&ImageTextOverlay::copySelection);
  connect(copyAllButton,&QToolButton::clicked,this,&ImageTextOverlay::copyAll);
  connect(retry,&QToolButton::clicked,this,&ImageTextOverlay::recognize);
  connect(fit,&QToolButton::clicked,this,&ImageTextOverlay::fitImage);
  connect(actual,&QToolButton::clicked,this,[this] {
    const auto r=previewRect();
    if (r.width()>0) zoomAt(source.width()/(r.width()*devicePixelRatioF()),layout.viewport.center());
  });
  connect(editor,&QTextEdit::selectionChanged,this,&ImageTextOverlay::editorSelectionChanged);
  connect(&job,&PaddleOcrJob::finished,this,&ImageTextOverlay::setResult);
  connect(&job,&PaddleOcrJob::progress,this,[this](const QString &name,int done,int total) {
    if (name=="recognize") phase=QString("识别中 %1/%2").arg(done).arg(total);
    else if (name=="detect") phase="查找文字中";
    else if (name=="fallback") phase="切换 CPU 识别";
    else phase="准备识别中";
  });
  connect(&timer,&QTimer::timeout,this,[this] {
    status->setText(phase+QString(" · %1 秒").arg(elapsed.elapsed()/1000.,0,'f',1));
  });
  hide();
}
void ImageTextOverlay::begin(const QImage &image,bool autoRecognize) {
  if (image.isNull()) return;
  auto selectedProfile=store?store->config["ocr_profile"].toString("common"):QString("common");
  auto selectedAcceleration=store?store->config["ocr_acceleration"].toString("auto"):QString("auto");
  const bool cached=ready && image==source && profile==selectedProfile && acceleration==selectedAcceleration;
  job.cancel(); timer.stop();
  source=image; profile=selectedProfile; acceleration=selectedAcceleration;
  anchor=caret=-1; dragging=panning=space=false; active=true; collapsed=false;
  if (!cached) { ready=false; document={}; }
  updateEditor();
  editor->setPlaceholderText("等待识别结果…");
  place(); show(); raise(); activateWindow(); setFocus(Qt::OtherFocusReason);
  if (cached) { status->setText("拖选文字 · 两侧同步（已缓存）"); selectionChanged(); }
  else if (autoRecognize) recognize();
  else { status->setText("等待识别结果"); selectionChanged(); }
  update();
}
void ImageTextOverlay::recognize() {
  job.cancel(); ready=false; document={}; anchor=caret=-1;
  { QSignalBlocker blocker(editor); editor->clear(); }
  editor->setPlaceholderText("正在识别文字…");
  phase="准备识别中"; status->setText(phase); status->setToolTip({});
  elapsed.start(); timer.start(100); ++starts;
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
  timer.stop(); if (!active) return;
  anchor=caret=-1; dragging=false;
  if (result.contains("error")) {
    ready=false; document={};
    status->setText("识别失败 · 可重试"); status->setToolTip(result["error"].toString());
    editor->setPlaceholderText(result["error"].toString());
  } else {
    document.load(result); ready=true;
    status->setText(document.text.isEmpty()?"没有识别到文字 · 可重试":"拖选文字 · 两侧同步 · Ctrl+C 复制");
    status->setToolTip("图片滚轮缩放，按住空格拖动；Esc 返回");
    editor->setPlaceholderText("没有识别到文字");
  }
  updateEditor();
  selectionChanged();
}
void ImageTextOverlay::finish() {
  if (!active) return;
  active=false; dragging=panning=space=resizingPanel=false;
  job.cancel(); timer.stop(); hide(); emit closed();
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
  setGeometry(layout.window);
  bar->setGeometry(layout.panel); bar->setVisible(!collapsed);
  divider->setGeometry(layout.divider); divider->setVisible(!collapsed && !layout.divider.isEmpty());
  expandButton->setGeometry(qMax(0,width()-118),10,106,32);
  expandButton->setVisible(collapsed); expandButton->raise();
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
  copyButton->setEnabled(ready && anchor>=0 && caret>=0 && anchor!=caret);
  allButton->setEnabled(ready && !document.text.isEmpty());
  copyAllButton->setEnabled(ready && !document.text.isEmpty());
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
  status->setText(QString("已复制 %1 个字符 · 可继续选择").arg(text.size()));
}
void ImageTextOverlay::copyAll() {
  if (document.text.isEmpty()) return;
  QApplication::clipboard()->setText(document.text);
  status->setText("已复制全文");
}
void ImageTextOverlay::mousePressEvent(QMouseEvent *event) {
  if (event->button()!=Qt::LeftButton && event->button()!=Qt::MiddleButton) return;
  setFocus();
  if (!layout.viewport.contains(event->position().toPoint())) return;
  lastPointer=event->position();
  if (space || event->button()==Qt::MiddleButton) {
    panning=true; setCursor(Qt::ClosedHandCursor); return;
  }
  const auto position=ready?document.caretAt(toImage(event->position())):-1;
  if (!(event->modifiers()&Qt::ShiftModifier) || anchor<0) anchor=position;
  caret=position; dragging=position>=0; selectionChanged();
}
void ImageTextOverlay::mouseMoveEvent(QMouseEvent *event) {
  if (panning) {
    pan+=event->position()-lastPointer; lastPointer=event->position();
    clampPan(); update(); return;
  }
  const auto point=toImage(event->position());
  if (dragging) { caret=document.caretAt(point,true); selectionChanged(); }
  setCursor(space?Qt::OpenHandCursor:
      (ready && layout.viewport.contains(event->position().toPoint()) &&
       document.caretAt(point)>=0?Qt::IBeamCursor:Qt::ArrowCursor));
}
void ImageTextOverlay::mouseReleaseEvent(QMouseEvent *event) {
  if (panning) { panning=false; setCursor(space?Qt::OpenHandCursor:Qt::ArrowCursor); return; }
  if (event->button()!=Qt::LeftButton || !dragging) return;
  caret=document.caretAt(toImage(event->position()),true);
  dragging=false; selectionChanged();
}
void ImageTextOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
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
  menu.addAction(collapsed?"展开文字面板":"收起文字面板",this,[this]{setPanelCollapsed(!collapsed);});
  menu.addAction("适应窗口\tCtrl+0",this,&ImageTextOverlay::fitImage);
  menu.addSeparator(); menu.addAction("退出文字识别\tEsc",this,&ImageTextOverlay::finish);
  menu.exec(event->globalPos());
}
void ImageTextOverlay::wheelEvent(QWheelEvent *event) {
  if (layout.viewport.contains(event->position().toPoint()) && event->angleDelta().y())
    zoomAt(std::pow(1.15,event->angleDelta().y()/120.),event->position());
  event->accept();
}
