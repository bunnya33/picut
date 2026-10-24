#pragma once
#include <QtGui/QImage>
#include <opencv2/core.hpp>
#include <vector>

namespace ScrollMotion {
struct Estimate {
  double dy=0;
  int support=0;
  int regions=0;
  bool reliable=false;
};
cv::Mat frame(const QImage &image);
std::vector<cv::Point2f> features(const cv::Mat &gray);
Estimate track(const cv::Mat &previous,const cv::Mat &current,
               const std::vector<cv::Point2f> &points);
}
