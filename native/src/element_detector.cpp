#include "element_detector.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <UIAutomation.h>
#include <algorithm>
#include <objbase.h>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
ElementDetector::ElementDetector(HWND target,
                                 QList<QPair<QRect, QRect>> mappings)
    : window(target), screens(std::move(mappings)) {
  connect(this, &QThread::finished, this, &QObject::deleteLater);
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
          [this] {
            requestInterruption();
            wait();
          });
}
void ElementDetector::run() {
  auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  QList<QRect> rectangles;
  ComPtr<IUIAutomation> client;
  if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&client)))) {
    ComPtr<IUIAutomation2> timed;
    if (SUCCEEDED(client.As(&timed))) {
      timed->put_ConnectionTimeout(250);
      timed->put_TransactionTimeout(250);
    }
    ComPtr<IUIAutomationElement> root;
    ComPtr<IUIAutomationTreeWalker> walker;
    if (SUCCEEDED(client->ElementFromHandle(window, &root)) && root &&
        SUCCEEDED(client->get_ControlViewWalker(&walker))) {
      std::vector<ComPtr<IUIAutomationElement>> pending{root};
      QElapsedTimer timer;
      timer.start();
      int count = 0;
      while (!pending.empty() && ++count <= 400 && timer.elapsed() < 650 &&
             !isInterruptionRequested()) {
        auto element = pending.back();
        pending.pop_back();
        RECT r{};
        BOOL hidden = FALSE;
        if (SUCCEEDED(element->get_CurrentBoundingRectangle(&r)) &&
            SUCCEEDED(element->get_CurrentIsOffscreen(&hidden)) && !hidden &&
            r.right - r.left > 6 && r.bottom - r.top > 6) {
          QRect physical(r.left, r.top, r.right - r.left, r.bottom - r.top);
          for (const auto &mapping : screens)
            if (mapping.second.contains(physical.center())) {
              double sx =
                         double(mapping.first.width()) / mapping.second.width(),
                     sy = double(mapping.first.height()) /
                          mapping.second.height();
              rectangles.append(
                  {mapping.first.x() +
                       qRound((physical.x() - mapping.second.x()) * sx),
                   mapping.first.y() +
                       qRound((physical.y() - mapping.second.y()) * sy),
                   qRound(physical.width() * sx),
                   qRound(physical.height() * sy)});
              break;
            }
        }
        ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(element.Get(), &child);
        int children = 0;
        while (child && ++children < 100 && timer.elapsed() < 650) {
          pending.push_back(child);
          ComPtr<IUIAutomationElement> sibling;
          walker->GetNextSiblingElement(child.Get(), &sibling);
          child = sibling;
        }
      }
    }
  }
  std::sort(rectangles.begin(), rectangles.end(), [](QRect a, QRect b) {
    return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
  });
  rectangles.erase(std::unique(rectangles.begin(), rectangles.end()),
                   rectangles.end());
  emit rectanglesReady(rectangles);
  client.Reset();
  if (SUCCEEDED(initialized))
    CoUninitialize();
}
