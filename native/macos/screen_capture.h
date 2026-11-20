#pragma once
#include <QtCore/QRect>
#include <QtGui/QImage>
#include <memory>
#include <functional>

struct DesktopSnapshot;
DesktopSnapshot captureMacDesktop();
QImage captureMacRegion(QRect region);
bool checkMacScreenPermission(bool request);
bool checkMacMicrophonePermission();
void showMacScreenSettings();
void activateMacWindow(quintptr window);

class MacScreenStream {
public:
  MacScreenStream(QRect logical, QSize pixels, unsigned fps, bool cursor,
                  quintptr window = 0, bool systemAudio = false,
                  bool microphone = false, const QString &micId = {},
                  std::function<void(int, const void *)> audio = {});
  ~MacScreenStream();

  QImage frame() const;
  QString failure() const;
  void moveTo(QRect logical);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

bool verifyMacRecording(const QString &folder);
bool verifyMacScroll(const QString &folder);
