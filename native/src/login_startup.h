#pragma once
#if defined(__APPLE__)
#include "platform.h"
#else
#include <QtCore/QDir>
#include <QtCore/QSettings>

inline QString loginStartupCommand(const QString &executable) {
  return QString("\"%1\" --tray").arg(QDir::toNativeSeparators(executable));
}

// The optional key lets native integration checks use an isolated HKCU key.
inline bool setLoginStartupEnabled(
    bool enabled, const QString &executable, QString *error = nullptr,
    const QString &key = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run")) {
  QSettings startup(key, QSettings::NativeFormat);
  if (enabled)
    startup.setValue("picut", loginStartupCommand(executable));
  else
    startup.remove("picut");
  startup.sync();
  if (startup.status() == QSettings::NoError) return true;
  if (error) *error = "无法更新 Windows 当前用户的开机启动项，请检查系统权限后重试。";
  return false;
}

#endif
