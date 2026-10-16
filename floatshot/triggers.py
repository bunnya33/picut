"""Optional desktop corners and Win+drag screenshot gesture."""
import ctypes as ct
import time
from ctypes import wintypes as wt
from PySide6.QtCore import QObject, QPoint, QRect, QTimer, Signal
from PySide6.QtGui import QCursor, QGuiApplication
from . import native


class DesktopCorners(QObject):
    triggered = Signal(str)

    def __init__(self, parent, config, allowed):
        super().__init__(parent)
        self.config, self.allowed = config, allowed
        self.current, self.since, self.fired = None, 0, False
        self.timer = QTimer(self)
        self.timer.setInterval(120)
        self.timer.timeout.connect(self.poll)
        self.timer.start()

    def poll(self):
        point = QCursor.pos()
        screen = QGuiApplication.screenAt(point)
        hit = None
        if screen and self.allowed():
            r = screen.geometry()
            for name, corner in (("tl", r.topLeft()), ("tr", r.topRight()), ("bl", r.bottomLeft()), ("br", r.bottomRight())):
                if (point-corner).manhattanLength() <= 2 and self.config.get("corners", {}).get(name):
                    hit = (screen.name(), name)
        if hit != self.current:
            self.current, self.since, self.fired = hit, time.monotonic(), False
        if hit and not self.fired and time.monotonic()-self.since >= .65:
            self.fired = True
            self.triggered.emit(self.config["corners"][hit[1]])


class SuperSnip(QObject):
    selected = Signal(QRect)

    def __init__(self, parent):
        super().__init__(parent)
        self.hook, self.callback, self.origin = None, None, None
        self.allowed = lambda: True

    def enable(self, enabled):
        self.close()
        if not enabled or not native.WINDOWS: return True
        user = native.user
        callback_type = ct.WINFUNCTYPE(ct.c_ssize_t, ct.c_int, wt.WPARAM, wt.LPARAM)
        class MouseInfo(ct.Structure):
            _fields_ = [("point", wt.POINT), ("mouse_data", wt.DWORD), ("flags", wt.DWORD),
                        ("time", wt.DWORD), ("extra", ct.c_size_t)]
        user.SetWindowsHookExW.argtypes = [ct.c_int, callback_type, wt.HINSTANCE, wt.DWORD]
        user.SetWindowsHookExW.restype = wt.HANDLE
        user.CallNextHookEx.argtypes = [wt.HANDLE, ct.c_int, wt.WPARAM, wt.LPARAM]
        user.CallNextHookEx.restype = ct.c_ssize_t
        user.GetAsyncKeyState.argtypes = [ct.c_int]
        user.GetAsyncKeyState.restype = ct.c_short
        @callback_type
        def callback(code, message, parameter):
            if code >= 0:
                info = ct.cast(parameter, ct.POINTER(MouseInfo)).contents
                point = QPoint(info.point.x, info.point.y)
                if message == 0x0201 and self.allowed() and (user.GetAsyncKeyState(0x5b) & 0x8000 or user.GetAsyncKeyState(0x5c) & 0x8000):
                    self.origin = point
                    return 1
                if self.origin is not None:
                    if message == 0x0202:
                        rect = QRect(self.origin, point).normalized()
                        self.origin = None
                        if rect.width() > 3 and rect.height() > 3:
                            self.selected.emit(native.to_logical(rect))
                        return 1
                    if message == 0x0200: return 1
            return user.CallNextHookEx(self.hook, code, message, parameter)
        self.callback = callback
        kernel = ct.WinDLL("kernel32")
        kernel.GetModuleHandleW.argtypes = [wt.LPCWSTR]
        kernel.GetModuleHandleW.restype = wt.HMODULE
        self.hook = user.SetWindowsHookExW(14, callback, kernel.GetModuleHandleW(None), 0)
        return bool(self.hook)

    def close(self):
        if self.hook:
            native.user.UnhookWindowsHookEx.argtypes = [wt.HANDLE]
            native.user.UnhookWindowsHookEx(self.hook)
        self.hook, self.callback, self.origin = None, None, None
