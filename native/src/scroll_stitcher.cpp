#include "scroll_capture.h"
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtGui/QPainter>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool caretOnlyChange(const QImage &a, const QImage &b) {
  int left=a.width(),right=-1,top=a.height(),bottom=-1;
  for (int y=0;y<a.height();++y) {
    auto p=reinterpret_cast<const QRgb *>(a.constScanLine(y));
    auto q=reinterpret_cast<const QRgb *>(b.constScanLine(y));
    for (int x=0;x<a.width();++x) if (p[x]!=q[x]) {
      left=qMin(left,x); right=qMax(right,x); top=qMin(top,y); bottom=y;
      if (right-left>3 || bottom-top>96) return false;
    }
  }
  return right>=left;
}
// Sample actual pixels rather than row averages: syntax-highlighted code and
// repeated lines otherwise produce deceptively similar row signatures.
QList<int> changedColumns(const QImage &a,const QImage &b) {
  QList<int> columns;
  const int step=qMax(1,a.height()/160);
  for (int x=2;x<a.width()-2;++x) {
    int sum=0,count=0;
    for (int y=0;y<a.height();y+=step) {
      sum+=qMin(90,std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])));
      ++count;
    }
    if (sum>count*.4) columns.append(x);
  }
  return columns;
}
double error(const QImage &a, const QImage &b, int shift, int first, int last,
             const QList<int> &active, int rows = 70, int columns = 40) {
  const int begin = qMax(first, qMax(0, -shift));
  const int end = qMin(last, qMin(b.height(), a.height() - shift));
  if (end <= begin || active.isEmpty()) return 1e9;
  columns=qMin(columns,int(active.size()));
  const int step = qMax(1, (end - begin) / rows);
  double sum = 0; int count = 0;
  for (int y = begin; y < end; y += step) {
    auto p = a.constScanLine(y + shift), q = b.constScanLine(y);
    for (int n = 0; n < columns; ++n) {
      const int x = active[n*active.size()/columns];
      const int d = std::abs(int(p[x]) - int(q[x]));
      sum += qMin(d, 90); ++count;
    }
  }
  return count ? sum / count : 1e9;
}
struct Motion { int shift = 0; double score = 1e9; bool ambiguous = false; };
Motion motion(const QImage &before, const QImage &after, int direction) {
  // Keep every row: resizing vertically loses exact offsets and is especially
  // unreliable when only a small overlap survives a fast wheel/PageDown jump.
  const int h = before.height();
  const QSize small(qMin(180, before.width()), h);
  auto a = before.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
               .convertToFormat(QImage::Format_Grayscale8);
  auto b = after.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
               .convertToFormat(QImage::Format_Grayscale8);
  const auto smallColumns=changedColumns(a,b);
  const int overlap = qMax(20, qMin(48, h / 20));
  auto allowed = [&](int shift) { return shift && std::abs(shift) <= h - overlap; };
  // Content rows vote for offsets. Common blank/repeated rows are excluded,
  // rather than letting them overwhelm real text features with false matches.
  auto rowKey = [&](const QImage &image, int y) {
    quint64 key = 1469598103934665603ULL;
    const auto row = image.constScanLine(y);
    const int count = qMin(32, int(smallColumns.size()));
    for (int n = 0; n < count; ++n) {
      key ^= row[smallColumns[n * smallColumns.size() / count]] >> 2;
      key *= 1099511628211ULL;
    }
    return key;
  };
  QHash<quint64, QList<int>> rows;
  for (int y = 0; y < h; ++y) rows[rowKey(a,y)].append(y);
  QHash<int,int> votes;
  for (int y = 0; y < h; ++y) {
    const auto found = rows.constFind(rowKey(b,y));
    if (found == rows.cend() || found->size() > 32) continue;
    for (int old : *found) if (allowed(old-y)) ++votes[old-y];
  }
  QList<int> offsets = votes.keys();
  std::sort(offsets.begin(), offsets.end(), [&](int x, int y) {
    return votes.value(x) > votes.value(y);
  });
  Motion best;
  auto fullA = before.convertToFormat(QImage::Format_Grayscale8);
  auto fullB = after.convertToFormat(QImage::Format_Grayscale8);
  const auto fullColumns=changedColumns(fullA,fullB);
  QList<Motion> refined;
  auto evaluate = [&](int shift) {
    const double score = error(fullA, fullB, shift, 0, h, fullColumns, 130, 64);
    refined.append({shift,score,false});
    if (score < best.score) best = {shift,score,false};
  };
  int positive = 0, negative = 0;
  const int support = offsets.isEmpty() ? 0 : qMax(3,votes.value(offsets.first()) / 5);
  for (int shift : offsets) {
    if (votes.value(shift)<support) break;
    auto &count = shift > 0 ? positive : negative;
    if (count++ < 16) evaluate(shift);
  }
  if (best.score > 4.5) {
    // Pixel-error fallback for repainting/antialiasing changes. A direction
    // never substitutes an assumed wheel distance.
    QList<Motion> matches;
    for (int shift = -h + overlap; shift <= h - overlap; ++shift)
      if (shift) matches.append({shift,error(a,b,shift,0,h,smallColumns,32,24),false});
    std::sort(matches.begin(),matches.end(),[](const Motion &x,const Motion &y) { return x.score < y.score; });
    for (int n=0;n<qMin(8,int(matches.size()));++n) evaluate(matches[n].shift);
  }
  // A direction hint can distinguish opposite-direction repeated patterns;
  // conflicting strong image evidence still wins (e.g. inertial reversal).
  if (direction) {
    for (auto value : refined)
      if (value.shift * direction > 0 && value.score <= best.score + .15) {
        if (best.shift * direction <= 0 || value.score < best.score) best=value;
      }
  }
  for (auto other : refined) {
    if (direction && best.shift * direction > 0 && other.shift * direction < 0) continue;
    if (std::abs(other.shift - best.shift) > 3 && other.score <= best.score + .4)
      best.ambiguous = true;
  }
  return best;
}
bool reachesEdges(const QImage &previous, const QImage &current, int shift) {
  // A downward scroll maps current[y] to previous[y + shift]. Test both edges,
  // shrinking the validation bands when PageDown leaves only a short overlap.
  const auto &top = shift > 0 ? current : previous;
  const auto &bottom = shift > 0 ? previous : current;
  const int d = std::abs(shift), h = top.height();
  const int band = qMax(4,std::min({qMax(10,h/20),40,(h-d-4)/2}));
  auto a = bottom.convertToFormat(QImage::Format_Grayscale8);
  auto b = top.convertToFormat(QImage::Format_Grayscale8);
  const auto columns=changedColumns(a,b);
  const int bottomBegin = h - d - band - 2;
  for (auto range : {QPair<int,int>{2, 2 + band},
                      QPair<int,int>{bottomBegin, h - d - 2}}) {
    const double moved = error(a, b, d, range.first, range.second, columns, band, 80);
    const double stationary = range.first == 2
        ? error(a, b, 0, range.first, range.second, columns, band, 80)
        : error(a, b, 0, h - band - 2, h - 2, columns, band, 80);
    if (moved > 5.0 || stationary < 1.0 || stationary < moved * 1.6)
      return false;
  }
  return true;
}
}

bool ScrollStitcher::append(const QImage &part) {
  if (!cache.isValid() || part.isNull()) return false;
  const QString path = cache.filePath(QString::number(strips.size()) + ".rgb");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char *>(part.constBits()),part.sizeInBytes()) != part.sizeInBytes() ||
      !file.flush())
    return false;
  strips.append({path, part.scaledToWidth(qMin(280, part.width()), Qt::SmoothTransformation),
                 height, part.height()});
  height += part.height();
  return true;
}
ScrollStitcher::Result ScrollStitcher::accept(const QImage &frame, int direction) {
  if (frame.width() < 12 || frame.height() < 64) return NoOverlap;
  auto value = frame.convertToFormat(QImage::Format_RGB32);
  if (previous.isNull()) {
    if (qint64(value.width()) * value.height() > 64000000) return LimitReached;
    if (!append(value)) return CacheError;
    width = value.width(); previous = value;
    previousGray=value.convertToFormat(QImage::Format_Grayscale8); return Started;
  }
  if (value.size() != previous.size()) return NoOverlap;
  if (value == previous || caretOnlyChange(previous,value)) return Unchanged;
  auto gray=value.convertToFormat(QImage::Format_Grayscale8);
  auto movement = motion(previousGray, gray, direction);
  if (movement.score > 4.5 || movement.ambiguous) return NoOverlap;
  if (!reachesEdges(previousGray, gray, movement.shift)) return BoundaryMismatch;
  const int nextPosition = position + movement.shift;
  if (nextPosition < 0) return NoOverlap;
  const int extra = nextPosition + value.height() - height;
  if (extra > 0) {
    if (qint64(width) * (qint64(height) + extra) > 64000000 ||
        height + extra > 100000 || strips.size() >= 8192) return LimitReached;
    if (!append(value.copy(0, value.height() - extra, width, extra))) return CacheError;
  }
  previous = value; previousGray=std::move(gray); position = nextPosition;
  return extra > 0 ? Appended : Revisited;
}
QImage ScrollStitcher::image() const {
  if (!width || !height) return {};
  QImage result(width, height, QImage::Format_RGB32);
  if (result.isNull()) return {};
  QPainter painter(&result);
  for (const auto &strip : strips) {
    QImage part(width,strip.height,QImage::Format_RGB32);
    QFile file(strip.path);
    if (part.isNull() || !file.open(QIODevice::ReadOnly) ||
        file.read(reinterpret_cast<char *>(part.bits()),part.sizeInBytes()) != part.sizeInBytes()) return {};
    painter.drawImage(0, strip.y, part);
  }
  return result;
}
QSize ScrollStitcher::previewSize(QSize original, QSize available) {
  if (original.isEmpty() || available.isEmpty()) return {};
  const double scale = qMin(1., qMin(double(available.width()) / original.width(),
                                     double(available.height()) / original.height()));
  return {qMax(1, qRound(original.width() * scale)),
           qMax(1, qRound(original.height() * scale))};
}
void ScrollStitcher::paint(QPainter &painter, QRectF target) const {
  if (!height) return;
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  for (const auto &strip : strips)
    painter.drawImage(QRectF(target.x(), target.y() + target.height() * strip.y / height,
                              target.width(), target.height() * strip.height / height),
                       strip.thumbnail);
}
