#include "image_translation.h"
#include "compact_ui.h"
#include "store.h"
#include "toolbar.h"
#include <QtCore/QSignalBlocker>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QTextCursor>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <cmath>

class TranslationCanvas : public QWidget {
public:
  QImage image;
  QList<QRect> blocks;
  std::function<void(int)> selected;
  int selectedIndex = -1;
  double zoom = 1;
  QPointF pan, last;
  bool panning = false;
  explicit TranslationCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("translationCanvas"); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setMinimumSize(100, 80);
  }
  QRectF preview() const {
    if (image.isNull()) return {};
    const auto area = QRectF(rect()).adjusted(12, 12, -12, -12);
    const double scale = qMin(area.width() / image.width(), area.height() / image.height()) * zoom;
    const QSizeF size(image.width() * scale, image.height() * scale);
    return {area.center() + pan - QPointF(size.width()/2, size.height()/2), size};
  }
  QPointF imageToView(QPointF point) const {
    const auto r = preview();
    return r.topLeft() + QPointF(point.x() * r.width() / qMax(1, image.width()), point.y() * r.height() / qMax(1, image.height()));
  }
  void resetView() { zoom = 1; pan = {}; update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this); p.fillRect(rect(), QColor("#e9e9e9"));
    if (image.isNull()) return;
    const auto r = preview(); p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, image);
    if (selectedIndex >= 0 && selectedIndex < blocks.size()) {
      const auto b = blocks[selectedIndex];
      p.setBrush(QColor(32,128,240,25)); p.setPen(QPen(QColor("#2080f0"), 1));
      p.drawRect(QRectF(imageToView(b.topLeft()), imageToView(b.bottomRight())));
    }
  }
  void mousePressEvent(QMouseEvent *e) override {
    setFocus();
    if (e->button() == Qt::MiddleButton) { panning = true; last = e->position(); setCursor(Qt::ClosedHandCursor); return; }
    if (e->button() != Qt::LeftButton || image.isNull()) return;
    const auto r = preview(); const auto point = e->position() - r.topLeft();
    const QPointF pos(point.x()*image.width()/qMax(1.,r.width()), point.y()*image.height()/qMax(1.,r.height()));
    int found = -1;
    for (int n = 0; n < blocks.size(); ++n) if (blocks[n].contains(pos.toPoint())) { found = n; break; }
    if (selected) selected(found);
  }
  void mouseMoveEvent(QMouseEvent *e) override {
    if (!panning) return;
    pan += e->position() - last; last = e->position(); update();
  }
  void mouseReleaseEvent(QMouseEvent *) override { panning = false; unsetCursor(); }
  void wheelEvent(QWheelEvent *e) override {
    if (image.isNull()) return;
    const double previous = zoom;
    zoom = qBound(1., zoom * std::pow(1.2, e->angleDelta().y()/120.), 8.);
    const auto origin = QPointF(rect().center());
    pan = (pan + origin - e->position()) * (zoom/previous) - origin + e->position();
    if (zoom == 1) pan = {};
    update(); e->accept();
  }
};

ImageTranslationOverlay::ImageTranslationOverlay(QWidget *parent, Store *s, std::function<QRect()> rect)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      store(s), imageRect(std::move(rect)), ocr(this), translator(this) {
  setObjectName("imageTranslationOverlay"); setWindowTitle("picut · 图片翻译试用");
  setWindowModality(Qt::WindowModal); setFocusPolicy(Qt::StrongFocus);
  if (parent) parent->installEventFilter(this);
  setStyleSheet(compactToolbarStyle() +
      "QWidget#imageTranslationOverlay{background:#f2f2f2;}"
      "QComboBox{background:white;color:#333;border:1px solid #c5c5c5;padding:3px;min-height:22px;}"
      "QPlainTextEdit{background:white;color:#222;border:1px solid #ccc;padding:10px;selection-background-color:#b6d8ff;}"
      "QLabel{color:#444;background:transparent;}");
  auto root = new QVBoxLayout(this); root->setContentsMargins(6,6,6,6); root->setSpacing(5);
  auto body = new QHBoxLayout; body->setSpacing(6);
  canvas = new TranslationCanvas(this); body->addWidget(canvas, 1);
  editor = new QPlainTextEdit(this); editor->setObjectName("translationText"); editor->setReadOnly(true);
  editor->setFixedWidth(320); editor->hide(); body->addWidget(editor); root->addLayout(body, 1);
  status = new QLabel; status->setObjectName("translationStatus"); status->setWordWrap(true); root->addWidget(status);
  auto footer = new QHBoxLayout; footer->setContentsMargins(0,0,0,0); footer->addStretch();
  auto bar = new QFrame; bar->setObjectName("annotationToolbar");
  auto row = new QHBoxLayout(bar); row->setContentsMargins(5,2,5,2); row->setSpacing(3);
  sourceLanguage = new QComboBox; sourceLanguage->setObjectName("translationSource");
  sourceLanguage->addItem("自动（中／英）", "auto"); sourceLanguage->addItem("英文", "en"); sourceLanguage->addItem("中文", "zh");
  targetLanguage = new QComboBox; targetLanguage->setObjectName("translationTarget");
  targetLanguage->addItem("简体中文", "zh"); targetLanguage->addItem("英文", "en");
  row->addWidget(sourceLanguage); row->addWidget(new QLabel("→")); row->addWidget(targetLanguage);
  const auto button = [&](QString name, QString icon, QString title, bool text = false) {
    auto b = new QToolButton; b->setObjectName("translation_"+name); b->setToolTip(title);
    b->setIcon(toolIcon(icon)); b->setIconSize({16,16}); b->setFixedHeight(32);
    if (text) { b->setText(title); b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); b->setMinimumWidth(58); }
    else b->setFixedWidth(32);
    row->addWidget(b); return b;
  };
  retry = button("retry", "translate", "翻译", true);
  compare = button("original", "select_text", "原文", true); compare->setCheckable(true);
  panelToggle = button("text", "text", "文字", true); panelToggle->setCheckable(true);
  copy = button("copy", "copy", "复制当前图片 · Ctrl+C");
  save = button("save", "save", "保存当前图片 · Ctrl+S");
  auto cancel = button("cancel", "cancel", "取消 · Esc");
  footer->addWidget(bar); footer->addStretch(); root->addLayout(footer);
  canvas->selected = [this](int index) { selectBlock(index); };
  connect(cancel, &QToolButton::clicked, this, &ImageTranslationOverlay::finish);
  connect(retry, &QToolButton::clicked, this, &ImageTranslationOverlay::translate);
  connect(compare, &QToolButton::toggled, this, [this] { refresh(); refreshEditor(); });
  connect(panelToggle, &QToolButton::toggled, this, &ImageTranslationOverlay::setPanelVisible);
  connect(copy, &QToolButton::clicked, this, &ImageTranslationOverlay::copyImage);
  connect(save, &QToolButton::clicked, this, &ImageTranslationOverlay::saveImage);
  connect(editor, &QPlainTextEdit::selectionChanged, this, &ImageTranslationOverlay::editorSelectionChanged);
  connect(sourceLanguage, &QComboBox::currentIndexChanged, this, [this] { if (ready) translate(); });
  connect(targetLanguage, &QComboBox::currentIndexChanged, this, [this] { if (ready) translate(); });
  connect(&ocr, &PaddleOcrJob::finished, this, [this](QJsonObject value) { setOcrResult(value); });
  connect(&ocr, &PaddleOcrJob::progress, this, [this](QString phase, int done, int total) {
    if (!active) return;
    status->setText(phase == "recognizing" ? QString("正在识别文字 %1 / %2…").arg(done).arg(total) : QString("正在识别图片文字…"));
  });
  renderTimer.setSingleShot(true); renderTimer.setInterval(100);
  connect(&renderTimer, &QTimer::timeout, this, &ImageTranslationOverlay::refresh);
  connect(&translator, &LocalTranslationJob::blockFinished, this, [this](QJsonObject block) {
    if (!active) return; document.apply(block); if (!renderTimer.isActive()) renderTimer.start();
  });
  connect(&translator, &LocalTranslationJob::progress, this, [this](QString phase, int done, int total) {
    if (active) status->setText(phase == "loading" ? "正在加载本地翻译模型…" : QString("正在翻译 %1 / %2…").arg(done).arg(total));
  });
  connect(&translator, &LocalTranslationJob::finished, this, &ImageTranslationOverlay::setTranslationResult);
}
ImageTranslationOverlay::~ImageTranslationOverlay() {
  if (parentWidget()) parentWidget()->removeEventFilter(this);
  ocr.cancel(); translator.cancel();
}
void ImageTranslationOverlay::begin(const QImage &image, bool recognize) {
  if (image.isNull()) return;
  const auto profile = store ? store->config["ocr_profile"].toString("common") : QString("common");
  const auto acceleration = store ? store->config["ocr_acceleration"].toString("auto") : QString("auto");
  const bool cached = ready && image == source && cachedProfile == profile && cachedAcceleration == acceleration;
  ocr.cancel(); translator.cancel(); renderTimer.stop();
  cachedProfile = profile; cachedAcceleration = acceleration;
  source = image; source.setDevicePixelRatio(1); translated = source;
  if (!cached) { document = {}; cache.clear(); completedKey.clear(); ready = false; }
  active = true;
  { QSignalBlocker s(sourceLanguage), t(targetLanguage);
    sourceLanguage->setCurrentIndex(qMax(0, sourceLanguage->findData(store ? store->config["translation_source"].toString("auto") : "auto")));
    targetLanguage->setCurrentIndex(qMax(0, targetLanguage->findData(store ? store->config["translation_target"].toString("zh") : "zh")));
  }
  compare->setChecked(false); compare->setEnabled(false); retry->setEnabled(false);
  copy->setEnabled(false); save->setEnabled(false); editor->clear(); setPanelVisible(false);
  canvas->image = source; canvas->blocks.clear(); canvas->selectedIndex = -1; canvas->resetView();
  status->setText("正在识别图片文字…"); place(); show(); raise(); activateWindow(); setFocus();
  if (recognize && cached) { retry->setEnabled(true); compare->setEnabled(true); translate(); }
  else if (recognize) ocr.start(source, profile, 0, acceleration);
}
void ImageTranslationOverlay::place() {
  const auto location = sourceGlobalRect();
  auto screen = QGuiApplication::screenAt(location.center());
  if (!screen) screen = QGuiApplication::primaryScreen();
  setGeometry(screen->availableGeometry());
}
QRect ImageTranslationOverlay::sourceGlobalRect() const {
  auto location = imageRect ? imageRect() : QRect();
  if (parentWidget()) location.translate(parentWidget()->mapToGlobal(QPoint(0,0)));
  return location;
}
void ImageTranslationOverlay::finish() {
  if (!active) return;
  active = false; renderTimer.stop(); ocr.cancel(); translator.cancel(); hide(); emit closed();
}
void ImageTranslationOverlay::closeEvent(QCloseEvent *event) { finish(); event->accept(); }
bool ImageTranslationOverlay::eventFilter(QObject *object, QEvent *event) {
  if (object == parentWidget() && (event->type() == QEvent::Hide || event->type() == QEvent::Close)) finish();
  return QWidget::eventFilter(object, event);
}
void ImageTranslationOverlay::setOcrResult(const QJsonObject &result, bool autoTranslate) {
  if (!active) return;
  if (result.contains("error")) { status->setText(result["error"].toString()); return; }
  cache.clear(); completedKey.clear(); document.load(source, result); ready = !document.blocks.isEmpty();
  retry->setEnabled(ready); compare->setEnabled(ready); refresh(); refreshEditor();
  if (!ready) { status->setText("没有识别到可翻译的文字。"); return; }
  if (autoTranslate) translate();
}
void ImageTranslationOverlay::translate() {
  if (!active || !ready) return;
  translator.cancel(); renderTimer.stop();
  const auto sourceKey = sourceLanguage->currentData().toString(), targetKey = targetLanguage->currentData().toString();
  const auto key = sourceKey + '-' + targetKey;
  if (store) {
    store->config["translation_source"] = sourceKey; store->config["translation_target"] = targetKey; store->saveConfig();
  }
  compare->setChecked(false); canvas->selectedIndex = -1;
  if (cache.contains(key)) {
    document = cache[key]; completedKey = key; refresh(); refreshEditor();
    int clipped = 0; for (const auto &block : document.blocks) if (block.clipped) ++clipped;
    status->setText(clipped ? QString("翻译完成；%1 处区域空间不足，保留原文，完整译文可在“文字”中查看。").arg(clipped) : "翻译完成");
    copy->setEnabled(true); save->setEnabled(true); return;
  }
  completedKey.clear();
  for (auto &block : document.blocks) { block.translated.clear(); block.unchanged = false; }
  copy->setEnabled(false); save->setEnabled(false); refresh(); refreshEditor();
  status->setText("正在准备本地翻译…");
  translator.start(document.request(), sourceKey, targetKey);
}
void ImageTranslationOverlay::setTranslationResult(const QJsonObject &result) {
  if (!active) return;
  renderTimer.stop();
  if (result.contains("error")) {
    status->setText(result["error"].toString()); refresh(); refreshEditor(); return;
  }
  const auto values = result["blocks"].toArray();
  if (values.size() != document.blocks.size()) { status->setText("翻译结果不完整，已保留原图。请重试。"); return; }
  QSet<int> ids;
  for (const auto &value : values) {
    const auto block = value.toObject(); const auto id = block["id"].toInt(-1);
    if (id < 0 || id >= document.blocks.size() || ids.contains(id) || block["text"].toString().isEmpty()) {
      status->setText("翻译结果格式错误，请重试。"); return;
    }
    ids.insert(id);
  }
  for (const auto &block : values) document.apply(block.toObject());
  completedKey = sourceLanguage->currentData().toString() + '-' + targetLanguage->currentData().toString();
  refresh(); refreshEditor(); cache[completedKey] = document;
  copy->setEnabled(true); save->setEnabled(true);
  int clipped = 0; for (const auto &block : document.blocks) if (block.clipped) ++clipped;
  status->setText(clipped ? QString("翻译完成；%1 处区域空间不足，保留原文，完整译文可在“文字”中查看。").arg(clipped) : "翻译完成");
}
bool ImageTranslationOverlay::showingOriginal() const { return compare->isChecked(); }
void ImageTranslationOverlay::setShowingOriginal(bool original) { compare->setChecked(original); }
void ImageTranslationOverlay::setPanelVisible(bool visible) {
  { QSignalBlocker blocker(panelToggle); panelToggle->setChecked(visible); }
  editor->setVisible(visible);
  if (visible) refreshEditor();
}
void ImageTranslationOverlay::refresh() {
  if (!active) return;
  translated = document.render(source);
  canvas->image = showingOriginal() ? source : translated;
  canvas->blocks.clear(); for (const auto &block : document.blocks) canvas->blocks << block.bounds;
  canvas->update();
}
void ImageTranslationOverlay::refreshEditor() {
  syncing = true;
  const auto cursor = editor->textCursor();
  editor->setPlainText(document.text(showingOriginal()));
  auto selection = editor->textCursor(); const int maximum = int(editor->toPlainText().size());
  selection.setPosition(qBound(0,cursor.anchor(),maximum)); selection.setPosition(qBound(0,cursor.position(),maximum), QTextCursor::KeepAnchor);
  editor->setTextCursor(selection); syncing = false;
}
void ImageTranslationOverlay::selectBlock(int index) {
  canvas->selectedIndex = index; canvas->update();
  if (index < 0 || index >= document.blocks.size()) return;
  int start = 0;
  for (int n = 0; n < index; ++n) {
    const auto &block = document.blocks[n];
    start += (showingOriginal() || block.translated.isEmpty() ? block.original : block.translated).size() + 2;
  }
  const auto &block = document.blocks[index];
  const auto text = showingOriginal() || block.translated.isEmpty() ? block.original : block.translated;
  syncing = true; auto cursor = editor->textCursor(); cursor.setPosition(start);
  cursor.setPosition(start + int(text.size()), QTextCursor::KeepAnchor); editor->setTextCursor(cursor); editor->ensureCursorVisible(); syncing = false;
}
void ImageTranslationOverlay::editorSelectionChanged() {
  if (syncing) return;
  const auto position = editor->textCursor().selectionStart(); int start = 0;
  for (int n = 0; n < document.blocks.size(); ++n) {
    const auto &block = document.blocks[n];
    start += (showingOriginal() || block.translated.isEmpty() ? block.original : block.translated).size() + 2;
    if (position < start) { canvas->selectedIndex = n; canvas->update(); return; }
  }
}
QPointF ImageTranslationOverlay::imageToView(QPointF point) const {
  return canvas->mapTo(const_cast<ImageTranslationOverlay *>(this), canvas->imageToView(point).toPoint());
}
void ImageTranslationOverlay::copyImage() {
  if (!copy->isEnabled()) return;
  QApplication::clipboard()->setImage(showingOriginal() ? source : translated);
  status->setText(showingOriginal() ? "已复制原图" : "已复制翻译后的图片");
}
void ImageTranslationOverlay::saveImage() {
  if (!save->isEnabled()) return;
  const auto folder = store ? store->config["image_dir"].toString() : QString();
  const auto file = QFileDialog::getSaveFileName(this,"保存图片",folder + "/翻译截图.png","PNG 图片 (*.png)");
  if (file.isEmpty()) return;
  status->setText(Store::writeImage(file, showingOriginal() ? source : translated) ? "图片已保存" : "保存失败，请检查目标目录。");
}
void ImageTranslationOverlay::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) { finish(); return; }
  if (event->matches(QKeySequence::Copy)) {
    if (editor->hasFocus() && editor->textCursor().hasSelection()) editor->copy(); else copyImage();
    return;
  }
  if (event->matches(QKeySequence::Save)) { saveImage(); return; }
  if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_0) { canvas->resetView(); return; }
  if (event->key() == Qt::Key_Tab && !(event->modifiers() & Qt::ControlModifier)) { setShowingOriginal(!showingOriginal()); return; }
  QWidget::keyPressEvent(event);
}
