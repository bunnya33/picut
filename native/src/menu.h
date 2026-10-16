#pragma once
#include <QtCore/QString>

// Display a hint in QMenu's shortcut column. Keyboard input remains owned by
// the global hotkey registry or the widget's existing key event handler.
inline QString menuLabel(const QString &label, const QString &shortcut = {}) {
  return shortcut.isEmpty() ? label : label + '\t' + shortcut;
}
