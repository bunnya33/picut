#include "scroll_capture.h"
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QSaveFile>
#include <QtGui/QPainter>
#include <QtGui/QRegion>
#include <algorithm>
#include <array>
#include <cmath>
#include <bit>
#include <cstring>
#include <vector>

namespace {
constexpr double maximumAlignmentError=.8;
// Keep every vertical row. Only the horizontal axis is reduced, so the
// displacement is still measured in original pixels, including short overlaps.
QImage matchingImage(const QImage &image) {
  return image.convertToFormat(QImage::Format_Grayscale8)
      .scaled(qMin(480,image.width()),image.height(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
}
QImage stripThumbnail(const QImage &image) {
  const int width=qMin(280,image.width());
  return image.scaled(width,qMax(1,qRound(double(image.height())*width/image.width())),
                      Qt::IgnoreAspectRatio,Qt::SmoothTransformation).copy();
}
int regionArea(const QRegion &region) {
  int result=0; for(const auto &rect:region) result+=rect.width()*rect.height();
  return result;
}
bool isolatedChange(const QImage &a,const QImage &b,QRect *bounds=nullptr) {
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
  if(bounds && right>=left) *bounds=QRect(QPoint(left,top),QPoint(right,bottom));
  return right<left || (top>3 && bottom<a.height()-4);
}
struct Tile { int left,right; };
QList<Tile> allTiles(int width) {
  QList<Tile> result; const int size=qMax(12,width/12);
  for(int x=0;x<width;x+=size) result.append({x,qMin(width,x+size)});
  return result;
}
// Locate compact repainting objects in coordinates of the new frame after a
// proposed alignment. Several independent stickers are supported; the old
// bounding-box shortcut treated their combined extent as a whole-page change.
QList<uchar> changingColumns(const QImage &a,const QImage &b) {
  QList<uchar> columns(a.width(),0);
  for(int x=0;x<a.width();++x) {
    double sum=0; int count=0;
    for(int y=0;y<a.height();y+=3) { sum+=std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])); ++count; }
    columns[x]=sum>count*.35;
  }
  return columns;
}
QImage repaintMask(const QImage &a,const QImage &b,int shift,const QList<uchar> &moving={},QList<QRect> *areas=nullptr,bool *needsStrong=nullptr) {
  if(needsStrong) *needsStrong=false;
  constexpr int cw=8,ch=12;
  const int begin=qMax(0,-shift),end=qMin(b.height(),a.height()-shift);
  const int columns=(a.width()+cw-1)/cw,rows=(end-begin+ch-1)/ch;
  if(rows<2) return {};
  const auto columnsInMotion=shift ? (moving.isEmpty()?changingColumns(a,b):moving) : QList<uchar>();
  QList<int> changed(columns*rows,0),samples(columns*rows,0);
  for(int y=begin;y<end;y+=2) {
    auto p=a.constScanLine(y+shift),q=b.constScanLine(y);
    for(int x=0;x<a.width();x+=2) {
      if(shift && !columnsInMotion[x]) continue;
      const int cell=(y-begin)/ch*columns+x/cw; ++samples[cell];
      if(std::abs(int(p[x])-int(q[x]))>12) ++changed[cell];
    }
  }
  QList<uchar> active(changed.size(),0);
  for(int n=0;n<changed.size();++n) active[n]=changed[n]>= (shift?qMax(3,samples[n]/8):1);
  QList<QRect> patches;
  for(int seed=0;seed<active.size();++seed) if(active[seed]) {
    QList<int> pending{seed}; active[seed]=0;
    int left=columns,right=0,top=rows,bottom=0,total=0;
    for(int index=0;index<pending.size();++index) {
      const int cell=pending[index],x=cell%columns,y=cell/columns;
      left=qMin(left,x);right=qMax(right,x);top=qMin(top,y);bottom=qMax(bottom,y);total+=changed[cell];
      for(auto point:{QPoint(x-1,y),QPoint(x+1,y),QPoint(x,y-1),QPoint(x,y+1)}) {
        if(point.x()<0 || point.x()>=columns || point.y()<0 || point.y()>=rows) continue;
        const int next=point.y()*columns+point.x();
        if(active[next]) { active[next]=0; pending.append(next); }
      }
    }
    QRect patch(left*cw,begin+top*ch,(right-left+1)*cw,(bottom-top+1)*ch);
    patch=patch.intersected(QRect(0,begin,a.width(),end-begin));
    // Do not erase distributed text mismatches, fixed headers, or most of a
    // viewport to manufacture a match. Ordinary page motion fails these tests.
    // During motion, keep single text-line differences in the score: masking
    // changing message numbers could otherwise join unrelated repeated bubbles.
    const bool shortEdge=shift && patch.height()<36 && (patch.top()==begin || patch.bottom()==end-1);
    if(patch.width()<(shift?16:1) || (patch.height()<(shift?36:1) && (!shortEdge || !needsStrong)) || patch.width()>a.width()*.65 ||
       patch.height()>(end-begin)*.7 || patch.width()*patch.height()>a.width()*(end-begin)*.25 ||
       (shift && total*4<patch.width()*patch.height()*.18)) continue;
    bool strongPatch=shortEdge;
    // An animation repaints an object at the same aligned position. An object
    // disappearing into blank space (or appearing elsewhere) contradicts this
    // displacement; it must not be hidden by an animation mask.
    int oldEdges=0,newEdges=0;
    const auto evidence=patch.adjusted(-2,-2,2,2).intersected(QRect(0,begin,a.width()-1,end-begin));
    for(int y=evidence.top();y<=evidence.bottom();++y) for(int x=evidence.left();x<=evidence.right();++x) {
      auto p=a.constScanLine(y+shift),q=b.constScanLine(y);
      const auto pn=a.constScanLine(qMin(a.height()-1,y+shift+1)),qn=b.constScanLine(qMin(b.height()-1,y+1));
      oldEdges+=qMax(std::abs(int(p[x])-int(p[x+1])),std::abs(int(p[x])-int(pn[x])))>=8;
      newEdges+=qMax(std::abs(int(q[x])-int(q[x+1])),std::abs(int(q[x])-int(qn[x])))>=8;
    }
    if(qMin(oldEdges,newEdges)<4) {
      if(!needsStrong) continue;
      // A flat repainting overlay can cover blank page content. A textured
      // sticker vanishing into blank space is different: do not erase that
      // contradiction merely because other repeated messages happen to match.
      const auto interior=patch.adjusted(12,12,-12,-12);
      auto flat=[&](const QImage &image,int offset,int edges) {
        if(edges<4 || interior.width()<8 || interior.height()<8) return false;
        std::array<int,256> histogram{}; int count=0;
        for(int y=interior.top();y<=interior.bottom();y+=2) for(int x=interior.left();x<=interior.right();x+=2) {
          ++histogram[image.constScanLine(y+offset)[x]]; ++count;
        }
        int low=0,high=255,total=0;
        while(low<255 && total+histogram[low]<count/20) total+=histogram[low++];
        total=0; while(high>0 && total+histogram[high]<count/20) total+=histogram[high--];
        return high-low<=4;
      };
      if(!flat(a,shift,oldEdges) && !flat(b,0,newEdges)) continue;
      strongPatch=true;
    }
    if(strongPatch) *needsStrong=true;
    patches.append(patch.adjusted(-4,-4,4,4));
    if(patches.size()>(shift?8:64)) return {};
  }
  if(patches.isEmpty()) return {};
  QImage mask(a.size(),QImage::Format_Grayscale8); mask.fill(0);
  for(auto patch:patches) {
    patch=patch.intersected(QRect(0,begin,a.width(),end-begin));
    for(int y=patch.top();y<=patch.bottom();++y) std::fill(mask.scanLine(y)+patch.left(),mask.scanLine(y)+patch.right()+1,uchar(255));
  }
  int area=0;
  for(int y=begin;y<end;++y) for(int x=0;x<a.width();++x) area+=mask.constScanLine(y)[x]!=0;
  if(area>a.width()*(end-begin)*.35) return {};
  if(areas) for(auto patch:patches) areas->append(patch.intersected(QRect(0,begin,a.width(),end-begin)));
  return mask;
}
// Known dynamic rectangles belong to document coordinates. Translate them for
// each proposed displacement; current text outside them still has to align.
QImage combinedMask(QImage mask,const QImage &known,int shift) {
  if(known.isNull()) return mask;
  if(mask.isNull()) { mask=QImage(known.size(),QImage::Format_Grayscale8); mask.fill(0); }
  const int begin=qMax(0,-shift),end=qMin(known.height(),known.height()-shift);
  int area=0;
  for(int y=begin;y<end;++y) {
    auto row=mask.scanLine(y); auto prior=known.constScanLine(y+shift);
    for(int x=0;x<mask.width();++x) { row[x]|=prior[x]; area+=row[x]!=0; }
  }
  // A learned region cannot remove most of the evidence for a displacement.
  if(!area || area>mask.width()*(end-begin)*.5) return {};
  return mask;
}
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
struct Motion { int shift=0; double score=1e9; int blocks=0; bool ambiguous=false, boundary=false, masked=false; };
Motion evaluate(const QImage &a,const QImage &b,int shift,const QList<Tile> &tiles,const QImage &mask={},bool needsStrong=false) {
  const int begin=qMax(0,-shift),end=qMin(b.height(),a.height()-shift);
  QList<double> errors;
  int points=0,first=end,last=begin,goodBlocks=0;
  int left=a.width(),right=-1;
  QList<uchar> supportedRows(end-begin,0);
  const int guard=end-begin>=64?12:0;
  unsigned bands=0;
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
        if(!mask.isNull() && mask.constScanLine(y)[x]) { if(y>=begin+guard && y<end-guard) supportedRows[y-begin]=1; continue; }
        const int difference=std::abs(int(p[x])-int(q[x]));
        sum+=qMin(90,qMax(0,difference-6)); ++count;
        if(difference<=6 && y>=begin+guard && y<end-guard) { supportedRows[y-begin]=1; left=qMin(left,x); right=qMax(right,x); }
      }
    }
    if(count>=4) {
      errors.append(sum/count); points+=count;
      if(sum/count<1) {
        ++goodBlocks;first=qMin(first,top);last=qMax(last,qMin(end,top+block));
        bands|=1u<<qMin(5,(top-begin)*6/qMax(1,end-begin));
      }
    }
  }
  if(points<16 || errors.size()<2) return {};
  // Repeated avatars/stickers are not independent anchors. Require support on
  // both sides of the image, or separate vertical groups (e.g. a message and a
  // sticker). A single repeated object in a short overlap cannot locate a chat.
  int groups=0,run=0,gap=0;
  for(auto row:supportedRows) {
    if(row) { if(gap>=16) { if(run>=8) ++groups; run=0; } ++run; gap=0; }
    else ++gap;
  }
  if(run>=8) ++groups;
  const bool denseColumn=points>=qMax(200,int((end-begin)*qMax(1,right-left)*.025)) &&
      last-first>=qMin(96,(end-begin)*3/5);
  if(right-left<a.width()/2 && groups<2 && !denseColumn) return {};
  if(!mask.isNull() && (points<80 || goodBlocks<4 || last-first<qMin(96,(end-begin)/3))) return {};
  if(!mask.isNull() && shift && end-begin<64) return {};
  if(!mask.isNull() && !shift && std::popcount(bands)<3) return {};
  if(needsStrong && (points<400 || goodBlocks<12 || std::popcount(bands)<3 || right-left<a.width()/2)) return {};
  // Bound the influence of local animation; keep every informative block in
  // the score so a unique number can disambiguate otherwise repeated rows.
  double sum=0,worst=0; int bad=0;
  for(double value:errors) { sum+=qMin(12.,value); worst=qMax(worst,value); if(value>6) ++bad; }
  // One conflicting message must not disappear in an average of many identical
  // sticker/blank blocks. Every informative block outside dynamic objects must
  // support the same displacement.
  return {shift,qMax(worst,sum/errors.size()+2.*bad/errors.size()),int(errors.size()),false,false,!mask.isNull()};
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
Motion motion(const QImage &a,const QImage &b,int direction,const QImage &known={},const QImage &stationary={}) {
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
  if(!stationary.isNull()) refined.append(evaluate(a,b,0,allTiles(a.width()),stationary));
  int positive=0,negative=0;
  const int support=offsets.isEmpty()?0:qMax(3,votes.value(offsets.first())/8);
  for(int shift:offsets) {
    if(votes.value(shift)<support) break;
    if(!shift) continue;
    auto &count=shift>0?positive:negative;
    if(count++<24) refined.append(evaluate(a,b,shift,tiles));
  }
  // Repainting changes can make the true displacement score worse than a tiny
  // overlap with a different copy of the same sticker. Evaluate both kinds of
  // candidates before selecting, even when an ordinary candidate scores zero.
  QList<int> tested;
  for(int shift:offsets) {
    if(!shift) continue;
    if(votes.value(shift)<support) break;
    bool nearby=false; for(int old:tested) if(std::abs(old-shift)<=4) nearby=true;
    if(nearby) continue;
    if(tested.size()>=16) break;
    tested.append(shift);
    bool needsStrong=false;
    const auto mask=combinedMask(repaintMask(a,b,shift,{},nullptr,&needsStrong),known,shift);
    if(mask.isNull()) continue;
    const auto value=evaluate(a,b,shift,tiles,mask,needsStrong);
    if(value.score<=maximumAlignmentError) refined.append(value);
  }
  Motion best;
  for(auto value:refined) if(value.score<best.score) best=value;
  if(direction) for(auto value:refined)
    if(value.shift*direction>0 && value.score<=best.score+.015 && best.shift*direction<0) best=value;
  for(auto other:refined) {
    if(direction && best.shift*direction>0 && other.shift*direction<0) continue;
    if(std::abs(other.shift-best.shift)>2 && other.score<=maximumAlignmentError)
      best.ambiguous=true;
  }
  if(best.shift) best.boundary=reachesEdges(a,b,best.shift,tiles);
  return best;
}
int seamReplacement(const QImage &mask,int boundary,bool prepend) {
  if(mask.isNull()) return 0;
  auto active=[&](int y) {
    for(int x=0;x<mask.width();++x) if(mask.constScanLine(y)[x]) return true;
    return false;
  };
  if(prepend) {
    int end=boundary;
    while(end<mask.height() && active(end)) ++end;
    return end-boundary;
  }
  int start=boundary;
  while(start>0 && active(start-1)) --start;
  return boundary-start;
}
}
bool ScrollStitcher::append(const QImage &part, bool prepend, int replacePixels) {
  if (!cache.isValid() || part.isNull()) return false;
  const QString path = cache.filePath(QString::number(nextStrip++) + ".rgb");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(reinterpret_cast<const char *>(part.constBits()),part.sizeInBytes()) != part.sizeInBytes() ||
      !file.flush())
    return false;
  Strip strip{path, stripThumbnail(part),
              prepend ? 0 : height-replacePixels, part.height(),0,part.height()};
  if (prepend) {
    while(!strips.isEmpty() && strips.first().y+strips.first().height<=replacePixels) {
      QFile::remove(strips.first().path); strips.removeFirst();
    }
    if(!strips.isEmpty() && strips.first().y<replacePixels) {
      auto &first=strips.first(); const int clipped=replacePixels-first.y;
      first.sourceY+=clipped; first.height-=clipped; first.y=replacePixels;
      // Reclaim discarded head pixels. Commit before changing the offsets so
      // a failed optional compaction leaves the original strip readable.
      QImage kept(width,first.height,QImage::Format_RGB32);
      QFile original(first.path);
      if(!kept.isNull() && original.open(QIODevice::ReadOnly) &&
         original.seek(qint64(first.sourceY)*width*4) &&
         original.read(reinterpret_cast<char *>(kept.bits()),kept.sizeInBytes())==kept.sizeInBytes()) {
        original.close(); QSaveFile compact(first.path);
        if(compact.open(QIODevice::WriteOnly) &&
           compact.write(reinterpret_cast<const char *>(kept.constBits()),kept.sizeInBytes())==kept.sizeInBytes() && compact.commit()) {
          first.sourceY=0; first.sourceHeight=first.height;
          first.thumbnail=stripThumbnail(kept);
        }
      }
    }
    for(auto &existing:strips) existing.y+=part.height()-replacePixels;
    for(auto &area:repaintAreas) area.translate(0,part.height()-replacePixels);
    strips.prepend(std::move(strip));
  } else {
    const int cut=height-replacePixels;
    while(!strips.isEmpty() && strips.last().y>=cut) {
      QFile::remove(strips.last().path); strips.removeLast();
    }
    if(!strips.isEmpty() && strips.last().y+strips.last().height>cut) {
      auto &last=strips.last(); last.height=cut-last.y;
      QFile::resize(last.path,qint64(last.sourceY+last.height)*width*4);
    }
    strips.append(std::move(strip));
  }
  height += part.height()-replacePixels;
  ++revision;
  return true;
}
QImage ScrollStitcher::referenceMask() const {
  QImage mask;
  const QRect viewport(0,position,width,previous.height());
  for(const auto &area:repaintAreas) for(const auto &visible:area.intersected(viewport).translated(0,-position)) {
    if(mask.isNull()) { mask=QImage(previousGray.size(),QImage::Format_Grayscale8); mask.fill(0); }
    const int left=visible.left()*mask.width()/width;
    const int right=qMin(mask.width(),(visible.right()+1)*mask.width()/width+1);
    for(int y=visible.top();y<=visible.bottom();++y)
      std::fill(mask.scanLine(y)+left,mask.scanLine(y)+right,uchar(255));
  }
  return mask;
}
void ScrollStitcher::rememberRepaints(const QList<QRect> &areas,int at,int matchingWidth) {
  for(const auto &area:areas) {
    if(area.isEmpty()) continue;
    const int left=qMax(0,area.left()*width/matchingWidth);
    const int right=qMin(width,((area.right()+1)*width+matchingWidth-1)/matchingWidth);
    QRegion value(QRect(left,area.top()+at,right-left,area.height()));
    // Keep the exact union, including its gaps. Bounding-box unions can grow
    // through the untouched messages between separate animated objects.
    for(int n=0;n<repaintAreas.size();) {
      const auto overlap=value.intersected(repaintAreas[n]);
      const auto merged=value.united(repaintAreas[n]);
      const auto bounds=merged.boundingRect();
      if(regionArea(overlap)*2>=qMin(regionArea(value),regionArea(repaintAreas[n])) &&
         bounds.width()<=width*.65 && bounds.height()<=previous.height()*.65) {
        value=merged; repaintAreas.removeAt(n); n=0;
      } else ++n;
    }
    if(repaintAreas.size()<8192) repaintAreas.append(value);
  }
}
bool ScrollStitcher::refreshVisible(const QImage &frame,int at) {
  const QRect viewport(0,at,width,frame.height());
  QRegion updates;
  for(const auto &area:repaintAreas) {
    // Updating only half of an object would combine different animation frames.
    // Wait until the complete observed rectangle is inside the current viewport.
    const auto bounds=area.boundingRect();
    if(viewport.contains(bounds) && bounds.top()>at && bounds.bottom()<viewport.bottom())
      updates+=area.translated(0,-at);
  }
  if(updates.isEmpty()) return true;
  struct MappedStrip {
    Strip *strip;
    QFile file;
    uchar *pixels=nullptr;
    explicit MappedStrip(Strip *value):strip(value),file(value->path) {}
    ~MappedStrip() { if(pixels) file.unmap(pixels); }
  };
  std::vector<std::unique_ptr<MappedStrip>> mapped;
  // Open every affected strip before changing pixels, preserving the last
  // complete canvas if a cache file cannot be opened or mapped.
  for(auto &strip:strips) if(updates.intersects(QRect(0,strip.y-at,width,strip.height))) {
    auto item=std::make_unique<MappedStrip>(&strip);
    if(!item->file.open(QIODevice::ReadWrite) || item->file.size()% (qint64(width)*4) ||
       item->file.size()<qint64(strip.sourceY+strip.height)*width*4) return false;
    item->pixels=item->file.map(0,item->file.size());
    if(!item->pixels) return false;
    mapped.push_back(std::move(item));
  }
  for(auto &item:mapped) {
    auto &strip=*item->strip;
    QImage pixels(item->pixels,width,int(item->file.size()/(qint64(width)*4)),width*4,QImage::Format_RGB32);
    const int offset=at-strip.y+strip.sourceY;
    const auto region=updates.translated(0,offset).intersected(QRect(0,strip.sourceY,width,strip.height));
    bool changed=false;
    for(const auto &rect:region) for(int y=rect.top();y<=rect.bottom() && !changed;++y)
      changed=std::memcmp(pixels.constScanLine(y)+rect.left()*4,frame.constScanLine(y-offset)+rect.left()*4,size_t(rect.width())*4)!=0;
    if(!changed) continue;
    QPainter painter(&pixels); painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setClipRegion(region); painter.drawImage(0,offset,frame); painter.end();
    strip.sourceHeight=pixels.height();
    strip.thumbnail=stripThumbnail(pixels);
    ++revision;
  }
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
  auto stationary=[&](const QList<QRect> &areas) {
    rememberRepaints(areas,position,gray.width());
    if(!refreshVisible(value,position)) return CacheError;
    animationSeen=true; previous=value; previousGray=std::move(gray); return Unchanged;
  };
  const auto known=referenceMask();
  QList<QRect> stationaryAreas;
  auto stationaryMask=combinedMask(repaintMask(previousGray,gray,0,{},&stationaryAreas),known,0);
  QRect isolated;
  if(isolatedChange(previousGray,gray,&isolated) && !isolated.isEmpty()) {
    const auto area=isolated.adjusted(-2,-2,2,2).intersected(gray.rect());
    stationaryAreas.append(area);
    if(stationaryMask.isNull()) { stationaryMask=QImage(gray.size(),QImage::Format_Grayscale8); stationaryMask.fill(0); }
    for(int y=area.top();y<=area.bottom();++y)
      std::fill(stationaryMask.scanLine(y)+area.left(),stationaryMask.scanLine(y)+area.right()+1,uchar(255));
  }
  auto movement = motion(previousGray, gray, direction,known,stationaryMask);
  if (movement.score > maximumAlignmentError || movement.ambiguous) return NoOverlap;
  // Direction cannot supply a distance, but an animation-masked, contrary
  // displacement is not sufficient evidence to override current scroll input.
  if(direction && movement.shift*direction<0 && (movement.masked || animationSeen || movement.blocks<24)) return NoOverlap;
  if (!movement.shift) return stationary(stationaryAreas);
  if (!movement.boundary) return BoundaryMismatch;
  const auto columnsInMotion=changingColumns(previousGray,gray);
  QList<QRect> areas;
  bool confirmed=false; // The displacement has already passed independent validation.
  if(!repaintMask(previousGray,gray,movement.shift,columnsInMotion,&areas,&confirmed).isNull()) animationSeen=true;
  int nextPosition = position + movement.shift;
  const int leading = qMax(0,-nextPosition);
  const int extra = qMax(0,nextPosition + value.height() - height);
  if (extra || leading) {
    if (qint64(width) * (qint64(height) + extra + leading) > 64000000 ||
        height + extra + leading > 100000 || strips.size() >= 8192) return LimitReached;
    int replace=0;
    if(leading && animationSeen) {
      const auto frozen=matchingImage(readRows(0,value.height()));
      if(!frozen.isNull()) replace=seamReplacement(repaintMask(frozen,gray,nextPosition,columnsInMotion,nullptr,&confirmed),leading,true);
    }
    if (leading && !append(value.copy(0,0,width,leading+replace),true,replace)) return CacheError;
    nextPosition+=leading;
  }
  if (extra > 0) {
    int replace=0;
    if(animationSeen) {
      const auto frozen=matchingImage(readRows(height-value.height(),value.height()));
      if(!frozen.isNull()) replace=seamReplacement(repaintMask(frozen,gray,nextPosition-(height-value.height()),columnsInMotion,nullptr,&confirmed),value.height()-extra,false);
    }
    if (!append(value.copy(0,value.height()-extra-replace,width,extra+replace),false,replace)) return CacheError;
  }
  rememberRepaints(areas,nextPosition,gray.width());
  const bool refreshed=refreshVisible(value,nextPosition);
  // New rows may already have been committed. Keep the reference coordinates
  // consistent with that canvas even if an optional overlap refresh failed.
  previous = value; previousGray=std::move(gray); position = nextPosition;
  if(!refreshed) return CacheError;
  return extra > 0 || leading > 0 ? Appended : Revisited;
}
QImage ScrollStitcher::image() const {
  if (!width || !height) return {};
  return readRows(0,height);
}
QImage ScrollStitcher::readRows(int y,int count) const {
  if(y<0 || count<1 || y+count>height) return {};
  QImage result(width, count, QImage::Format_RGB32);
  if (result.isNull()) return {};
  QPainter painter(&result);
  for (const auto &strip : strips) {
    const int top=qMax(y,strip.y),bottom=qMin(y+count,strip.y+strip.height);
    if(top>=bottom) continue;
    QImage part(width,bottom-top,QImage::Format_RGB32);
    QFile file(strip.path);
    if (part.isNull() || !file.open(QIODevice::ReadOnly) ||
        !file.seek(qint64(strip.sourceY+top-strip.y)*width*4) ||
        file.read(reinterpret_cast<char *>(part.bits()),part.sizeInBytes()) != part.sizeInBytes()) return {};
    painter.drawImage(0, top-y, part);
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
  for (const auto &strip : strips) {
    const double top=std::round(target.y()+target.height()*strip.y/height);
    const double bottom=std::round(target.y()+target.height()*(strip.y+strip.height)/height);
    painter.drawImage(QRectF(target.x(),top,target.width(),bottom-top),
                       strip.thumbnail,QRectF(0,double(strip.sourceY)*strip.thumbnail.height()/strip.sourceHeight,
                           strip.thumbnail.width(),double(strip.height)*strip.thumbnail.height()/strip.sourceHeight));
  }
}
