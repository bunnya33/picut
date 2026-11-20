#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QRect>
#include <QtGui/QKeySequence>
#include <functional>

class QApplication;
class QWidget;
inline void enableDpi() {}
QRect physicalRect(const QRect &logical);
QJsonObject memoryUsage();
inline bool excludeCapture(QWidget *) { return true; }
bool nativeHotkey(const QKeySequence &sequence, unsigned &modifiers, unsigned &key);
bool registerMacHotkey(int id, unsigned modifiers, unsigned key,
                       std::function<void()> action);
void unregisterMacHotkey(int id);
bool macLoginLaunch();
bool setLoginStartupEnabled(bool enabled, const QString &executable, QString *error = nullptr);
void macActivate();
void macSetClickThrough(QWidget *widget, bool enabled);
quint32 macClipboardSequence();
