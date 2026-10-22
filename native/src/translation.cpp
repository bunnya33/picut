#include "translation.h"
#include "image_text.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtGui/QPainter>
#include <QtGui/QTextLayout>
#include <algorithm>
#include <cmath>

QString LocalTranslationJob::modelsPath() { return QCoreApplication::applicationDirPath() + "/translation"; }
LocalTranslationJob::LocalTranslationJob(QObject *parent) : QObject(parent) {
  setObjectName("localTranslationJob"); timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, this, [this] {
    cancel(); emit finished({{"error", "翻译超时，已停止。请缩小截图区域后重试。"}});
  });
  connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (active && error == QProcess::FailedToStart)
      complete({{"error", "无法启动本地翻译组件：" + process.errorString()}});
  });
  connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
    buffer += process.readAllStandardOutput();
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
      const auto value = QJsonDocument::fromJson(buffer.left(newline)).object();
      buffer.remove(0, newline + 1);
      if (!active) continue;
      if (value.contains("phase")) emit progress(value["phase"].toString(), value["done"].toInt(), value["total"].toInt());
      if (value.contains("block")) emit blockFinished(value["block"].toObject());
    }
    if (buffer.size() > 1024 * 1024) buffer.clear();
  });
  connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    if (!active) return;
    QFile output(temporary->path() + "/result.json"); QJsonObject result;
    if (output.open(QIODevice::ReadOnly)) { result = QJsonDocument::fromJson(output.readAll()).object(); output.close(); }
    if (result.isEmpty() || status != QProcess::NormalExit || (code && !result.contains("error")))
      result = {{"error", "翻译组件未正常完成，请检查语言包及运行库是否完整。"}};
    complete(result);
  });
}
LocalTranslationJob::~LocalTranslationJob() { cancel(); }
void LocalTranslationJob::complete(const QJsonObject &result) {
  if (!active) return;
  active = false; timeout.stop(); temporary.reset(); emit finished(result);
}
void LocalTranslationJob::cancel() {
  active = false; timeout.stop();
  if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
  buffer.clear(); temporary.reset();
}
void LocalTranslationJob::start(const QJsonArray &blocks, const QString &source, const QString &target) {
  cancel(); active = true;
  if (blocks.isEmpty()) { complete({{"error", "没有可翻译的文字。"}}); return; }
  if (!QFileInfo::exists(modelsPath() + "/models.json")) {
    complete({{"error", "缺少本地翻译语言包，请使用完整的翻译试用包。"}}); return;
  }
  temporary = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/picut-translation-XXXXXX");
  if (!temporary->isValid()) { complete({{"error", "无法创建翻译临时目录。"}}); return; }
  QFile input(temporary->path() + "/request.json");
  const auto data = QJsonDocument(QJsonObject{{"source", source}, {"target", target}, {"blocks", blocks}}).toJson(QJsonDocument::Compact);
  if (!input.open(QIODevice::WriteOnly) || input.write(data) != data.size()) {
    input.close(); complete({{"error", "无法准备翻译文字，请检查临时目录。"}}); return;
  }
  input.close();
  process.setWorkingDirectory(QCoreApplication::applicationDirPath());
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(QCoreApplication::applicationDirPath() + "/FloatShotTranslate.exe",
      {"--input", temporary->path() + "/request.json", "--output", temporary->path() + "/result.json", "--models", modelsPath()});
  timeout.start(180000);
}

static double colorDistance(QColor a, QColor b) {
  return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}
static QColor backgroundAt(const QImage &image, QRect region) {
  const auto inner = region.intersected(image.rect());
  const auto outer = inner.adjusted(-3, -3, 3, 3).intersected(image.rect());
  QHash<int, QList<QColor>> histogram;
  for (int y = outer.top(); y <= outer.bottom(); ++y)
    for (int x = outer.left(); x <= outer.right(); ++x) {
      if (inner.contains(x, y)) continue;
      const auto c = image.pixelColor(x, y);
      histogram[(c.red() >> 4) * 256 + (c.green() >> 4) * 16 + (c.blue() >> 4)].append(c);
    }
  if (histogram.isEmpty()) return image.pixelColor(inner.topLeft());
  QList<QColor> dominant;
  for (auto it = histogram.begin(); it != histogram.end(); ++it)
    if (it->size() > dominant.size()) dominant = *it;
  qint64 red = 0, green = 0, blue = 0;
  for (const auto c : dominant) { red += c.red(); green += c.green(); blue += c.blue(); }
  return QColor(int(red / dominant.size()), int(green / dominant.size()), int(blue / dominant.size()));
}
static QColor foregroundAt(const QImage &image, QRect rect, QColor background) {
  rect = rect.intersected(image.rect());
  QHash<int, QList<QColor>> histogram;
  for (int y = rect.top(); y <= rect.bottom(); y += 2)
    for (int x = rect.left(); x <= rect.right(); x += 2) {
      const auto c = image.pixelColor(x, y);
      if (colorDistance(c, background) < 100) continue;
      histogram[(c.red() >> 4) * 256 + (c.green() >> 4) * 16 + (c.blue() >> 4)].append(c);
    }
  QList<QColor> dominant;
  for (auto it = histogram.begin(); it != histogram.end(); ++it)
    if (it->size() > dominant.size()) dominant = *it;
  if (dominant.isEmpty()) return background.lightness() > 128 ? Qt::black : Qt::white;
  qint64 red = 0, green = 0, blue = 0;
  for (const auto c : dominant) { red += c.red(); green += c.green(); blue += c.blue(); }
  return QColor(int(red / dominant.size()), int(green / dominant.size()), int(blue / dominant.size()));
}
static bool hasBulletBefore(const QImage &image, QRect text) {
  const int height=text.height();
  if(height<8)return false;
  const QRect gutter=QRect(QPoint(text.left()-int(std::ceil(height*1.7)),text.top()+int(height*.18)),
                          QPoint(text.left()-3,text.top()+int(height*.82))).intersected(image.rect());
  if(gutter.isEmpty())return false;
  const auto background=backgroundAt(image,text);
  QSet<QPoint> visited;
  for(int y=gutter.top();y<=gutter.bottom();++y)for(int x=gutter.left();x<=gutter.right();++x) {
    const QPoint first(x,y);
    if(visited.contains(first) || colorDistance(image.pixelColor(first),background)<120)continue;
    QList<QPoint> component{first}; visited.insert(first); QRect bounds(first,QSize(1,1));
    for(qsizetype n=0;n<component.size();++n) {
      const auto point=component[n]; bounds=bounds.united(QRect(point,QSize(1,1)));
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const auto next=point+QPoint(dx,dy);
        if(!gutter.contains(next) || visited.contains(next) || colorDistance(image.pixelColor(next),background)<120)continue;
        visited.insert(next); component.append(next);
      }
    }
    // OCR often omits the bullet itself. An isolated compact dot to the left
    // of the text still starts a new list item; wrapped lines have no such dot.
    const int maximum=qMax(3,int(height*.42));
    if(component.size()>=3 && bounds.width()<=maximum && bounds.height()<=maximum &&
       bounds.width()*2>=bounds.height() && bounds.height()*2>=bounds.width() &&
       component.size()>=bounds.width()*bounds.height()*.35 &&
       gutter.adjusted(1,1,-1,-1).contains(bounds))return true;
  }
  return false;
}
void TranslationPage::load(const QImage &image, const QJsonObject &ocr) {
  blocks.clear();
  if (image.isNull()) return;
  ImageTextPage page; page.load(ocr);
  static const QRegularExpression listItem("^\\s*(?:[0-9]{1,4}[.)、．]|[•●▪◆·]|[-*]\\s)\\s*");
  for (const auto &line : page.lines) {
    const auto rawBox = line.quad.boundingRect().toAlignedRect();
    // OCR boxes can miss a few anti-aliased edge pixels, especially punctuation.
    const auto box = rawBox.adjusted(-2,-2,2,2).intersected(image.rect());
    if (box.width() < 2 || box.height() < 2) continue;
    const bool startsList=listItem.match(line.text).hasMatch() || hasBulletBefore(image,rawBox);
    int previous = -1; double bestGap = 1e9;
    for (int i = 0; i < blocks.size(); ++i) {
      const auto &block = blocks[i];
      const auto last = block.lines.last().boundingRect();
      const auto gap = box.top() - last.bottom();
      const auto h = qMin(last.height(), double(box.height()));
      bool chinese = false;
      for (const auto c : block.original) if (c.unicode() >= 0x3400 && c.unicode() <= 0x9fff) { chinese = true; break; }
      // Merge wrapped prose conservatively. Short labels and table cells stay independent.
      if (startsList || block.lines.size() >= 12 || block.original.size() > 1000 || gap < 0 || gap > h * .9 ||
          qAbs(last.height() - box.height()) > h * .35 || qAbs(last.left() - box.left()) > h * .7 ||
          last.width() < h * 8 || block.original.size() < (chinese ? 12 : 28) || gap >= bestGap) continue;
      const auto combined = block.bounds.united(box);
      bool intersectsOther = false;
      for (const auto &other : page.lines) {
        if (other.quad == line.quad || block.lines.contains(other.quad)) continue;
        if (combined.intersects(other.quad.boundingRect().toAlignedRect())) { intersectsOther = true; break; }
      }
      if (intersectsOther) continue;
      previous = i; bestGap = gap;
    }
    if (previous >= 0) {
      auto &block = blocks[previous];
      const auto last = block.original.back(); const auto first = line.text.front();
      const bool cjk = (last.unicode() >= 0x3000 && last.unicode() <= 0x9fff) ||
                       (first.unicode() >= 0x3000 && first.unicode() <= 0x9fff);
      if (!cjk) block.original += ' ';
      block.original += line.text; block.lines << line.quad; block.bounds = block.bounds.united(box);
    } else {
      TranslationBlock block; block.id = int(blocks.size()); block.original = line.text;
      block.lines << line.quad; block.bounds = box;
      block.background = backgroundAt(image, box); block.foreground = foregroundAt(image, box, block.background);
      block.fontSize = qMax(7., rawBox.height() * .86);
      blocks << block;
    }
  }
}
QJsonArray TranslationPage::request() const {
  QJsonArray result;
  for (const auto &block : blocks) result.append(QJsonObject{{"id", block.id}, {"text", block.original}});
  return result;
}
void TranslationPage::apply(const QJsonObject &result) {
  const auto id = result["id"].toInt(-1);
  if (id < 0 || id >= blocks.size()) return;
  auto &block = blocks[id]; block.translated = result["text"].toString(); block.unchanged = result["unchanged"].toBool();
}
static double layoutText(QTextLayout &layout, double width) {
  QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option); layout.beginLayout(); double height = 0;
  for (;;) {
    auto line = layout.createLine(); if (!line.isValid()) break;
    line.setLineWidth(width); line.setPosition({0, height}); height += line.height();
  }
  layout.endLayout(); return height;
}
QImage TranslationPage::render(const QImage &source) {
  if (source.isNull()) return {};
  QImage output = source.convertToFormat(QImage::Format_ARGB32); output.setDevicePixelRatio(1);
  QPainter painter(&output); painter.setRenderHint(QPainter::TextAntialiasing);
  for (auto &block : blocks) {
    block.clipped = false;
    if (block.translated.isEmpty() || block.unchanged || block.translated == block.original) continue;
    const auto area = block.bounds.intersected(output.rect());
    QFont font("Microsoft YaHei UI"); font.setBold(block.bold);
    int size = qMax(6, int(std::round(block.fontSize))); double height = 0;
    for (; size >= 6; --size) {
      font.setPixelSize(size); QTextLayout measure(block.translated, font);
      height = layoutText(measure, area.width());
      if (height <= area.height()) break;
    }
    // Never publish a clipped translation as if it were complete. Preserve the
    // original pixels; the complete text remains available in the text panel.
    if (height > area.height()) { block.clipped = true; continue; }
    for (const auto &line : block.lines) {
      const auto box = line.boundingRect().toAlignedRect().adjusted(-2,-2,2,2).intersected(output.rect());
      painter.fillRect(box, backgroundAt(source, box));
    }
    // All replacement glyphs stay inside the original paragraph bounds.
    painter.save(); painter.setClipRect(area); painter.setPen(block.foreground);
    QTextLayout layout(block.translated, font); layoutText(layout, area.width());
    layout.draw(&painter, area.topLeft()); painter.restore();
  }
  return output;
}
QString TranslationPage::text(bool original) const {
  QStringList result;
  for (const auto &block : blocks) result << (original || block.translated.isEmpty() ? block.original : block.translated);
  return result.join("\n\n");
}
