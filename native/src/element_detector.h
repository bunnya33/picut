#pragma once
#include <QtCore/QList>
#include <QtCore/QRect>
#include <QtCore/QThread>
#include <Windows.h>
#include <objbase.h>

class ElementDetector : public QThread {
  Q_OBJECT
public:
  ElementDetector(HWND window, QList<QPair<QRect, QRect>> screens);
signals:
  void rectanglesReady(QList<QRect> rectangles);

protected:
  void run() override;

private:
  HWND window;
  QList<QPair<QRect, QRect>> screens;
};
