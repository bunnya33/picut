#include "scroll_capture.h"
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QHash>
#include <QtCore/QCache>
#include <QtGui/QPainter>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace {
// Preserve every original row: vertical resampling would change displacement.
QImage descriptorImage(const QImage &image) {
  return image.convertToFormat(QImage::Format_Grayscale8)
      .scaled(qMin(256,image.width()),image.height(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
quint64 rowKey(const uchar *row,int width,int lane) {
  const int begin=lane*width/3,end=(lane+1)*width/3;
  int lo=255,hi=0,edges=0;
  quint64 hash=1469598103934665603ULL ^ quint64(lane);
  for(int x=begin;x<end;++x) {
    const int v=row[x]; lo=qMin(lo,v); hi=qMax(hi,v);
    if(x>begin && std::abs(v-int(row[x-1]))>8) ++edges;
    // Gradient polarity survives a small brightness shift and antialiasing
    // changes. It proposes positions only; original RGB overlap is validated.
    if(x>begin) {
      const int delta=v-int(row[x-1]);
      hash=(hash^quint64(delta>10?2:delta<-10?1:0))*1099511628211ULL;
    }
  }
  return hi-lo>=20 && edges>=3 ? hash : 0;
}
}

struct ScrollStitcherV2::State {
  struct Part { QString path; QImage gray,thumb; int y,height; };
  QTemporaryDir cache;
  std::map<int,Part> parts;
  QHash<quint64,QList<int>> rows;
  mutable QCache<int,QImage> pixelCache{32768};
  int width=0,viewportHeight=0,top=0,bottom=0,position=0;
  quint64 revision=0,nextFile=0;
  QImage lastAccepted;
  bool lost=false;

  bool append(const QImage &pixels,int y) {
    if(!cache.isValid()) return false;
    const auto path=cache.filePath(QString::number(nextFile++)+".rgb");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) ||
       file.write(reinterpret_cast<const char *>(pixels.constBits()),pixels.sizeInBytes())!=pixels.sizeInBytes() ||
       !file.commit()) return false;
    auto gray=descriptorImage(pixels);
    const int tw=qMin(280,pixels.width());
    auto thumb=pixels.scaled(tw,qMax(1,qRound(double(pixels.height())*tw/pixels.width())),
                            Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    parts.emplace(y,Part{path,gray,thumb,y,pixels.height()});
    pixelCache.insert(y,new QImage(pixels),qMax(1,int(pixels.sizeInBytes()/1024)));
    for(int r=0;r<gray.height();++r) for(int lane=0;lane<3;++lane) {
      auto key=rowKey(gray.constScanLine(r),gray.width(),lane);
      if(key) rows[key].append(y+r);
    }
    if(!width) { width=pixels.width(); top=y; bottom=y+pixels.height(); }
    else { top=qMin(top,y); bottom=qMax(bottom,y+pixels.height()); }
    ++revision;
    return true;
  }
  QImage read(int start,int count,bool gray=false) const {
    if(count<1 || start<top || start+count>bottom) return {};
    QImage image(gray?qMin(256,width):width,count,gray?QImage::Format_Grayscale8:QImage::Format_RGB32);
    if(image.isNull()) return {};
    auto it=parts.upper_bound(start); if(it!=parts.begin()) --it;
    for(;it!=parts.end() && it->first<start+count;++it) {
      const auto &p=it->second;
      const int first=qMax(start,p.y),last=qMin(start+count,p.y+p.height);
      if(first>=last) continue;
      if(gray) {
        for(int y=first;y<last;++y)
          std::memcpy(image.scanLine(y-start),p.gray.constScanLine(y-p.y),size_t(image.width()));
      } else {
        auto pixels=pixelCache.object(p.y);
        if(!pixels) {
          QImage value(width,p.height,QImage::Format_RGB32); QFile file(p.path);
          if(value.isNull() || !file.open(QIODevice::ReadOnly) ||
             file.read(reinterpret_cast<char *>(value.bits()),value.sizeInBytes())!=value.sizeInBytes()) return {};
          if(value.sizeInBytes()/1024>pixelCache.maxCost()) {
            std::memcpy(image.scanLine(first-start),value.constScanLine(first-p.y),size_t(last-first)*width*4); continue;
          }
          pixelCache.insert(p.y,new QImage(value),qMax(1,int(value.sizeInBytes()/1024))); pixels=pixelCache.object(p.y);
        }
        std::memcpy(image.scanLine(first-start),pixels->constScanLine(first-p.y),size_t(last-first)*width*4);
      }
    }
    return image;
  }
  struct Validation { bool valid=false,boundary=false,cacheError=false; double error=1e10; };
  Validation validate(const QImage &frame,int at) const {
    const int begin=qMax(top,at),end=qMin(bottom,at+frame.height()),overlap=end-begin;
    if(overlap<qMin(96,qMax(24,viewportHeight/5))) return {};
    const auto reference=read(begin,overlap);
    if(reference.isNull()) return {false,false,true};
    qint64 changed=0,total=qint64(overlap)*width;
    int left=width,right=-1,first=overlap,last=-1,textRows=0;
    qint64 error=0;
    for(int y=0;y<overlap;++y) {
      const auto a=reinterpret_cast<const QRgb *>(reference.constScanLine(y));
      const auto b=reinterpret_cast<const QRgb *>(frame.constScanLine(begin-at+y));
      int rowEdges=0;
      for(int x=0;x<width;++x) {
        const int delta=qMax(std::abs(qRed(a[x])-qRed(b[x])),qMax(std::abs(qGreen(a[x])-qGreen(b[x])),std::abs(qBlue(a[x])-qBlue(b[x]))));
        error+=delta;
        if(delta>6) { ++changed; left=qMin(left,x); right=qMax(right,x); first=qMin(first,y); last=y; }
        if(x && std::abs(qGray(a[x])-qGray(a[x-1]))>12) ++rowEdges;
        if(changed>8 && (right-left+1>qMin(96,width/5) || last-first+1>qMin(96,overlap/4)))
          return {false,first<8 || last>=overlap-8 || last-first>overlap*3/4};
      }
      if(rowEdges>=6) ++textRows;
    }
    // Blank overlap is not evidence of displacement, even when pixel-identical.
    if(textRows<qMin(16,qMax(5,overlap/12))) return {};
    if(!changed) return {true,false,false,0};
    const QRect change(QPoint(left,first),QPoint(right,last));
    // Only one compact interior repaint may be ignored. In particular, scattered
    // line-number changes, fixed headers and sidebars cannot be masked away.
    const bool compact=change.width()<=qMin(96,width/5) && change.height()<=qMin(96,overlap/4) &&
        qint64(change.width())*change.height()<=total/40 && first>=8 && last<overlap-8 && left>=8 && right<width-8;
    if(compact) return {true,false,false,double(error)/double(total)};
    const bool boundary=first<8 || last>=overlap-8 || change.height()>overlap*3/4;
    return {false,boundary,false,double(error)/double(total)};
  }
};

ScrollStitcherV2::ScrollStitcherV2():state(std::make_unique<State>()) {}
ScrollStitcherV2::~ScrollStitcherV2()=default;
QSize ScrollStitcherV2::size() const { return {state->width,state->bottom-state->top}; }
quint64 ScrollStitcherV2::contentRevision() const { return state->revision; }
int ScrollStitcherV2::viewportPosition() const { return state->position-state->top; }
QImage ScrollStitcherV2::image() const { return state->read(state->top,state->bottom-state->top); }
ScrollStitcher::Preview ScrollStitcherV2::preview() const {
  ScrollStitcher::Preview result; result.size=size(); result.revision=state->revision;
  result.viewportY=viewportPosition(); result.viewportHeight=state->viewportHeight;
  for(const auto &[y,p]:state->parts) result.parts.append({p.thumb,y-state->top,p.height,0,p.height});
  return result;
}
ScrollStitcher::Result ScrollStitcherV2::accept(const QImage &input,int direction) {
  Q_UNUSED(direction); // Input intent never supplies a document coordinate.
  using Engine=ScrollStitcher;
  auto &s=*state;
  if(input.width()<24 || input.height()<64) return Engine::NoOverlap;
  auto frame=input.convertToFormat(QImage::Format_RGB32);
  if(!s.width) {
    if(qint64(frame.width())*frame.height()>64000000) return Engine::LimitReached;
    if(!s.append(frame,0)) return Engine::CacheError;
    s.viewportHeight=frame.height(); s.lastAccepted=frame; return Engine::Started;
  }
  if(frame.size()!=QSize(s.width,s.viewportHeight)) { s.lost=true; return Engine::BoundaryMismatch; }
  if(!s.lost && frame==s.lastAccepted) return Engine::Unchanged;
  const auto gray=descriptorImage(frame);
  QHash<int,int> votes;
  for(int y=0;y<gray.height();++y) for(int lane=0;lane<3;++lane) {
    const auto key=rowKey(gray.constScanLine(y),gray.width(),lane);
    if(!key) continue;
    auto found=s.rows.constFind(key);
    // Ubiquitous rows do not help identify a location and must not explode work.
    if(found==s.rows.cend() || found->size()>96) continue;
    for(int documentY:*found) {
      const int at=documentY-y;
      if(qMin(s.bottom,at+frame.height())-qMax(s.top,at)>=qMin(96,qMax(24,frame.height()/5))) ++votes[at];
    }
  }
  std::vector<std::pair<int,int>> candidates;
  for(auto it=votes.cbegin();it!=votes.cend();++it) if(it.value()>=8) candidates.emplace_back(it.value(),it.key());
  std::sort(candidates.begin(),candidates.end(),[](auto a,auto b) { return a.first>b.first; });
  if(candidates.empty()) { s.lost=true; return Engine::NoOverlap; }
  std::vector<std::pair<int,double>> valid;
  bool boundary=false;
  int considered=0;
  for(const auto &[count,at]:candidates) {
    if(count<qMax(8,candidates.front().first/8)) break;
    if(++considered>512) { s.lost=true; return Engine::NoOverlap; }
    const auto result=s.validate(frame,at);
    if(result.cacheError) return Engine::CacheError;
    boundary|=result.boundary;
    if(result.valid) valid.emplace_back(at,result.error);
    if(valid.size()>1) { s.lost=true; return Engine::NoOverlap; }
  }
  if(valid.size()!=1) { s.lost=true; return boundary?Engine::BoundaryMismatch:Engine::NoOverlap; }
  const int at=valid.front().first;
  const int leading=qMax(0,s.top-at),trailing=qMax(0,at+frame.height()-s.bottom);
  if(leading || trailing) {
    if(qint64(s.width)*(s.bottom-s.top+leading+trailing)>64000000 ||
       s.bottom-s.top+leading+trailing>100000 || s.parts.size()>=8192) return Engine::LimitReached;
    // Equal viewport sizes imply only one end can grow at a time. No committed
    // row is replaced, including when the user returns to an animated region.
    if(leading && !s.append(frame.copy(0,0,s.width,leading),at)) return Engine::CacheError;
    if(trailing && !s.append(frame.copy(0,frame.height()-trailing,s.width,trailing),s.bottom)) return Engine::CacheError;
  }
  const bool moved=at!=s.position;
  s.position=at; s.lastAccepted=frame; s.lost=false;
  return leading || trailing?Engine::Appended:moved?Engine::Revisited:Engine::Unchanged;
}
