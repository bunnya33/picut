#include "scroll_motion.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace ScrollMotion {
cv::Mat frame(const QImage &image) {
  static std::once_flag threads;
  std::call_once(threads,[]{ cv::setNumThreads(1); });
  const cv::Mat bgra(image.height(),image.width(),CV_8UC4,
                     const_cast<uchar *>(image.constBits()),image.bytesPerLine());
  cv::Mat gray,small;
  cv::cvtColor(bgra,gray,cv::COLOR_BGRA2GRAY);
  const int width=qMin(384,image.width());
  if(width==image.width()) return gray;
  cv::resize(gray,small,cv::Size(width,qMax(1,qRound(double(image.height())*width/image.width()))),
             0,0,cv::INTER_AREA);
  return small;
}

std::vector<cv::Point2f> features(const cv::Mat &gray) {
  std::vector<cv::Point2f> candidates,selected;
  cv::goodFeaturesToTrack(gray,candidates,320,0.012,7.0,cv::noArray(),3,false);
  std::array<int,20> cells{};
  for(const auto &p:candidates) {
    if(p.x<9 || p.y<9 || p.x>=gray.cols-9 || p.y>=gray.rows-9) continue;
    const int cell=qMin(3,int(p.y*4/gray.rows))*5+qMin(4,int(p.x*5/gray.cols));
    if(cells[cell]++>=6) continue;
    selected.push_back(p);
    if(selected.size()==96) break;
  }
  return selected;
}

Estimate track(const cv::Mat &previous,const cv::Mat &current,
               const std::vector<cv::Point2f> &points) {
  if(points.size()<12) return {};
  std::vector<cv::Point2f> forward,backward;
  std::vector<uchar> status,backStatus;
  std::vector<float> error,backError;
  const auto criteria=cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,15,0.04);
  cv::calcOpticalFlowPyrLK(previous,current,points,forward,status,error,
                            cv::Size(17,17),3,criteria);
  cv::calcOpticalFlowPyrLK(current,previous,forward,backward,backStatus,backError,
                            cv::Size(17,17),3,criteria);
  struct Observation { float dy; int cell; };
  std::vector<Observation> observations;
  for(size_t i=0;i<points.size();++i) {
    if(!status[i] || !backStatus[i] ||
       cv::norm(points[i]-backward[i])>1.4 ||
       std::abs(points[i].x-forward[i].x)>2.2) continue;
    const auto &p=points[i];
    observations.push_back({p.y-forward[i].y,
      qMin(3,int(p.y*4/previous.rows))*4+qMin(3,int(p.x*4/previous.cols))});
  }
  if(observations.size()<10) return {};
  std::sort(observations.begin(),observations.end(),
            [](const auto &a,const auto &b){return a.dy<b.dy;});
  Estimate best;
  for(size_t i=0;i<observations.size();++i) {
    if(i && observations[i].dy-observations[i-1].dy<0.4f) continue;
    std::array<int,16> regions{};
    int support=0,spread=0;
    for(const auto &o:observations) if(std::abs(o.dy-observations[i].dy)<=1.25f) {
      ++support;
      if(!regions[o.cell]++) ++spread;
    }
    if(spread>best.regions || (spread==best.regions && support>best.support))
      best={observations[i].dy,support,spread,false};
  }
  best.reliable=best.support>=qMax(9,int(observations.size()*0.47)) && best.regions>=5;
  if(best.reliable) {
    std::vector<float> inliers;
    for(const auto &o:observations) if(std::abs(o.dy-best.dy)<=1.25)
      inliers.push_back(o.dy);
    std::nth_element(inliers.begin(),inliers.begin()+inliers.size()/2,inliers.end());
    best.dy=inliers[inliers.size()/2];
  }
  return best;
}
}
