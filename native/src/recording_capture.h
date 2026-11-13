#pragma once
#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtGui/QImage>
#include <memory>

struct RecordingWindowInfo {
  QRect physical, logical;
  QString title;
  bool valid = false, minimized = false;
};
RecordingWindowInfo recordingWindowInfo(quintptr window);
quintptr recordingWindowAt(QPoint logicalPoint);

// Created and used on the recording thread. Captures the window itself,
// including when another application covers it.
class RecordingWindowCapture {
public:
  RecordingWindowCapture(quintptr window, bool cursor);
  ~RecordingWindowCapture();
  QImage grab();
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
