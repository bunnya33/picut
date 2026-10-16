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
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
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
ImageTextOverlay::ImageTextOverlay(QWidget *parent, Store *s,
                                   std::function<QRect()> rect, bool floatingBar)
    : QWidget(parent), store(s), imageRect(std::move(rect)), job(this) {
  setObjectName("imageTextOverlay");
  setAttribute(Qt::WA_TranslucentBackground);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  parent->installEventFilter(this);
  bar = new QFrame(this, floatingBar ? Qt::Tool | Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint : Qt::Widget);
  bar->setObjectName("imageTextControls");
  if (floatingBar) {
    bar->setWindowFlag(Qt::WindowDoesNotAcceptFocus);
    bar->setAttribute(Qt::WA_ShowWithoutActivating);
  }
  bar->setStyleSheet("QFrame#imageTextControls{background:#f2f2f2;border:1px solid #2080f0;}"
                      "QLabel{color:#333;border:0;background:transparent;}"
                      "QToolButton{color:#222;background:transparent;border:0;padding:6px;}"
                      "QToolButton:hover{background:#dcecff;}QToolButton:disabled{color:#888;}");
  auto row = new QHBoxLayout(bar);
  row->setContentsMargins(6, 2, 4, 2); row->setSpacing(2);
  auto title = new QLabel("图片选字", bar);
  title->setContentsMargins(0, 0, 8, 0);
  row->addWidget(title);
  status = new QLabel;
  status->setObjectName("imageTextStatus");
  status->setFixedWidth(205);
  row->addWidget(status);
  auto button = [&](const QString &name, const QString &title, const QString &tip) {
    auto b = new QToolButton;
    b->setObjectName(name); b->setText(title); b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus); row->addWidget(b); return b;
  };
  allButton = button("imageTextSelectAll", "全选", "Ctrl+A");
  copyButton = button("imageTextCopy", "复制", "复制选中文字 · Ctrl+C");
  fullButton = button("imageTextFull", "全文", "查看与修正识别全文");
  auto retry = button("imageTextRetry", "重试", "重新识别当前图片");
  auto close = button("imageTextExit", "×", "退出图片选字 · Esc / F4");
  connect(allButton, &QToolButton::clicked, this, &ImageTextOverlay::selectAll);
  connect(copyButton, &QToolButton::clicked, this, &ImageTextOverlay::copySelection);
  connect(fullButton, &QToolButton::clicked, this, &ImageTextOverlay::showFullText);
  connect(retry, &QToolButton::clicked, this, &ImageTextOverlay::recognize);
  connect(close, &QToolButton::clicked, this, &ImageTextOverlay::finish);
  connect(&job, &PaddleOcrJob::finished, this, &ImageTextOverlay::setResult);
  connect(&job, &PaddleOcrJob::progress, this, [this](const QString &name, int done, int total) {
    if (name == "recognize") phase = QString("识别中 %1/%2").arg(done).arg(total);
    else if (name == "detect") phase = "查找文字中";
    else if (name == "fallback") phase = "切换 CPU 识别";
    else phase = "准备识别中";
  });
  connect(&timer, &QTimer::timeout, this, [this] {
    status->setText(phase + QString(" · %1秒").arg(elapsed.elapsed() / 1000., 0, 'f', 1));
  });
  hide(); bar->hide();
}
void ImageTextOverlay::begin(const QImage &image, bool autoRecognize) {
  auto selectedProfile = store ? store->config["ocr_profile"].toString("common") : QString("common");
  auto selectedAcceleration = store ? store->config["ocr_acceleration"].toString("auto") : QString("auto");
  const bool cached = ready && image == source && profile == selectedProfile && acceleration == selectedAcceleration;
  job.cancel(); timer.stop();
  source = image; profile = selectedProfile; acceleration = selectedAcceleration;
  anchor = caret = -1; dragging = false; active = true;
  if (!cached) { ready = false; document = {}; }
  setGeometry(parentWidget()->rect()); show(); raise();
  place(); bar->show(); bar->raise(); setFocus(Qt::OtherFocusReason);
  if (cached) {
    status->setText("拖选文字 · Ctrl+C 复制（已缓存）"); selectionChanged();
  } else if (autoRecognize) recognize();
  else { status->setText("等待识别结果"); selectionChanged(); }
  update();
}
void ImageTextOverlay::recognize() {
  job.cancel(); ready = false; document = {}; anchor = caret = -1;
  phase = "准备识别中"; status->setText(phase);
  elapsed.start(); timer.start(100); ++starts;
  selectionChanged(); update();
  job.start(source, profile, 0, acceleration);
}
void ImageTextOverlay::setResult(const QJsonObject &result) {
  timer.stop();
  if (!active) return;
  anchor = caret = -1; dragging = false;
  if (result.contains("error")) {
    ready = false; document = {};
    status->setText("识别失败 · 可重试"); status->setToolTip(result["error"].toString());
  } else {
    document.load(result); ready = true;
    status->setText(document.text.isEmpty() ? "没有识别到文字 · 可重试" : "拖选文字 · Ctrl+C 复制");
    status->setToolTip("本机离线识别；拖动选择，双击选择词，Ctrl+A 全选，Esc 返回");
  }
  selectionChanged(); update();
}
void ImageTextOverlay::finish() {
  if (!active) return;
  active = false; dragging = false; job.cancel(); timer.stop();
  bar->hide(); hide(); emit closed();
}
void ImageTextOverlay::place() {
  if (!active) return;
  setGeometry(parentWidget()->rect());
  bar->adjustSize();
  const auto r = imageRect();
  auto screen = QGuiApplication::screenAt(mapToGlobal(r.center()));
  if (!screen) screen = QGuiApplication::primaryScreen();
  QRect available(mapFromGlobal(screen->availableGeometry().topLeft()), screen->availableGeometry().size());
  if (!bar->isWindow()) available = available.intersected(rect());
  available.adjust(4, 4, -4, -4);
  int x = qBound(available.left(), r.right() - bar->width() + 1,
                 qMax(available.left(), available.right() - bar->width() + 1));
  int y = r.bottom() + 7;
  if (y + bar->height() > available.bottom()) y = r.top() - bar->height() - 7;
  y = qBound(available.top(), y, qMax(available.top(), available.bottom() - bar->height() + 1));
  bar->move(bar->isWindow() ? mapToGlobal(QPoint(x, y)) : QPoint(x, y));
  update();
}
bool ImageTextOverlay::eventFilter(QObject *object, QEvent *event) {
  if (object == parentWidget() && active) {
    if (event->type() == QEvent::Move || event->type() == QEvent::Resize) place();
    if (event->type() == QEvent::Hide || event->type() == QEvent::Close) finish();
  }
  return QWidget::eventFilter(object, event);
}
QPointF ImageTextOverlay::toImage(QPointF point) const {
  auto r = imageRect();
  return {(point.x() - r.x()) * source.width() / qMax(1, r.width()),
           (point.y() - r.y()) * source.height() / qMax(1, r.height())};
}
void ImageTextOverlay::paintEvent(QPaintEvent *) {
  QPainter p(this);
  const auto r = imageRect();
  p.setClipRect(r);
  p.drawImage(r, source);
  if (!ready || source.isNull()) return;
  p.translate(r.topLeft());
  p.scale(double(r.width()) / source.width(), double(r.height()) / source.height());
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(38, 133, 245, 22));
  for (const auto &line : document.lines) p.drawPolygon(line.quad);
  p.setBrush(QColor(38, 133, 245, 94));
  for (const auto &quad : document.highlights(anchor, caret)) p.drawPolygon(quad);
}
void ImageTextOverlay::selectionChanged() {
  copyButton->setEnabled(ready && anchor >= 0 && caret >= 0 && anchor != caret);
  allButton->setEnabled(ready && !document.text.isEmpty());
  fullButton->setEnabled(ready && !document.text.isEmpty());
  update();
}
void ImageTextOverlay::selectAll() {
  if (!ready) return;
  anchor = 0; caret = int(document.text.size()); selectionChanged();
}
QString ImageTextOverlay::selectedText() const {
  if (anchor < 0 || caret < 0) return {};
  return document.text.mid(qMin(anchor, caret), qAbs(anchor - caret));
}
void ImageTextOverlay::copySelection() {
  auto text = selectedText();
  if (!text.isEmpty()) {
    QApplication::clipboard()->setText(text);
    status->setText(QString("已复制 %1 个字符 · 可继续选择").arg(text.size()));
  }
}
void ImageTextOverlay::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton) return;
  setFocus();
  int position = ready ? document.caretAt(toImage(event->position())) : -1;
  if (!(event->modifiers() & Qt::ShiftModifier) || anchor < 0) anchor = position;
  caret = position; dragging = position >= 0; selectionChanged();
}
void ImageTextOverlay::mouseMoveEvent(QMouseEvent *event) {
  auto point = toImage(event->position());
  if (dragging) { caret = document.caretAt(point, true); selectionChanged(); }
  setCursor(ready && document.caretAt(point) >= 0 ? Qt::IBeamCursor : Qt::ArrowCursor);
}
void ImageTextOverlay::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || !dragging) return;
  caret = document.caretAt(toImage(event->position()), true);
  dragging = false; selectionChanged();
}
void ImageTextOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
  // Use the glyph under the pointer, not the nearest caret. The right half of
  // the last letter otherwise resolves to the following word boundary/space.
  const auto point = toImage(event->position());
  int offset = -1;
  for (const auto &line : document.lines)
    for (const auto &glyph : line.glyphs)
      if (glyph.quad.containsPoint(point, Qt::OddEvenFill)) {
        if (line.glyphs.size() == 1) {
          anchor = glyph.offset; caret = anchor + int(glyph.text.size());
          dragging = false; selectionChanged(); return;
        }
        offset = glyph.offset;
        break;
      }
  if (offset < 0) return;
  QTextBoundaryFinder finder(QTextBoundaryFinder::Word, document.text);
  finder.setPosition(qMin(offset, int(document.text.size()) - 1));
  if (!finder.isAtBoundary()) finder.toPreviousBoundary();
  anchor = finder.position(); caret = finder.toNextBoundary();
  dragging = false; selectionChanged();
}
void ImageTextOverlay::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F4) finish();
  else if (event->matches(QKeySequence::Copy)) copySelection();
  else if (event->matches(QKeySequence::SelectAll)) selectAll();
  // Other screenshot/annotation shortcuts stay suspended while selecting text.
  event->accept();
}
void ImageTextOverlay::contextMenuEvent(QContextMenuEvent *event) {
  QMenu menu(this);
  menu.setObjectName("imageTextMenu");
  menu.addAction("复制选中\tCtrl+C", this, &ImageTextOverlay::copySelection)->setEnabled(!selectedText().isEmpty());
  menu.addAction("复制全部", this, [this] { QApplication::clipboard()->setText(document.text); })->setEnabled(!document.text.isEmpty());
  menu.addAction("全选\tCtrl+A", this, &ImageTextOverlay::selectAll)->setEnabled(!document.text.isEmpty());
  menu.addAction("查看 / 编辑全文", this, &ImageTextOverlay::showFullText)->setEnabled(!document.text.isEmpty());
  menu.addSeparator();
  menu.addAction("退出图片选字\tEsc", this, &ImageTextOverlay::finish);
  menu.exec(event->globalPos());
}
void ImageTextOverlay::showFullText() {
  QDialog dialog(this);
  dialog.setWindowTitle("图片选字 · 识别全文"); dialog.resize(600, 420);
  auto root = new QVBoxLayout(&dialog);
  auto editor = new QPlainTextEdit(document.text);
  editor->setObjectName("imageTextFullEditor"); root->addWidget(editor);
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  auto copy = buttons->addButton("复制全文", QDialogButtonBox::ActionRole);
  connect(copy, &QPushButton::clicked, editor, [editor] { QApplication::clipboard()->setText(editor->toPlainText()); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  root->addWidget(buttons); dialog.exec(); setFocus();
}
void ImageTextOverlay::wheelEvent(QWheelEvent *event) {
  // Pin zoom stays usable. The child image/selection share the same new scale.
  if (bar->isWindow()) {
    QWheelEvent forwarded(event->position(), event->globalPosition(), event->pixelDelta(),
                           event->angleDelta(), event->buttons(), event->modifiers(),
                           event->phase(), event->inverted());
    if (auto parent = parentWidget(); !parent->property("imageTextWasEditing").toBool())
      QApplication::sendEvent(parent, &forwarded);
  }
  event->accept();
}
