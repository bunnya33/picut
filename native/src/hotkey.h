#pragma once
#include <QtCore/QJsonObject>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtWidgets/QKeySequenceEdit>
#include <QtWidgets/QLineEdit>
#ifdef Q_OS_MACOS
#include "platform.h"
#else
#include <Windows.h>
#endif

inline QKeySequence hotkeySequence(QString text) {
  text.replace("Win+", "Meta+", Qt::CaseInsensitive);
  return QKeySequence::fromString(text, QKeySequence::PortableText);
}
inline QString hotkeyDisplay(const QString &text) {
#ifdef Q_OS_MACOS
  return hotkeySequence(text).toString(QKeySequence::NativeText);
#else
  return text;
#endif
}

#ifdef Q_OS_MACOS
inline constexpr auto defaultPinHotkey = "Ctrl+Alt+Shift+P";
inline QKeySequence pinShortcut(const QJsonObject &config) {
  return hotkeySequence(config["hotkeys"].toObject()["pin"].toString());
}
inline bool matchesPinShortcut(const QKeyEvent *event, const QJsonObject &config) {
  return pinShortcut(config) == QKeySequence(event->keyCombination());
}
#else
inline constexpr auto defaultPinHotkey = "F3";
#endif

#ifndef Q_OS_MACOS
inline bool nativeHotkey(const QKeySequence &sequence, UINT &modifiers, UINT &vk) {
  modifiers = MOD_NOREPEAT; vk = 0;
  if (sequence.isEmpty() || sequence.count() != 1) return false;
  const auto combination = sequence[0];
  const auto mods = combination.keyboardModifiers();
  if (mods & Qt::ControlModifier) modifiers |= MOD_CONTROL;
  if (mods & Qt::AltModifier) modifiers |= MOD_ALT;
  if (mods & Qt::ShiftModifier) modifiers |= MOD_SHIFT;
  if (mods & Qt::MetaModifier) modifiers |= MOD_WIN;
  const int key = combination.key();
  if (key >= Qt::Key_F1 && key <= Qt::Key_F24) vk = VK_F1 + key - Qt::Key_F1;
  else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) vk = key;
  else {
    switch (key) {
    case Qt::Key_Space: vk = VK_SPACE; break;
    case Qt::Key_Escape: vk = VK_ESCAPE; break;
    case Qt::Key_Tab: vk = VK_TAB; break;
    case Qt::Key_Return: case Qt::Key_Enter: vk = VK_RETURN; break;
    case Qt::Key_Backspace: vk = VK_BACK; break;
    case Qt::Key_Delete: vk = VK_DELETE; break;
    case Qt::Key_Insert: vk = VK_INSERT; break;
    case Qt::Key_Home: vk = VK_HOME; break;
    case Qt::Key_End: vk = VK_END; break;
    case Qt::Key_PageUp: vk = VK_PRIOR; break;
    case Qt::Key_PageDown: vk = VK_NEXT; break;
    case Qt::Key_Left: vk = VK_LEFT; break;
    case Qt::Key_Right: vk = VK_RIGHT; break;
    case Qt::Key_Up: vk = VK_UP; break;
    case Qt::Key_Down: vk = VK_DOWN; break;
    case Qt::Key_Print: vk = VK_SNAPSHOT; break;
    case Qt::Key_Pause: vk = VK_PAUSE; break;
    default:
      if (key > 0 && key < 128) {
        const SHORT mapped = VkKeyScanW(WCHAR(key));
        if (mapped != -1) {
          vk = LOBYTE(mapped);
          if (HIBYTE(mapped) & 1) modifiers |= MOD_SHIFT;
          if (HIBYTE(mapped) & 2) modifiers |= MOD_CONTROL;
          if (HIBYTE(mapped) & 4) modifiers |= MOD_ALT;
        }
      }
    }
  }
  return vk != 0;
}

#endif

class HotkeyEdit : public QKeySequenceEdit {
public:
  explicit HotkeyEdit(const QString &binding, QWidget *parent = nullptr)
      : QKeySequenceEdit(hotkeySequence(binding), parent) {
    setMaximumSequenceLength(1);
    setClearButtonEnabled(true);
    setMinimumWidth(230);
    setToolTip("点击后直接按组合键；点击 × 可停用此快捷键");
    if (auto line = findChild<QLineEdit *>()) {
      line->setPlaceholderText("点击后按下组合键");
      line->setContextMenuPolicy(Qt::NoContextMenu);
    }
  }
};
