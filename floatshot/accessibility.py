"""Collect UI Automation rectangles without delaying the screenshot overlay."""
import time
import threading
from PySide6.QtCore import QObject, QRect, Signal
from . import native


def automation_client():
    import comtypes.client
    try:
        from comtypes.gen import UIAutomationClient as uia
    except ImportError:
        uia = comtypes.client.GetModule("UIAutomationCore.dll")
    client = comtypes.client.CreateObject(uia.CUIAutomation, interface=uia.IUIAutomation)
    try:
        newer = client.QueryInterface(uia.IUIAutomation2)
        newer.ConnectionTimeout = 250
        newer.TransactionTimeout = 250
    except Exception: pass
    return client, uia


def element_rectangles(handle=None):
    if not native.WINDOWS: return []
    try:
        client, _ = automation_client()
        handle = handle or native.user.GetForegroundWindow()
        if not handle: return []
        root = client.ElementFromHandle(handle)
        walker = client.ControlViewWalker
        pending, result, count = [root], [], 0
        deadline = time.monotonic()+.65
        mapping = native.physical_monitors()
        while pending and count < 400 and time.monotonic() < deadline:
            node = pending.pop()
            count += 1
            if not node: continue
            rectangle = node.CurrentBoundingRectangle
            if rectangle.right-rectangle.left > 6 and rectangle.bottom-rectangle.top > 6 and not node.CurrentIsOffscreen:
                result.append(native.to_logical(QRect(rectangle.left, rectangle.top, rectangle.right-rectangle.left,
                                                      rectangle.bottom-rectangle.top), mapping))
            child = walker.GetFirstChildElement(node)
            children = []
            while child and len(children) < 100 and time.monotonic() < deadline:
                children.append(child)
                child = walker.GetNextSiblingElement(child)
            pending.extend(reversed(children))
        return sorted(set((r.x(), r.y(), r.width(), r.height()) for r in result), key=lambda r: r[2]*r[3])
    except Exception:
        # Some providers (secure/elevated/custom windows) cannot be queried.
        return []


class ElementDetector(QObject):
    ready = Signal(int, object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.sequence = 0
        self.busy = threading.Lock()

    def request(self, handle):
        self.sequence += 1
        token = self.sequence
        if not handle or not self.busy.acquire(blocking=False): return token
        def collect():
            import ctypes
            ole = ctypes.OleDLL("ole32")
            initialized = False
            try:
                ole.CoInitializeEx(None, 2)
                initialized = True
                result = element_rectangles(handle)
                self.ready.emit(token, result)
            except (OSError, RuntimeError):
                pass
            finally:
                if initialized: ole.CoUninitialize()
                self.busy.release()
        threading.Thread(target=collect, name="FloatShot-elements", daemon=True).start()
        return token


_detector = None


def element_detector():
    global _detector
    if _detector is None:
        from PySide6.QtWidgets import QApplication
        _detector = ElementDetector(QApplication.instance())
    return _detector
