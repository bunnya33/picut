#include "scroll_capture.h"
#include <QtCore/QFile>
#include <QtCore/QDebug>
#include <QtCore/QHash>
#include <QtCore/QCache>
#include <QtGui/QPainter>
#include "scroll_motion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <vector>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace {
// Disk latency must not delay motion tracking. Pending pixels remain readable
// until their immutable file has been committed, including an IO failure.
class RawWriter {
  struct Job { QString key; QImage image; quint64 serial; qint64 offset; };
  struct Pending { QImage image; quint64 serial; };
  QString storagePath;
  mutable std::mutex mutex;
  std::condition_variable ready,idle;
  std::deque<Job> jobs;
  QHash<QString,Pending> pending;
  QHash<QString,qint64> locations;
  std::thread worker;
  qint64 queuedBytes=0,nextOffset=0;
  quint64 serial=0;
  bool active=false,stopping=false,failed=false;
public:
  explicit RawWriter(QString path):storagePath(std::move(path)) {}
  ~RawWriter() {
    { std::lock_guard lock(mutex); stopping=true; }
    ready.notify_all();
    if(worker.joinable()) worker.join();
  }
  bool enqueue(const QString &key,const QImage &image) {
    std::lock_guard lock(mutex);
    const auto limit=qMax<qint64>(12*1024*1024,image.sizeInBytes());
    if(failed || stopping || queuedBytes+image.sizeInBytes()>limit) {
      qWarning()<<"Scroll cache writer unavailable or over budget"<<queuedBytes;
      return false;
    }
    if(!locations.contains(key)) { locations.insert(key,nextOffset); nextOffset+=image.sizeInBytes(); }
    const auto version=++serial;
    pending.insert(key,{image,version}); jobs.push_back({key,image,version,locations.value(key)});
    queuedBytes+=image.sizeInBytes();
    if(!worker.joinable()) worker=std::thread([this] { run(); });
    ready.notify_one(); return true;
  }
  QImage pixels(const QString &key) const {
    std::lock_guard lock(mutex);
    const auto found=pending.constFind(key);
    return found==pending.cend()?QImage():found->image;
  }
  bool load(const QString &key,QImage &image) const {
    qint64 offset;
    {
      std::lock_guard lock(mutex);
      const auto live=pending.constFind(key);
      if(live!=pending.cend()) { image=live->image; return true; }
      if(!locations.contains(key)) return false;
      offset=locations.value(key);
    }
    QFile file(storagePath);
    return file.open(QIODevice::ReadOnly) && file.seek(offset) &&
      file.read(reinterpret_cast<char *>(image.bits()),image.sizeInBytes())==image.sizeInBytes();
  }
  void waitIdle() {
    std::unique_lock lock(mutex);
    idle.wait(lock,[&] { return jobs.empty() && !active; });
  }
private:
  void run() {
    // Temporary cache data need visibility to readers, not a forced disk sync
    // or an atomic rename for every frame. Each immutable part owns a range;
    // seam updates reuse that range while pending pixels protect all readers.
    QFile file(storagePath);
    const bool opened=file.open(QIODevice::WriteOnly|QIODevice::Unbuffered);
    for(;;) {
      Job job;
      {
        std::unique_lock lock(mutex);
        ready.wait(lock,[&] { return stopping || !jobs.empty(); });
        if(jobs.empty()) return;
        job=std::move(jobs.front()); jobs.pop_front(); active=true;
      }
      const bool written=opened && file.seek(job.offset) &&
        file.write(reinterpret_cast<const char *>(job.image.constBits()),job.image.sizeInBytes())==job.image.sizeInBytes();
      {
        std::lock_guard lock(mutex);
        active=false; queuedBytes-=job.image.sizeInBytes();
        auto found=pending.find(job.key);
        if(written && found!=pending.end() && found->serial==job.serial) pending.erase(found);
        if(!written) { failed=true; qWarning()<<"Scroll cache write failed:"<<file.errorString(); }
      }
      idle.notify_all();
    }
  }
};

constexpr int kCell=32;
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
    // Coarse gradient sign and magnitude survive small brightness shifts.
    // The extra magnitude bins retain discriminating digits in repeated text.
    // Signatures only propose positions; original RGB overlap is validated.
    if(x>begin) {
      const int delta=v-int(row[x-1]);
      const int magnitude=std::abs(delta);
      const int quantized=magnitude>100?3:magnitude>40?2:magnitude>10?1:0;
      hash=(hash^quint64(quantized?(delta<0?quantized+4:quantized):0))*1099511628211ULL;
    }
  }
  return hi-lo>=20 && edges>=3 ? hash : 0;
}
}

struct ScrollStitcherV2::State {
  struct Part { QString path; QImage thumb; int y,height; std::vector<float> phase; };
  QTemporaryDir cache;
  RawWriter writer{cache.filePath("pixels.rgb")};
  std::map<int,Part> parts;
  QHash<quint64,QList<int>> rows;
  mutable QCache<int,QImage> pixelCache{16384};
  int width=0,viewportHeight=0,top=0,bottom=0,position=0,thumbWidth=280;
  double coordinate=0;
  quint64 revision=0,nextFile=0;
  QImage recent;
  cv::Mat recentGray;
  std::vector<cv::Point2f> recentFeatures;
  bool lost=false,motionTrusted=false;

  bool append(const QImage &pixels,int y,double phase=0) {
    if(!cache.isValid()) return false;
    const auto path=cache.filePath(QString::number(nextFile++)+".rgb");
    if(!writer.enqueue(path,pixels)) return false;
    auto gray=descriptorImage(pixels);
    const int tw=qMin(thumbWidth,pixels.width());
    auto thumb=pixels.scaled(tw,qMax(1,qRound(double(pixels.height())*tw/pixels.width())),
                            Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    parts.emplace(y,Part{path,thumb,y,pixels.height(),
                         std::vector<float>(size_t(pixels.height()),float(phase))});
    pixelCache.insert(y,new QImage(pixels),qMax(1,int(pixels.sizeInBytes()/1024)));
    for(int r=0;r<gray.height();++r) for(int lane=0;lane<3;++lane) {
      auto key=rowKey(gray.constScanLine(r),gray.width(),lane);
      if(key && rows[key].size()<=96) rows[key].append(y+r);
    }
    if(!width) { width=pixels.width(); top=y; bottom=y+pixels.height(); }
    else { top=qMin(top,y); bottom=qMax(bottom,y+pixels.height()); }
    while(thumbWidth>16 && qint64(bottom-top)*thumbWidth>qint64(width)*4096) {
      thumbWidth=qMax(16,thumbWidth/2);
      for(auto &[partY,part]:parts) {
        Q_UNUSED(partY);
        const int w=qMin(thumbWidth,width);
        part.thumb=part.thumb.scaled(w,qMax(1,qRound(double(part.height)*w/width)),
                                    Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
      }
    }
    ++revision;
    return true;
  }
  QImage read(int start,int count) const {
    if(count<1 || start<top || start+count>bottom) return {};
    QImage image(width,count,QImage::Format_RGB32);
    if(image.isNull()) return {};
    auto it=parts.upper_bound(start); if(it!=parts.begin()) --it;
    for(;it!=parts.end() && it->first<start+count;++it) {
      const auto &p=it->second;
      const int first=qMax(start,p.y),last=qMin(start+count,p.y+p.height);
      if(first>=last) continue;
      const auto pending=writer.pixels(p.path);
      if(!pending.isNull()) {
        std::memcpy(image.scanLine(first-start),pending.constScanLine(first-p.y),size_t(last-first)*width*4); continue;
      }
      auto pixels=pixelCache.object(p.y);
      if(!pixels) {
        QImage value(width,p.height,QImage::Format_RGB32);
        if(value.isNull() || !writer.load(p.path,value)) return {};
        if(value.sizeInBytes()/1024>pixelCache.maxCost()) {
          std::memcpy(image.scanLine(first-start),value.constScanLine(first-p.y),size_t(last-first)*width*4); continue;
        }
        pixelCache.insert(p.y,new QImage(value),qMax(1,int(value.sizeInBytes()/1024))); pixels=pixelCache.object(p.y);
      }
      std::memcpy(image.scanLine(first-start),pixels->constScanLine(first-p.y),size_t(last-first)*width*4);
    }
    return image;
  }
  enum class Seam { Unchanged, Rebased, Insufficient, Error };
  Seam rebaseSeam(const QImage &frame,int at,double phase,bool prepend,int renderedRows=0) {
    const int probeBegin=prepend?top:qMax(at,bottom-32);
    const int probeEnd=prepend?qMin(top+32,at+frame.height()):bottom;
    if(probeEnd<=probeBegin) return Seam::Unchanged;
    const auto original=read(probeBegin,probeEnd-probeBegin);
    if(original.isNull()) return Seam::Error;
    int differences=0;
    for(int y=probeBegin;y<probeEnd;y+=2) {
      const auto a=reinterpret_cast<const QRgb *>(original.constScanLine(y-probeBegin));
      const auto b=reinterpret_cast<const QRgb *>(frame.constScanLine(y-at));
      for(int x=0;x<width;x+=2) {
        const int delta=qMax(std::abs(qRed(a[x])-qRed(b[x])),qMax(std::abs(qGreen(a[x])-qGreen(b[x])),std::abs(qBlue(a[x])-qBlue(b[x]))));
        if(delta>7) ++differences;
      }
    }
    if(differences<4 && !renderedRows) return Seam::Unchanged;
    // The current source must cover the whole bounded seam tail. Otherwise
    // an animated object could be assembled from two incompatible phases.
    const int seamRows=qMax(144,renderedRows);
    const int begin=prepend?top:qMax(top,bottom-seamRows);
    const int end=prepend?qMin(bottom,top+seamRows):bottom;
    if(at>begin || at+frame.height()<end) return Seam::Insufficient;
    auto it=parts.upper_bound(begin); if(it!=parts.begin()) --it;
    for(;it!=parts.end() && it->first<end;++it) {
      auto &p=it->second;
      const int first=qMax(begin,p.y),last=qMin(end,p.y+p.height);
      if(first>=last) continue;
      const auto previous=read(p.y,p.height);
      if(previous.isNull()) return Seam::Error;
      auto updated=previous;
      updated.detach();
      for(int y=first;y<last;++y)
        std::memcpy(updated.scanLine(y-p.y),frame.constScanLine(y-at),size_t(width)*4);
      if(!writer.enqueue(p.path,updated)) return Seam::Error;
      const auto oldGray=descriptorImage(previous);
      for(int y=first;y<last;++y) for(int lane=0;lane<3;++lane) {
        const auto key=rowKey(oldGray.constScanLine(y-p.y),oldGray.width(),lane);
        if(key && rows[key].size()<=96) rows[key].removeAll(y);
      }
      const auto gray=descriptorImage(updated);
      for(int y=first;y<last;++y) p.phase[size_t(y-p.y)]=float(phase);
      const int tw=qMin(thumbWidth,width);
      p.thumb=updated.scaled(tw,qMax(1,qRound(double(p.height)*tw/width)),
                             Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
      for(int y=first;y<last;++y) for(int lane=0;lane<3;++lane) {
        const auto key=rowKey(gray.constScanLine(y-p.y),gray.width(),lane);
        if(key && rows[key].size()<=96) rows[key].append(y);
      }
      pixelCache.insert(p.y,new QImage(updated),qMax(1,int(updated.sizeInBytes()/1024)));
    }
    ++revision;
    return Seam::Rebased;
  }
  void remember(const QImage &frame,cv::Mat gray) {
    recent=frame;
    recentGray=std::move(gray);
    recentFeatures=ScrollMotion::features(recentGray);
    lost=false;
  }

  struct Validation {
    bool valid=false,boundary=false,cacheError=false;
    double error=1e10;
    bool perfect=false;
    int renderedRows=0;
  };
  Validation validateRenderedBoundary(const QImage &frame,int at) const {
    const bool prepend=at<top && at+frame.height()<=bottom;
    const bool append=at>=top && at+frame.height()>bottom;
    if(!prepend && !append) return {};
    const int begin=qMax(top,at),end=qMin(bottom,at+frame.height()),overlap=end-begin;
    if(overlap<96) return {};
    const auto reference=read(begin,overlap);
    if(reference.isNull()) return {false,false,true};
    const auto background=reference.pixel(0,prepend?0:overlap-1);
    int blankRows=0;
    for(int n=0;n<overlap;++n) {
      const int y=prepend?n:overlap-1-n;
      const auto row=reinterpret_cast<const QRgb *>(reference.constScanLine(y));
      bool flat=true;
      for(int x=0;x<width;++x)
        if(std::abs(qRed(row[x])-qRed(background))>2 ||
           std::abs(qGreen(row[x])-qGreen(background))>2 ||
           std::abs(qBlue(row[x])-qBlue(background))>2) { flat=false; break; }
      if(!flat) break;
      ++blankRows;
    }
    // Only a bounded blank interval attached to the growing document edge may
    // be filled. Every already drawn pixel outside that interval must agree.
    if(blankRows<8 || blankRows>qMin(512,viewportHeight/2) || overlap-blankRows<64) return {};
    const int stableBegin=prepend?blankRows:0,stableEnd=prepend?overlap:overlap-blankRows;
    int oldEvidence=0,newEvidence=0;
    for(int y=0;y<overlap;++y) {
      const auto a=reinterpret_cast<const QRgb *>(reference.constScanLine(y));
      const auto b=reinterpret_cast<const QRgb *>(frame.constScanLine(begin-at+y));
      const bool stable=y>=stableBegin && y<stableEnd;
      if(stable && std::memcmp(a,b,size_t(width)*4)) return {};
      if(y%2) continue;
      for(int x=2;x<width;x+=2) {
        if(stable && std::abs(qGray(a[x])-qGray(a[x-2]))>16) ++oldEvidence;
        if(!stable && std::abs(qGray(b[x])-qGray(b[x-2]))>16) ++newEvidence;
      }
    }
    if(oldEvidence<64 || newEvidence<64) return {};
    return {true,false,false,0,false,blankRows};
  }
  Validation validate(const QImage &frame,int at,double phase=0) const {
    const int begin=qMax(top,at),end=qMin(bottom,at+frame.height()),overlap=end-begin;
    if(overlap<qMin(96,qMax(24,viewportHeight/5))) return {};
    const auto reference=read(begin,overlap);
    if(reference.isNull()) return {false,false,true};
    std::vector<float> phases(static_cast<size_t>(overlap),0.0f);
    auto part=parts.upper_bound(begin); if(part!=parts.begin()) --part;
    for(;part!=parts.end() && part->first<end;++part) {
      const auto &p=part->second;
      for(int y=qMax(begin,p.y);y<qMin(end,p.y+p.height);++y)
        phases[size_t(y-begin)]=p.phase[size_t(y-p.y)];
    }
    bool exact=std::abs(phase)<0.00000001 &&
      std::all_of(phases.begin(),phases.end(),[](float value) { return std::abs(value)<0.00000001; });
    if(exact) for(int y=0;y<overlap;++y)
      if(std::memcmp(reference.constScanLine(y),frame.constScanLine(begin-at+y),size_t(width)*4)) { exact=false; break; }
    if(exact) {
      int evidence=0;
      for(int y=0;y<overlap;y+=2) {
        const auto pixels=reinterpret_cast<const QRgb *>(reference.constScanLine(y));
        for(int x=2;x<width;x+=2) if(std::abs(qGray(pixels[x])-qGray(pixels[x-2]))>16) ++evidence;
      }
      return evidence>=qMax(100,overlap*width/500)?Validation{true,false,false,0,true}:Validation{};
    }
    const int nx=(width+kCell-1)/kCell,ny=(overlap+kCell-1)/kCell;
    struct Cell { int samples=0,changed=0,texture=0; quint64 residual=0; };
    std::vector<Cell> cells(size_t(nx)*ny);
    int changes=0,texture=0;
    for(int y=0;y<overlap;y+=2) {
      const double delta=phase-phases[size_t(y)];
      const int shift=int(std::floor(delta));
      const double fraction=delta-shift;
      const int first=y+shift,second=first+1;
      if(first<0 || first>=overlap || (fraction>0.02 && second>=overlap) ||
         std::abs(phases[size_t(first)]-phases[size_t(y)])>0.03 ||
         (fraction>0.02 && std::abs(phases[size_t(second)]-phases[size_t(y)])>0.03)) continue;
      const auto a=reinterpret_cast<const QRgb *>(reference.constScanLine(first));
      const auto next=fraction>0.02?reinterpret_cast<const QRgb *>(reference.constScanLine(second)):a;
      const auto b=reinterpret_cast<const QRgb *>(frame.constScanLine(begin-at+y));
      for(int x=0;x<width;x+=2) {
        auto &cell=cells[size_t(y/kCell)*nx+x/kCell]; ++cell.samples;
        int difference;
        if(fraction<=0.02) {
          difference=qMax(std::abs(qRed(a[x])-qRed(b[x])),qMax(std::abs(qGreen(a[x])-qGreen(b[x])),std::abs(qBlue(a[x])-qBlue(b[x]))));
        } else {
          const int r=qRound(qRed(a[x])*(1-fraction)+qRed(next[x])*fraction);
          const int g=qRound(qGreen(a[x])*(1-fraction)+qGreen(next[x])*fraction);
          const int blue=qRound(qBlue(a[x])*(1-fraction)+qBlue(next[x])*fraction);
          difference=qMax(std::abs(r-qRed(b[x])),qMax(std::abs(g-qGreen(b[x])),std::abs(blue-qBlue(b[x]))));
        }
        cell.residual+=quint64(difference);
        if(difference>7) { ++cell.changed; ++changes; }
        if(x>=2 && std::abs(qGray(a[x])-qGray(a[x-2]))>16) { ++cell.texture; ++texture; }
      }
    }
    // Identical blank pixels provide no document-coordinate evidence.
    if(texture<qMax(100,overlap*width/500)) return {};
    std::vector<uchar> bad(cells.size());
    int badCount=0,badChanges=0,goodTexture=0;
    quint64 residual=0,totalResidual=0;
    int residualSamples=0,totalSamples=0;
    for(size_t i=0;i<cells.size();++i) {
      const auto &cell=cells[i];
      totalResidual+=cell.residual; totalSamples+=cell.samples;
      bad[i]=cell.changed>qMax(6,cell.samples/30);
      if(bad[i]) { ++badCount; badChanges+=cell.changed; }
      else { goodTexture+=cell.texture; residual+=cell.residual; residualSamples+=cell.samples; }
    }
    if(goodTexture<texture*2/3 || changes-badChanges>qMax(24,overlap*width/800)) return {};
    const double score=residualSamples?double(residual)/(255.*residualSamples)+
        (totalSamples?0.01*double(totalResidual)/(255.*totalSamples):0.):1.;
    if(!badCount) return {true,false,false,score};
    if(badCount>int(cells.size())/8) return {false,true,false};
    std::vector<uchar> visited(cells.size());
    int islands=0; bool boundary=false;
    for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
      const int seed=y*nx+x;
      if(!bad[seed] || visited[seed]) continue;
      int left=x,right=x,first=y,last=y,area=0;
      std::vector<int> queue{seed}; visited[seed]=1;
      for(size_t i=0;i<queue.size();++i) {
        const int index=queue[i],cy=index/nx,cx=index%nx;
        left=qMin(left,cx); right=qMax(right,cx); first=qMin(first,cy); last=qMax(last,cy); ++area;
        for(const auto [dx,dy]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}) {
          const int xx=cx+dx,yy=cy+dy;
          if(xx<0 || yy<0 || xx>=nx || yy>=ny) continue;
          const int neighbor=yy*nx+xx;
          if(bad[neighbor] && !visited[neighbor]) { visited[neighbor]=1; queue.push_back(neighbor); }
        }
      }
      ++islands;
      // A compact animated object may enter the overlap at its edge. A fixed
      // header or scrollbar spans a whole axis and fails these extent limits.
      if(((left==0 || right==nx-1) && right-left<=1 && last-first>=2) ||
         (right-left+1)*kCell>qMin(192,width/3) ||
         (last-first+1)*kCell>qMin(128,overlap/3) || area>24) boundary=true;
    }
    if(boundary || islands>8) return {false,boundary,false,double(changes)/(overlap*width/4)};
    return {true,false,false,score};
  }

  struct Candidate { int at=0; double phase=0; int votes=0; double weight=0; };
  // Only rows already verified at this coordinate are location evidence.
  // Novel pixels outside the overlap cannot prove that a short repeated sticker
  // is new content. Require several rare rows spread through the known overlap.
  bool anchored(const QImage &gray,int at) const {
    const int begin=qMax(top,at),end=qMin(bottom,at+viewportHeight);
    if(end-begin<qMin(96,qMax(24,viewportHeight/5))) return false;
    auto part=parts.upper_bound(begin); if(part!=parts.begin()) --part;
    for(;part!=parts.end() && part->first<end;++part)
      for(int y=qMax(begin,part->second.y);y<qMin(end,part->second.y+part->second.height);++y)
        if(std::abs(part->second.phase[size_t(y-part->second.y)])>0.00000001) return false;
    int first=-1,last=-1,count=0;
    for(int y=begin-at;y<end-at;++y) {
      for(int lane=0;lane<3;++lane) {
        const auto key=rowKey(gray.constScanLine(y),gray.width(),lane);
        if(!key) continue;
        const auto found=rows.constFind(key);
        if(found==rows.cend() || found->size()!=1 || found->front()-y!=at) continue;
        if(first<0) first=y;
        last=y; ++count; break;
      }
      if(count>=4 && last-first>=qMin(64,qMax(24,(end-begin)/4))) return true;
    }
    return false;
  }
  std::vector<Candidate> indexed(const QImage &gray) const {
    QHash<qint64,Candidate> votes;
    for(int y=2;y<gray.height()-2;y+=3) for(int lane=0;lane<3;++lane) {
      const auto key=rowKey(gray.constScanLine(y),gray.width(),lane);
      if(!key) continue;
      const auto found=rows.constFind(key);
      if(found==rows.cend() || found->size()>96) continue;
      for(int documentY:*found) {
        const int at=documentY-y;
        if(qMin(bottom,at+gray.height())-qMax(top,at)<qMin(96,qMax(24,viewportHeight/5))) continue;
        auto part=parts.upper_bound(documentY); if(part!=parts.begin()) --part;
        if(part==parts.end() || documentY<part->second.y || documentY>=part->second.y+part->second.height) continue;
        const auto phase=part->second.phase[size_t(documentY-part->second.y)];
        const qint64 voteKey=qint64(at)*64+qRound(phase*16)+16;
        auto &vote=votes[voteKey]; vote.at=at; vote.phase=phase; ++vote.votes;
        vote.weight+=1./found->size();
      }
    }
    std::vector<Candidate> result;
    for(auto it=votes.cbegin();it!=votes.cend();++it)
      if(it.value().votes>=3) result.push_back(it.value());
    std::sort(result.begin(),result.end(),[](auto a,auto b) {
      return a.weight!=b.weight?a.weight>b.weight:a.votes>b.votes;
    });
    if(result.size()>12) result.resize(12);
    return result;
  }
};

ScrollStitcherV2::ScrollStitcherV2():state(std::make_unique<State>()) {}
ScrollStitcherV2::~ScrollStitcherV2()=default;
void ScrollStitcherV2::finishPendingWrites() { state->writer.waitIdle(); }
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
    s.viewportHeight=frame.height(); s.remember(frame,ScrollMotion::frame(frame));
    return Engine::Started;
  }
  if(frame.size()!=QSize(s.width,s.viewportHeight)) { s.lost=true; return Engine::BoundaryMismatch; }
  if(frame==s.recent) return Engine::Unchanged;
  QElapsedTimer timing; timing.start();
  auto gray=ScrollMotion::frame(frame);
  const auto motion=ScrollMotion::track(s.recentGray,gray,s.recentFeatures);
  std::vector<State::Candidate> candidates;
  if(motion.reliable) {
    // Retain the fractional displacement through tracking, then snap only
    // the confirmed original-pixel output position to an integer row.
    const double coordinate=s.coordinate+motion.dy*double(frame.height())/gray.rows;
    const int center=qRound(coordinate);
    candidates.push_back({center,coordinate-center,0});
  }
  const auto motionNs=timing.nsecsElapsed();
  const auto signature=descriptorImage(frame);
  bool direct=false;
  State::Candidate directCandidate;
  if(motion.reliable) {
    const int center=qRound(s.coordinate+motion.dy*double(frame.height())/gray.rows);
    const auto result=s.validate(frame,center,0);
    if(result.cacheError) return Engine::CacheError;
    const int overlap=qMin(s.bottom,center+frame.height())-qMax(s.top,center);
    const bool continuous=s.motionTrusted && !s.lost && center>=s.top && overlap>=frame.height()*2/3;
    if(result.perfect && center>=s.top && (s.anchored(signature,center) || continuous)) {
      direct=true; directCandidate={center,0,0}; candidates={directCandidate};
    }
  }
  if(!direct && motion.reliable) {
    const int center=qRound(s.coordinate+motion.dy*double(frame.height())/gray.rows);
    // Try the exact integer phase before interpolated optical-flow estimates.
    // A fractional estimate must not taint a perfectly integer desktop image.
    candidates.insert(candidates.begin(),{center,0,0});
  }
  const auto indexed=direct?std::vector<State::Candidate>{}:s.indexed(signature);
  const auto indexNs=timing.nsecsElapsed();
  const bool debug=qEnvironmentVariableIsSet("PICUT_SCROLL_DEBUG");
  if(debug) qInfo()<<"motion"<<s.position<<motion.dy<<motion.reliable<<"index"<<indexed.size()
                  <<"trusted"<<s.motionTrusted<<"lost"<<s.lost;
  if(candidates.empty()) candidates=indexed;
  if(candidates.empty()) { s.lost=true; return Engine::NoOverlap; }
  struct Valid { State::Candidate candidate; double error; bool anchored,perfect,proven; int renderedRows; };
  std::vector<Valid> valid;
  bool boundary=false,unambiguous=direct;
  auto check=[&](State::Candidate candidate) {
    auto result=direct && candidate.at==directCandidate.at && candidate.phase==0?
      State::Validation{true,false,false,0,true}:s.validate(frame,candidate.at,candidate.phase);
    if(!result.valid && !result.cacheError && candidate.phase==0) {
      const auto rendered=s.validateRenderedBoundary(frame,candidate.at);
      if(rendered.valid || rendered.cacheError) result=rendered;
    }
    if(debug) qInfo()<<"candidate"<<candidate.at<<candidate.phase<<candidate.votes<<"valid"<<result.valid<<"error"<<result.error<<"boundary"<<result.boundary;
    if(result.cacheError) return false;
    boundary|=result.boundary;
    if(result.valid) {
      const bool anchored=candidate.phase==0 && s.anchored(signature,candidate.at);
      const bool proven=result.perfect && anchored && candidate.at>=s.top;
      if(debug) qInfo()<<"evidence"<<candidate.at<<"anchored"<<anchored<<"perfect"<<result.perfect;
      valid.push_back({candidate,result.error,anchored,result.perfect,proven,result.renderedRows});
      if(proven) unambiguous=true;
    }
    return true;
  };
  for(auto candidate:candidates) {
    if(!check(candidate)) return Engine::CacheError;
    if(unambiguous) break;
  }
  if(motion.reliable && valid.empty()) {
    const auto center=candidates.front();
    for(int offset:{-1,1,-2,2})
      if(!check({center.at+offset,center.phase-offset,0})) return Engine::CacheError;
  }
  // An exact row-signature match is useful for ambiguous repeated rows. Limit
  // independent original-pixel checks to the strongest historical positions.
  int alternatives=0;
  for(const auto &candidate:indexed) {
    if(unambiguous || alternatives>=12) break;
    if(candidate.weight<indexed.front().weight*0.20) break;
    if(std::any_of(candidates.begin(),candidates.end(),[&](auto item) {
      return item.at==candidate.at && std::abs(item.phase-candidate.phase)<0.00001;
    })) continue;
    if(!check(candidate)) return Engine::CacheError;
    ++alternatives;
  }
  // A proposed new prefix must also be checked against the original viewport.
  // Its full historical match may contain a small repaint while a repeated
  // sticker gives the prefix a superficially perfect, much shorter overlap.
  if(!unambiguous && std::any_of(valid.begin(),valid.end(),[&](const auto &v) { return v.candidate.at<s.top; }) &&
     std::none_of(valid.begin(),valid.end(),[](const auto &v) { return v.candidate.at==0 && v.candidate.phase==0; }))
    if(!check({0,0,0})) return Engine::CacheError;
  if(valid.empty()) { s.lost=true; return boundary?Engine::BoundaryMismatch:Engine::NoOverlap; }
  std::sort(valid.begin(),valid.end(),[](auto a,auto b){
    if(a.proven!=b.proven) return a.proven>b.proven;
    if(a.anchored!=b.anchored) return a.anchored>b.anchored;
    if(a.perfect!=b.perfect) return a.perfect>b.perfect;
    return a.error<b.error;
  });
  const auto chosen=valid.front().candidate;
  const int at=chosen.at;
  const int knownOverlap=qMin(s.bottom,at+frame.height())-qMax(s.top,at);
  // Repeated stickers can also yield a plausible, locally repainted match at
  // an old coordinate. A verified integer match with a long overlap and a
  // previously established optical reference is stronger evidence. A rejected
  // frame does not invalidate that last confirmed reference: subsequent stops
  // can recover at any pixel offset, not just at an earlier wheel stop.
  // Still reject a short edge match, an unestablished track, or two exact copies.
  const bool trackedExact=valid.front().perfect &&
      s.motionTrusted && motion.reliable && chosen.phase==0 &&
      at==qRound(s.coordinate+motion.dy*double(frame.height())/gray.rows) &&
      std::abs(at-s.position)<=frame.height()/3 && knownOverlap>=frame.height()*2/3;
  for(size_t i=1;!unambiguous && i<valid.size();++i)
    if(std::abs(valid[i].candidate.at+valid[i].candidate.phase-at-chosen.phase)>2 &&
       (!valid.front().anchored || valid[i].anchored) &&
       !(trackedExact && !valid[i].perfect)) {
      s.lost=true; return Engine::NoOverlap;
    }
  const auto validationNs=timing.nsecsElapsed();
  const int leading=qMax(0,s.top-at),trailing=qMax(0,at+frame.height()-s.bottom);
  if(leading || trailing) {
    if(qint64(s.width)*(s.bottom-s.top+leading+trailing)>64000000 ||
       s.bottom-s.top+leading+trailing>100000 || s.parts.size()>=8192) return Engine::LimitReached;
    // Only a bounded, unfrozen seam tail can change source on extension.
    // The rest of the confirmed document remains untouched.
    const auto seam=s.rebaseSeam(frame,at,chosen.phase,leading>0,valid.front().renderedRows);
    if(seam==State::Seam::Error) return Engine::CacheError;
    if(seam==State::Seam::Insufficient) { s.lost=true; return Engine::NoOverlap; }
    if(leading && !s.append(frame.copy(0,0,s.width,leading),at,chosen.phase)) return Engine::CacheError;
    if(trailing && !s.append(frame.copy(0,frame.height()-trailing,s.width,trailing),s.bottom,chosen.phase)) return Engine::CacheError;
    if(valid.front().renderedRows) qInfo()<<"Scroll filled blank boundary: prepend"<<(leading>0)
        <<"rows"<<valid.front().renderedRows<<"overlap"<<knownOverlap;
  }
  const auto storageNs=timing.nsecsElapsed();
  const bool moved=at!=s.position;
  s.position=at; s.coordinate=at+chosen.phase; s.motionTrusted=direct || valid.front().anchored || trackedExact;
  s.remember(frame,std::move(gray));
  if(qEnvironmentVariableIsSet("PICUT_SCROLL_PROFILE"))
    qInfo()<<"SCROLL_PHASE_NS"<<motionNs<<indexNs-motionNs<<validationNs-indexNs
           <<storageNs-validationNs<<timing.nsecsElapsed()-storageNs;
  return leading || trailing?Engine::Appended:moved?Engine::Revisited:Engine::Unchanged;
}
