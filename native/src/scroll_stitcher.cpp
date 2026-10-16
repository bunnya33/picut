#include "scroll_capture.h"
#include <QtCore/QFileInfo>
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
Motion motion(const QImage &before, const QImage &after) {
  const int h = qMin(720, before.height());
  const QSize small(qMin(180, before.width()), h);
  auto a = before.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
               .convertToFormat(QImage::Format_Grayscale8);
  auto b = after.scaled(small, Qt::IgnoreAspectRatio, Qt::FastTransformation)
               .convertToFormat(QImage::Format_Grayscale8);
  const auto smallColumns=changedColumns(a,b);
  QList<Motion> matches;
  const int overlap = qMax(32, h / 4);
  for (int shift = -h + overlap; shift <= h - overlap; ++shift) {
    if (!shift) continue;
    matches.append({shift, error(a, b, shift, 0, h, smallColumns, 48, 32), false});
  }
  std::sort(matches.begin(), matches.end(), [](const Motion &x, const Motion &y) {
    return x.score < y.score;
  });
  Motion best;
  auto fullA = before.convertToFormat(QImage::Format_Grayscale8);
  auto fullB = after.convertToFormat(QImage::Format_Grayscale8);
  const auto fullColumns=changedColumns(fullA,fullB);
  const double scale = double(before.height()) / h;
  const int radius = qCeil(scale) + 1;
  QList<Motion> refined;
  for (int n = 0; n < qMin(6, int(matches.size())); ++n) {
    int center = qRound(matches[n].shift * scale);
    for (int shift = center - radius; shift <= center + radius; ++shift) {
      if (!shift || std::abs(shift) > before.height() * .78) continue;
      const double score = error(fullA, fullB, shift, 0, before.height(), fullColumns, 130, 64);
      refined.append({shift, score, false});
      if (score < best.score) best = {shift, score, false};
    }
  }
  for (auto other : refined)
    if (std::abs(other.shift - best.shift) > qMax(3, radius * 2) &&
        other.score <= best.score + .4)
      best.ambiguous = true;
  return best;
}
bool reachesEdges(const QImage &previous, const QImage &current, int shift) {
  // A downward scroll maps current[y] to previous[y + shift]. Test the
  // overlapping edge at the top of the new frame AND bottom of the old one.
  const auto &top = shift > 0 ? current : previous;
  const auto &bottom = shift > 0 ? previous : current;
  const int d = std::abs(shift), h = top.height();
  const int band = qMin(qMax(10, h / 20), 40);
  auto a = bottom.convertToFormat(QImage::Format_Grayscale8);
  auto b = top.convertToFormat(QImage::Format_Grayscale8);
  const auto columns=changedColumns(a,b);
  const int bottomBegin = h - d - band - 2;
  for (auto range : {QPair<int,int>{2, 2 + band},
                      QPair<int,int>{bottomBegin, h - d - 2}}) {
    const double moved = error(a, b, d, range.first, range.second, columns, band, 80);
    // For the lower edge, compare the same pixels in the old frame, so a
    // stationary footer cannot be masked by scrolling pixels further above it.
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
  const QString path = cache.filePath(QString::number(strips.size()) + ".png");
  if (!part.save(path, "PNG")) return false;
  strips.append({path, part.scaledToWidth(qMin(280, part.width()), Qt::SmoothTransformation),
                 height, part.height()});
  height += part.height();
  return true;
}
ScrollStitcher::Result ScrollStitcher::accept(const QImage &frame) {
  if (frame.width() < 12 || frame.height() < 64) return NoOverlap;
  auto value = frame.convertToFormat(QImage::Format_RGB32);
  if (previous.isNull()) {
    if (qint64(value.width()) * value.height() > 64000000) return LimitReached;
    if (!append(value)) return CacheError;
    width = value.width(); previous = value; return Started;
  }
  if (value.size() != previous.size()) return NoOverlap;
  if (value == previous || caretOnlyChange(previous,value)) return Unchanged;
  auto movement = motion(previous, value);
  if (movement.score > 4.5 || movement.ambiguous) return NoOverlap;
  if (!reachesEdges(previous, value, movement.shift)) return BoundaryMismatch;
  const int nextPosition = position + movement.shift;
  if (nextPosition < 0) return NoOverlap;
  const int extra = nextPosition + value.height() - height;
  if (extra > 0) {
    if (qint64(width) * (qint64(height) + extra) > 64000000 ||
        height + extra > 100000 || strips.size() >= 1200) return LimitReached;
    if (!append(value.copy(0, value.height() - extra, width, extra))) return CacheError;
  }
  previous = value; position = nextPosition;
  return extra > 0 ? Appended : Revisited;
}
QImage ScrollStitcher::image() const {
  if (!width || !height) return {};
  QImage result(width, height, QImage::Format_RGB32);
  if (result.isNull()) return {};
  QPainter painter(&result);
  for (const auto &strip : strips) {
    QImage part(strip.path);
    if (part.isNull()) return {};
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
