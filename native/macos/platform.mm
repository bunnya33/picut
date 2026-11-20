#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <mach/mach.h>

#include "platform.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QHash>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtWidgets/QWidget>

namespace {
struct Hotkey {
  EventHotKeyRef reference = nullptr;
  std::function<void()> action;
};
QHash<int, Hotkey> hotkeys;
constexpr OSType signature = 'pcut';

OSStatus handleHotkey(EventHandlerCallRef, EventRef event, void *) {
  EventHotKeyID key{};
  if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                        nullptr, sizeof key, nullptr, &key) != noErr ||
      key.signature != signature || !hotkeys.contains(int(key.id)))
    return eventNotHandledErr;
  QMetaObject::invokeMethod(QCoreApplication::instance(), [id = int(key.id)] {
    const auto action = hotkeys.value(id).action;
    if (action) action();
  }, Qt::QueuedConnection);
  return noErr;
}
}

QRect physicalRect(const QRect &logical) {
  auto screen = QGuiApplication::screenAt(logical.center());
  if (!screen) screen = QGuiApplication::primaryScreen();
  if (!screen) return logical;
  return {logical.topLeft(), logical.size() * screen->devicePixelRatio()};
}

QJsonObject memoryUsage() {
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&info), &count);
  return {{"working_set_mib", double(info.resident_size) / 1048576.0}};
}

bool nativeHotkey(const QKeySequence &sequence, unsigned &modifiers, unsigned &key) {
  if (sequence.isEmpty() || sequence.count() != 1) return false;
  const auto combination = sequence[0];
  const auto flags = combination.keyboardModifiers();
  modifiers = 0;
  if (flags & Qt::ControlModifier) modifiers |= cmdKey;
  if (flags & Qt::MetaModifier) modifiers |= controlKey;
  if (flags & Qt::AltModifier) modifiers |= optionKey;
  if (flags & Qt::ShiftModifier) modifiers |= shiftKey;

  static const QHash<int, unsigned> special = {
    {Qt::Key_F1,kVK_F1}, {Qt::Key_F2,kVK_F2}, {Qt::Key_F3,kVK_F3},
    {Qt::Key_F4,kVK_F4}, {Qt::Key_F5,kVK_F5}, {Qt::Key_F6,kVK_F6},
    {Qt::Key_F7,kVK_F7}, {Qt::Key_F8,kVK_F8}, {Qt::Key_F9,kVK_F9},
    {Qt::Key_F10,kVK_F10}, {Qt::Key_F11,kVK_F11}, {Qt::Key_F12,kVK_F12},
    {Qt::Key_F13,kVK_F13}, {Qt::Key_F14,kVK_F14}, {Qt::Key_F15,kVK_F15},
    {Qt::Key_F16,kVK_F16}, {Qt::Key_F17,kVK_F17}, {Qt::Key_F18,kVK_F18},
    {Qt::Key_F19,kVK_F19}, {Qt::Key_F20,kVK_F20}, {Qt::Key_Space,kVK_Space},
    {Qt::Key_Escape,kVK_Escape}, {Qt::Key_Tab,kVK_Tab},
    {Qt::Key_Return,kVK_Return}, {Qt::Key_Enter,kVK_ANSI_KeypadEnter},
    {Qt::Key_Backspace,kVK_Delete}, {Qt::Key_Delete,kVK_ForwardDelete},
    {Qt::Key_Home,kVK_Home}, {Qt::Key_End,kVK_End},
    {Qt::Key_PageUp,kVK_PageUp}, {Qt::Key_PageDown,kVK_PageDown},
    {Qt::Key_Left,kVK_LeftArrow}, {Qt::Key_Right,kVK_RightArrow},
    {Qt::Key_Up,kVK_UpArrow}, {Qt::Key_Down,kVK_DownArrow}
  };
  if (const auto found = special.constFind(combination.key()); found != special.cend()) {
    key = *found;
    return true;
  }
  if (combination.key() < 32 || combination.key() > 0xffff) return false;
  auto source = TISCopyCurrentKeyboardLayoutInputSource();
  if (!source) return false;
  auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
  if (data) {
    const auto layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
    for (unsigned shift : {0u, unsigned(shiftKey)}) {
      for (unsigned code = 0; code < 128; ++code) {
        UInt32 dead = 0;
        UniChar chars[4]{};
        UniCharCount count = 0;
        if (UCKeyTranslate(layout, code, kUCKeyActionDown, shift >> 8,
                           LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask, &dead,
                           4, &count, chars) == noErr && count == 1 &&
            QChar(chars[0]).toUpper().unicode() == combination.key()) {
          CFRelease(source);
          key = code;
          modifiers |= shift;
          return true;
        }
      }
    }
  }
  CFRelease(source);
  return false;
}

bool registerMacHotkey(int id, unsigned modifiers, unsigned key,
                       std::function<void()> action) {
  static const bool installed = [] {
    const EventTypeSpec press{kEventClassKeyboard, kEventHotKeyPressed};
    return InstallEventHandler(GetApplicationEventTarget(), handleHotkey, 1,
                               &press, nullptr, nullptr) == noErr;
  }();
  EventHotKeyRef reference = nullptr;
  if (!installed || hotkeys.contains(id) ||
      RegisterEventHotKey(key, modifiers, {signature, UInt32(id)},
                          GetApplicationEventTarget(), 0, &reference) != noErr)
    return false;
  hotkeys.insert(id, {reference, std::move(action)});
  return true;
}

void unregisterMacHotkey(int id) {
  if (const auto key = hotkeys.take(id); key.reference)
    UnregisterEventHotKey(key.reference);
}

bool macLoginLaunch() {
  auto event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
  if (event.eventClass != kCoreEventClass || event.eventID != kAEOpenApplication) return false;
  return [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem;
}

bool setLoginStartupEnabled(bool enabled, const QString &, QString *error) {
  SMAppService *service = SMAppService.mainAppService;
  const auto status = service.status;
  if (enabled && status == SMAppServiceStatusEnabled) return true;
  if (!enabled && status != SMAppServiceStatusEnabled &&
      status != SMAppServiceStatusRequiresApproval) return true;
  NSError *failure = nil;
  const bool success = enabled ? [service registerAndReturnError:&failure]
                               : [service unregisterAndReturnError:&failure];
  if (success && (!enabled || service.status == SMAppServiceStatusEnabled)) return true;
  if (error)
    *error = QStringLiteral("请在系统设置 → 通用 → 登录项与扩展中允许 picut 登录时打开。") +
             (failure ? "\n" + QString::fromNSString(failure.localizedDescription) : QString());
  return false;
}

void macActivate() {
  [NSApp activate];
}

void macSetClickThrough(QWidget *widget, bool enabled) {
  NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
  view.window.ignoresMouseEvents = enabled;
}

quint32 macClipboardSequence() {
  return quint32(NSPasteboard.generalPasteboard.changeCount);
}
