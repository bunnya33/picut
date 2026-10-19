#include "scroll_capture.h"
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtGui/QPainter>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Keep every vertical row. Only the horizontal axis is reduced, so the
// displacement is still measured in original pixels, including short overlaps.
QImage matchingImage(const QImage &image) {
  return image.convertToFormat(QImage::Format_Grayscale8)
      .scaled(qMin(480,image.width()),image.height(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
bool isolatedChange(const QImage &a,const QImage &b) {
  int left=a.width(),right=-1,top=a.height(),bottom=-1;
  const int maximumWidth=qMax(4,a.width()/3),maximumHeight=qMax(8,qMin(96,a.height()/4));
  for(int y=0;y<a.height();++y) {
    auto p=a.constScanLine(y),q=b.constScanLine(y);
    for(int x=0;x<a.width();++x) if(std::abs(int(p[x])-int(q[x]))>6) {
      left=qMin(left,x); right=qMax(right,x); top=qMin(top,y); bottom=y;
      if(right-left>maximumWidth || bottom-top>maximumHeight) return false;
    }
  }
  // A small isolated animation/caret inside a stationary viewport supplies no
  // evidence that the viewport itself scrolled. In particular it must not
  // stop automatic scrolling or invent a new strip.
  return right<left || (top>3 && bottom<a.height()-4);
}
struct Tile { int left,right; };
QList<Tile> movingTiles(const QImage &a,const QImage &b) {
  QList<Tile> result;
  const int size=qMax(12,a.width()/12);
  for(int x=0;x<a.width();x+=size) {
    Tile tile{x,qMin(a.width(),x+size)};
    double difference=0; int count=0;
    for(int y=0;y<a.height();y+=3) for(int xx=tile.left;xx<tile.right;xx+=2) {
      difference+=std::abs(int(a.constScanLine(y)[xx])-int(b.constScanLine(y)[xx])); ++count;
    }
    if(count && difference/count>.35) result.append(tile);
  }
  return result;
}
// A local binary descriptor survives small repaint/antialias changes. Local
// tiles prevent a blinking caret, animation or fixed sidebar from invalidating
// the entire row, as happened with the old full-row RGB hashes.
quint32 descriptor(const QImage &image,int y,Tile tile) {
  std::array<int,16> values{}; int sum=0;
  auto row=image.constScanLine(y);
  for(int n=0;n<16;++n) { values[n]=row[tile.left+n*(tile.right-tile.left)/16]; sum+=values[n]; }
  const int mean=sum/16;
  quint32 key=0;
  for(int n=0;n<16;++n) {
    if(values[n]<mean-12) key|=1u<<n;
    if(values[n]>mean+12) key|=1u<<(n+16);
  }
  return key;
}
struct Motion { int shift=0; double score=1e9; int blocks=0; bool ambiguous=false, boundary=false; };
Motion evaluate(const QImage &a,const QImage &b,int shift,const QList<Tile> &tiles) {
  const int begin=qMax(0,-shift),end=qMin(b.height(),a.height()-shift);
  QList<double> errors;
  int points=0;
  const int block=qMin(28,qMax(8,(end-begin)/4));
  for(auto tile:tiles) for(int top=begin;top<end;top+=block) {
    double sum=0; int count=0;
    for(int y=top;y<qMin(end,top+block);y+=2) {
      auto p=a.constScanLine(y+shift),q=b.constScanLine(y);
      const auto pn=a.constScanLine(qMin(a.height()-1,y+shift+1));
      const auto qn=b.constScanLine(qMin(b.height()-1,y+1));
      for(int x=tile.left;x<tile.right-1;x+=2) {
        // White space contributes no false evidence of a match. Evaluate the
        // actual character/image edges in either image, not a diluted mean.
        int texture=std::max({std::abs(int(p[x])-int(p[x+1])),std::abs(int(q[x])-int(q[x+1])),
                          std::abs(int(p[x])-int(pn[x])),std::abs(int(q[x])-int(qn[x]))});
        if(texture<8) continue;
        sum+=qMin(90,qMax(0,std::abs(int(p[x])-int(q[x]))-6)); ++count;
      }
    }
    if(count>=4) { errors.append(sum/count); points+=count; }
  }
  if(points<16 || errors.size()<2) return {};
  // Bound the influence of local animation; keep every informative block in
  // the score so a unique number can disambiguate otherwise repeated rows.
  double sum=0; int bad=0;
  for(double value:errors) { sum+=qMin(12.,value); if(value>6) ++bad; }
  return {shift,sum/errors.size()+2.*bad/errors.size(),int(errors.size()),false,false};
}
bool reachesEdges(const QImage &a,const QImage &b,int shift,const QList<Tile> &tiles) {
  const auto &old=shift>0?a:b;
  const auto &now=shift>0?b:a;
  const int d=std::abs(shift),h=a.height(),overlap=h-d;
  const int band=qMin(qMax(24,h/10),qMin(72,overlap));
  for(bool bottom:{false,true}) {
    int good=0,bad=0;
    for(auto tile:tiles) {
      double moved=0,still=0; int count=0;
      for(int n=0;n<band;++n) {
        int y=bottom?overlap-band+n:n;
        auto p=old.constScanLine(y+d),q=now.constScanLine(y);
        const int physical=bottom?h-band+n:n;
        auto s=old.constScanLine(physical),t=now.constScanLine(physical);
        for(int x=tile.left;x<tile.right;x+=2) {
          moved+=qMin(90,std::abs(int(p[x])-int(q[x])));
          still+=qMin(90,std::abs(int(s[x])-int(t[x]))); ++count;
        }
      }
      moved/=qMax(1,count); still/=qMax(1,count);
      if(moved<4 && still>qMax(.45,moved*1.8)) ++good;
      else if(moved>2 && still<qMax(.8,moved*.45)) ++bad;
    }
    // Blank margins are neutral, not a reason to reject a valid match. Actual
    // stationary headers/footers within moving columns contradict the match.
    if(bad>good) return false;
  }
  return true;
}
Motion motion(const QImage &a,const QImage &b,int direction) {
  const int h=a.height(),overlap=qMax(20,qMin(48,h/20));
  const auto tiles=movingTiles(a,b);
  if(tiles.isEmpty()) return {0,0};
  QHash<int,int> votes;
  for(auto tile:tiles) {
    QHash<quint64,QList<int>> rows;
    auto key=[&](const QImage &image,int y) {
      return quint64(descriptor(image,y,tile)) | quint64(descriptor(image,y+2,tile))<<32;
    };
    for(int y=0;y<h-2;++y) { const auto value=key(a,y); if(value) rows[value].append(y); }
    for(int y=0;y<h-2;++y) {
      const auto value=key(b,y); if(!value) continue;
      auto found=rows.constFind(value);
      if(found==rows.cend() || found->size()>40) continue;
      for(int old:*found) if(std::abs(old-y)<=h-overlap) ++votes[old-y];
    }
  }
  QList<int> offsets=votes.keys();
  std::sort(offsets.begin(),offsets.end(),[&](int x,int y){ return votes.value(x)>votes.value(y); });
  QList<Motion> refined;
  refined.append(evaluate(a,b,0,tiles));
  int positive=0,negative=0;
  const int support=offsets.isEmpty()?0:qMax(3,votes.value(offsets.first())/8);
  for(int shift:offsets) {
    if(votes.value(shift)<support) break;
    if(!shift) continue;
    auto &count=shift>0?positive:negative;
    if(count++<24) refined.append(evaluate(a,b,shift,tiles));
  }
  Motion best;
  for(auto value:refined) if(value.score<best.score) best=value;
  if(direction) for(auto value:refined)
    if(value.shift*direction>0 && value.score<=best.score+.015 && best.shift*direction<0) best=value;
  for(auto other:refined) {
    if(direction && best.shift*direction>0 && other.shift*direction<0) continue;
    if(std::abs(other.shift-best.shift)>2 && other.score<=best.score+qMax(.025,best.score*.08))
      best.ambiguous=true;
  }
  if(best.shift) best.boundary=reachesEdges(a,b,best.shift,tiles);
  return best;
}
}
bool ScrollStitcher::append(const QImage &part, bool prepend) {
  if (!cache.isValid() || part.isNull()) return false;
  const QString path = cache.filePath(QString::number(strips.size()) + ".rgb");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char *>(part.constBits()),part.sizeInBytes()) != part.sizeInBytes() ||
      !file.flush())
    return false;
  Strip strip{path, part.scaledToWidth(qMin(280, part.width()), Qt::SmoothTransformation),
              prepend ? 0 : height, part.height()};
  if (prepend) {
    for(auto &existing:strips) existing.y+=part.height();
    strips.prepend(std::move(strip));
  } else strips.append(std::move(strip));
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
    previousGray=matchingImage(value); return Started;
  }
  if (value.size() != previous.size()) return NoOverlap;
  if (value == previous) return Unchanged;
  auto gray=matchingImage(value);
  if(isolatedChange(previousGray,gray)) return Unchanged;
  auto movement = motion(previousGray, gray, direction);
  if (movement.score > 3.2 || movement.ambiguous) return NoOverlap;
  if (!movement.shift) return Unchanged;
  if (!movement.boundary) return BoundaryMismatch;
  int nextPosition = position + movement.shift;
  const int leading = qMax(0,-nextPosition);
  const int extra = qMax(0,nextPosition + value.height() - height);
  if (extra || leading) {
    if (qint64(width) * (qint64(height) + extra + leading) > 64000000 ||
        height + extra + leading > 100000 || strips.size() >= 8192) return LimitReached;
    if (leading && !append(value.copy(0,0,width,leading),true)) return CacheError;
    nextPosition+=leading;
  }
  if (extra > 0) {
    if (!append(value.copy(0, value.height() - extra, width, extra))) return CacheError;
  }
  previous = value; previousGray=std::move(gray); position = nextPosition;
  return extra > 0 || leading > 0 ? Appended : Revisited;
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
