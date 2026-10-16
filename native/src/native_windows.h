#pragma once
#include <QtCore/QJsonObject>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtWidgets/QWidget>
#include <Windows.h>
#include <objbase.h>
#include <propkeydef.h>
#include <psapi.h>

inline void enableDpi() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
inline bool excludeCapture(QWidget *widget) {
  auto window = reinterpret_cast<HWND>(widget->winId());
  DWORD affinity = 0;
  return SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) &&
         GetWindowDisplayAffinity(window, &affinity) &&
         affinity == WDA_EXCLUDEFROMCAPTURE;
}
inline QJsonObject memoryUsage() {
  PROCESS_MEMORY_COUNTERS_EX value{};
  value.cb = sizeof(value);
  GetProcessMemoryInfo(GetCurrentProcess(),
                       reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&value),
                       sizeof(value));
  return {{"working_set_mib", double(value.WorkingSetSize) / 1048576.0},
          {"private_commit_mib", double(value.PrivateUsage) / 1048576.0},
          {"private_working_set_mib", [&] {
             PROCESS_MEMORY_COUNTERS_EX2 extended{};
             extended.cb = sizeof(extended);
             return GetProcessMemoryInfo(
                        GetCurrentProcess(),
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&extended),
                        sizeof(extended))
                        ? QJsonValue(double(extended.PrivateWorkingSetSize) /
                                     1048576.0)
                        : QJsonValue();
           }()}};
}
inline QRect physicalRect(const QRect &logical) {
  auto screen = QGuiApplication::screenAt(logical.center());
  if (!screen)
    screen = QGuiApplication::primaryScreen();
  struct MonitorLookup {
    QString name;
    QRect rectangle;
  } lookup{screen->name(), {}};
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM raw) -> BOOL {
        auto lookup = reinterpret_cast<MonitorLookup *>(raw);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)) &&
            lookup->name.compare(QString::fromWCharArray(info.szDevice),
                                 Qt::CaseInsensitive) == 0) {
          lookup->rectangle = QRect(info.rcMonitor.left, info.rcMonitor.top,
                                    info.rcMonitor.right - info.rcMonitor.left,
                                    info.rcMonitor.bottom - info.rcMonitor.top);
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&lookup));
  if (!lookup.rectangle.isEmpty()) {
    auto offset = logical.topLeft() - screen->geometry().topLeft();
    double sx = double(lookup.rectangle.width()) / screen->geometry().width(),
           sy = double(lookup.rectangle.height()) / screen->geometry().height();
    return {lookup.rectangle.x() + qRound(offset.x() * sx),
            lookup.rectangle.y() + qRound(offset.y() * sy),
            qRound(logical.width() * sx), qRound(logical.height() * sy)};
  }
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(reinterpret_cast<LPCWSTR>(screen->name().utf16()),
                           ENUM_CURRENT_SETTINGS, &mode)) {
    auto offset = logical.topLeft() - screen->geometry().topLeft();
    auto ratio = screen->devicePixelRatio();
    return {mode.dmPosition.x + qRound(offset.x() * ratio),
            mode.dmPosition.y + qRound(offset.y() * ratio),
            qRound(logical.width() * ratio), qRound(logical.height() * ratio)};
  }
  auto ratio = screen->devicePixelRatio();
  return {qRound(logical.x() * ratio), qRound(logical.y() * ratio),
          qRound(logical.width() * ratio), qRound(logical.height() * ratio)};
}
