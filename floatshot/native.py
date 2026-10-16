"""Small, typed Win32 bridge: hotkeys, monitor coordinates and capture exclusion."""
from __future__ import annotations

import ctypes as ct
import os
import sys
import fnmatch
from ctypes import wintypes as wt

from PySide6.QtCore import QAbstractNativeEventFilter, QPoint, QRect, Qt
from PySide6.QtGui import QGuiApplication, QKeySequence

WINDOWS = sys.platform == "win32"
if WINDOWS:
    user = ct.WinDLL("user32", use_last_error=True)
    user.EnumWindows.argtypes = [ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM), wt.LPARAM]
    user.GetWindowRect.argtypes = [wt.HWND, ct.POINTER(wt.RECT)]
    user.IsWindowVisible.argtypes = [wt.HWND]
    user.GetWindowThreadProcessId.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
    user.GetWindowThreadProcessId.restype = wt.DWORD
    user.SetWindowDisplayAffinity.argtypes = [wt.HWND, wt.DWORD]
    user.RegisterHotKey.argtypes = [wt.HWND, ct.c_int, wt.UINT, wt.UINT]
    user.UnregisterHotKey.argtypes = [wt.HWND, ct.c_int]


def enable_dpi():
    if WINDOWS:
        try:
            user.SetProcessDpiAwarenessContext.argtypes = [ct.c_void_p]
            user.SetProcessDpiAwarenessContext(ct.c_void_p(-4))
        except (AttributeError, OSError):
            pass


def exclude_from_capture(widget):
    if not WINDOWS:
        return False
    # Windows 10 2004+: omit our controls instead of recording a black rectangle.
    return bool(user.SetWindowDisplayAffinity(int(widget.winId()), 0x11))


def watch_session(widget):
    if WINDOWS:
        wts = ct.WinDLL("wtsapi32", use_last_error=True)
        wts.WTSRegisterSessionNotification.argtypes = [wt.HWND, wt.DWORD]
        wts.WTSRegisterSessionNotification(int(widget.winId()), 0)


class MonitorInfo(ct.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("rcMonitor", wt.RECT), ("rcWork", wt.RECT),
                ("dwFlags", wt.DWORD), ("szDevice", wt.WCHAR * 32)]


def physical_monitors():
    if not WINDOWS:
        return []
    result = []
    callback_type = ct.WINFUNCTYPE(wt.BOOL, wt.HANDLE, wt.HDC, ct.POINTER(wt.RECT), wt.LPARAM)
    user.GetMonitorInfoW.argtypes = [wt.HANDLE, ct.POINTER(MonitorInfo)]
    user.EnumDisplayMonitors.argtypes = [wt.HDC, ct.POINTER(wt.RECT), callback_type, wt.LPARAM]

    @callback_type
    def callback(handle, dc, rect, data):
        info = MonitorInfo()
        info.cbSize = ct.sizeof(info)
        if user.GetMonitorInfoW(handle, ct.byref(info)):
            r = info.rcMonitor
            result.append((info.szDevice, QRect(r.left, r.top, r.right-r.left, r.bottom-r.top)))
        return True

    user.EnumDisplayMonitors(None, None, callback, 0)
    return result


def to_physical(rect: QRect):
    for screen in QGuiApplication.screens():
        logical = screen.geometry()
        if logical.contains(rect):
            mapping = dict(physical_monitors())
            native = mapping.get(screen.name())
            ratio = screen.devicePixelRatio()
            if native is None:
                native = QRect(round(logical.x()*ratio), round(logical.y()*ratio),
                               round(logical.width()*ratio), round(logical.height()*ratio))
            sx, sy = native.width()/logical.width(), native.height()/logical.height()
            return {"left": native.left()+round((rect.left()-logical.left())*sx),
                    "top": native.top()+round((rect.top()-logical.top())*sy),
                    "width": max(2, round(rect.width()*sx)), "height": max(2, round(rect.height()*sy))}
    raise ValueError("录屏选区需要位于一个显示器内；多屏合并录制将在后续版本加入。")


def to_logical(rect: QRect, mapping=None):
    mapping = physical_monitors() if mapping is None else mapping
    screens = {screen.name(): screen for screen in QGuiApplication.screens()}
    for name, native in mapping:
        if native.contains(rect.center()) and name in screens:
            logical = screens[name].geometry()
            sx, sy = logical.width()/native.width(), logical.height()/native.height()
            return QRect(logical.x()+round((rect.x()-native.x())*sx),
                         logical.y()+round((rect.y()-native.y())*sy),
                         round(rect.width()*sx), round(rect.height()*sy))
    return rect


def desktop_windows():
    if not WINDOWS:
        return []
    result = []
    mapping = physical_monitors()
    child_callback_type = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    user.EnumChildWindows.argtypes = [wt.HWND, child_callback_type, wt.LPARAM]
    user.IsIconic.argtypes = [wt.HWND]

    @ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def callback(hwnd, data):
        pid = wt.DWORD()
        user.GetWindowThreadProcessId(hwnd, ct.byref(pid))
        if pid.value == os.getpid() or not user.IsWindowVisible(hwnd) or user.IsIconic(hwnd):
            return True
        r = wt.RECT()
        if user.GetWindowRect(hwnd, ct.byref(r)) and r.right-r.left > 12 and r.bottom-r.top > 12:
            outer = QRect(r.left, r.top, r.right-r.left, r.bottom-r.top)
            children = []
            @child_callback_type
            def child_callback(child, parameter):
                child_rect = wt.RECT()
                if user.IsWindowVisible(child) and user.GetWindowRect(child, ct.byref(child_rect)):
                    inner = QRect(child_rect.left, child_rect.top, child_rect.right-child_rect.left,
                                  child_rect.bottom-child_rect.top).intersected(outer)
                    if inner.width() > 12 and inner.height() > 12:
                        children.append(inner)
                return True
            user.EnumChildWindows(hwnd, child_callback, 0)
            result.extend(to_logical(inner, mapping) for inner in sorted(children, key=lambda area: area.width()*area.height()))
            result.append(to_logical(outer, mapping))
        return True

    user.EnumWindows(callback, 0)
    return result


def set_startup(enabled):
    if not WINDOWS:
        return
    import winreg
    from pathlib import Path
    if getattr(sys, "frozen", False):
        command = f'"{sys.executable}" --tray'
    else:
        pythonw = Path(sys.executable).with_name("pythonw.exe")
        command = f'"{pythonw}" "{Path(__file__).resolve().parent.parent / "run.py"}" --tray'
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                         r"Software\Microsoft\Windows\CurrentVersion\Run") as key:
        if enabled:
            winreg.SetValueEx(key, "FloatShot", 0, winreg.REG_SZ, command)
        else:
            try:
                winreg.DeleteValue(key, "FloatShot")
            except FileNotFoundError:
                pass


def foreground_program():
    if not WINDOWS: return ""
    user.GetForegroundWindow.restype = wt.HWND
    pid = wt.DWORD()
    user.GetWindowThreadProcessId(user.GetForegroundWindow(), ct.byref(pid))
    kernel = ct.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel.OpenProcess.restype = wt.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ct.POINTER(wt.DWORD)]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, pid.value)
    if not handle: return ""
    try:
        text, size = ct.create_unicode_buffer(32768), wt.DWORD(32768)
        return text.value if kernel.QueryFullProcessImageNameW(handle, 0, text, ct.byref(size)) else ""
    finally: kernel.CloseHandle(handle)


def program_ignored(program, patterns):
    program = program.lower().replace("\\", "/")
    filename = program.rsplit("/", 1)[-1]
    return any(pattern and (fnmatch.fnmatch(program, pattern) or fnmatch.fnmatch(filename, pattern) or pattern in program)
               for pattern in (p.strip().lower().replace("\\", "/") for p in patterns.split(";")))


def foreground_rect():
    if not WINDOWS: return QRect()
    user.GetForegroundWindow.restype = wt.HWND
    rectangle = wt.RECT()
    if user.GetWindowRect(user.GetForegroundWindow(), ct.byref(rectangle)):
        return to_logical(QRect(rectangle.left, rectangle.top, rectangle.right-rectangle.left,
                                rectangle.bottom-rectangle.top))
    return QRect()


def move_cursor_pixels(delta):
    if WINDOWS:
        user.GetCursorPos.argtypes = [ct.POINTER(wt.POINT)]
        user.SetCursorPos.argtypes = [ct.c_int, ct.c_int]
        point = wt.POINT()
        if user.GetCursorPos(ct.byref(point)):
            user.SetCursorPos(point.x+delta.x(), point.y+delta.y())
    else:
        from PySide6.QtGui import QCursor
        QCursor.setPos(QCursor.pos()+delta)


_cursor_cache = {}


def composite_cursor(frame, region):
    """Draw the current native cursor into a writable BGRA numpy frame."""
    if not WINDOWS:
        return
    import numpy as np

    class CursorInfo(ct.Structure):
        _fields_ = [("cbSize", wt.DWORD), ("flags", wt.DWORD),
                    ("hCursor", wt.HANDLE), ("ptScreenPos", wt.POINT)]

    class IconInfo(ct.Structure):
        _fields_ = [("fIcon", wt.BOOL), ("xHotspot", wt.DWORD), ("yHotspot", wt.DWORD),
                    ("hbmMask", wt.HANDLE), ("hbmColor", wt.HANDLE)]

    class BitmapHeader(ct.Structure):
        _fields_ = [("size", wt.DWORD), ("width", wt.LONG), ("height", wt.LONG),
                    ("planes", wt.WORD), ("bits", wt.WORD), ("compression", wt.DWORD),
                    ("image_size", wt.DWORD), ("xppm", wt.LONG), ("yppm", wt.LONG),
                    ("used", wt.DWORD), ("important", wt.DWORD)]

    ci = CursorInfo()
    ci.cbSize = ct.sizeof(ci)
    user.GetCursorInfo.argtypes = [ct.POINTER(CursorInfo)]
    if not user.GetCursorInfo(ct.byref(ci)) or not ci.flags & 1:
        return
    key = int(ci.hCursor)
    if key not in _cursor_cache:
        gdi = ct.WinDLL("gdi32", use_last_error=True)
        gdi.CreateCompatibleDC.argtypes = [wt.HDC]
        gdi.CreateCompatibleDC.restype = wt.HDC
        gdi.CreateDIBSection.argtypes = [wt.HDC, ct.c_void_p, wt.UINT, ct.POINTER(ct.c_void_p), wt.HANDLE, wt.DWORD]
        gdi.CreateDIBSection.restype = wt.HANDLE
        gdi.SelectObject.argtypes = [wt.HDC, wt.HANDLE]
        gdi.SelectObject.restype = wt.HANDLE
        gdi.DeleteObject.argtypes = [wt.HANDLE]
        gdi.DeleteDC.argtypes = [wt.HDC]
        user.GetIconInfo.argtypes = [wt.HANDLE, ct.POINTER(IconInfo)]
        user.DrawIconEx.argtypes = [wt.HDC, ct.c_int, ct.c_int, wt.HANDLE, ct.c_int, ct.c_int,
                                   wt.UINT, wt.HANDLE, wt.UINT]
        icon = IconInfo()
        if not user.GetIconInfo(ci.hCursor, ct.byref(icon)):
            return
        width, height = max(32, user.GetSystemMetrics(13)), max(32, user.GetSystemMetrics(14))
        header = BitmapHeader(ct.sizeof(BitmapHeader), width, -height, 1, 32, 0, width*height*4, 0, 0, 0, 0)
        pixels = ct.c_void_p()
        dc = gdi.CreateCompatibleDC(None)
        bitmap = gdi.CreateDIBSection(dc, ct.byref(header), 0, ct.byref(pixels), None, 0)
        if not bitmap or not pixels.value:
            gdi.DeleteDC(dc)
            for handle in (icon.hbmMask, icon.hbmColor):
                if handle: gdi.DeleteObject(handle)
            return
        previous = gdi.SelectObject(dc, bitmap)
        ct.memset(pixels, 0, width*height*4)
        user.DrawIconEx(dc, 0, 0, ci.hCursor, width, height, 0, None, 3)
        rgba = np.frombuffer(ct.string_at(pixels, width*height*4), np.uint8).reshape(height, width, 4).copy()
        if not rgba[:, :, 3].any():
            rgba[:, :, 3] = np.where(rgba[:, :, :3].any(axis=2), 255, 0)
        _cursor_cache[key] = (rgba, int(icon.xHotspot), int(icon.yHotspot))
        gdi.SelectObject(dc, previous)
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(dc)
        for handle in (icon.hbmMask, icon.hbmColor):
            if handle: gdi.DeleteObject(handle)
        if len(_cursor_cache) > 32:
            _cursor_cache.clear()
            _cursor_cache[key] = (rgba, int(icon.xHotspot), int(icon.yHotspot))
    icon, hot_x, hot_y = _cursor_cache[key]
    x, y = ci.ptScreenPos.x-region["left"]-hot_x, ci.ptScreenPos.y-region["top"]-hot_y
    left, top = max(0, x), max(0, y)
    right, bottom = min(frame.shape[1], x+icon.shape[1]), min(frame.shape[0], y+icon.shape[0])
    if right <= left or bottom <= top:
        return
    source = icon[top-y:bottom-y, left-x:right-x].astype(np.uint16)
    dest = frame[top:bottom, left:right]
    alpha = source[:, :, 3:4]
    dest[:, :, :3] = ((source[:, :, :3]*alpha+dest[:, :, :3].astype(np.uint16)*(255-alpha))//255).astype(np.uint8)


class Hotkeys(QAbstractNativeEventFilter):
    def __init__(self, actions):
        super().__init__()
        self.actions = actions
        self.registered = {}
        self.ignored = {}
        self.names = {}

    def register(self, bindings):
        self.clear()
        failed = []
        if not WINDOWS:
            return ["此平台暂不支持全局快捷键"]
        mods_map = [(Qt.ControlModifier, 0x2), (Qt.AltModifier, 0x1),
                    (Qt.ShiftModifier, 0x4), (Qt.MetaModifier, 0x8)]
        for identifier, (name, text) in enumerate(bindings.items(), 1):
            sequence = QKeySequence(text)
            if sequence.isEmpty() or sequence.count() != 1:
                failed.append(f"{name}: {text or '未设置'}")
                continue
            combo = sequence[0]
            key = int(combo.key())
            if Qt.Key_F1 <= key <= Qt.Key_F24:
                key = 0x70 + key-int(Qt.Key_F1)
            elif not (0x30 <= key <= 0x5a):
                failed.append(f"{name}: {text}（仅支持字母、数字和 F1–F24）")
                continue
            modifiers = 0x4000
            for flag, value in mods_map:
                if combo.keyboardModifiers() & flag:
                    modifiers |= value
            if user.RegisterHotKey(None, identifier, modifiers, key):
                self.registered[identifier] = self.actions[name]
                self.names[identifier] = name
            else:
                failed.append(f"{text}（已被占用）")
        return failed

    def clear(self):
        if WINDOWS:
            for identifier in self.registered:
                user.UnregisterHotKey(None, identifier)
        self.registered.clear()
        self.names.clear()

    def nativeEventFilter(self, event_type, message):
        if WINDOWS:
            msg = wt.MSG.from_address(int(message))
            if msg.message == 0x0312 and int(msg.wParam) in self.registered:
                patterns = self.ignored.get(self.names[int(msg.wParam)], "")
                if not patterns or not program_ignored(foreground_program(), patterns): self.registered[int(msg.wParam)]()
                return True, 0
            if ((msg.message == 0x02B1 and int(msg.wParam) in (2, 3, 6, 7)) or
                    (msg.message == 0x0218 and int(msg.wParam) == 4)):
                callback = self.actions.get("interrupt")
                if callback:
                    callback()
        return False, 0
