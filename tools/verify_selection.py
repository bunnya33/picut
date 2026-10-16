"""Verify selection feedback through the real Windows compositor."""
import argparse
import ctypes as ct
import json
import sys
from ctypes import wintypes as wt
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import mss
from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from floatshot import native
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.theme import STYLE, load_fonts


def input_desktop_name():
    user = native.user
    user.OpenInputDesktop.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    user.OpenInputDesktop.restype = wt.HANDLE
    user.CloseDesktop.argtypes = [wt.HANDLE]
    user.GetUserObjectInformationW.argtypes = [wt.HANDLE, ct.c_int, ct.c_void_p,
                                              wt.DWORD, ct.POINTER(wt.DWORD)]
    handle = user.OpenInputDesktop(0, False, 1)
    if not handle:
        return None
    try:
        name = ct.create_unicode_buffer(256)
        length = wt.DWORD()
        if user.GetUserObjectInformationW(handle, 2, name, ct.sizeof(name), ct.byref(length)):
            return name.value
        return None
    finally:
        user.CloseDesktop(handle)


def session_is_unlocked():
    class SessionInfo(ct.Structure):
        _fields_ = [("session_id", wt.DWORD), ("state", ct.c_int), ("flags", wt.LONG),
                    ("station", wt.WCHAR * 33), ("user", wt.WCHAR * 21), ("domain", wt.WCHAR * 18),
                    ("logon", ct.c_longlong), ("connect", ct.c_longlong),
                    ("disconnect", ct.c_longlong), ("last_input", ct.c_longlong),
                    ("now", ct.c_longlong), ("incoming_bytes", wt.DWORD),
                    ("outgoing_bytes", wt.DWORD), ("incoming_frames", wt.DWORD),
                    ("outgoing_frames", wt.DWORD), ("incoming_compressed", wt.DWORD),
                    ("outgoing_compressed", wt.DWORD)]

    class SessionInfoEx(ct.Structure):
        _fields_ = [("level", wt.DWORD), ("data", SessionInfo)]

    wts = ct.WinDLL("wtsapi32", use_last_error=True)
    wts.WTSQuerySessionInformationW.argtypes = [wt.HANDLE, wt.DWORD, ct.c_int,
                                              ct.POINTER(ct.c_void_p), ct.POINTER(wt.DWORD)]
    wts.WTSFreeMemory.argtypes = [ct.c_void_p]
    data = ct.c_void_p()
    length = wt.DWORD()
    if not wts.WTSQuerySessionInformationW(None, 0xFFFFFFFF, 25, ct.byref(data), ct.byref(length)):
        raise ct.WinError(ct.get_last_error())
    try:
        if length.value < ct.sizeof(SessionInfoEx):
            raise RuntimeError("Windows returned incomplete session information")
        info = ct.cast(data, ct.POINTER(SessionInfoEx)).contents
        return info.level == 1 and info.data.flags == 1
    finally:
        wts.WTSFreeMemory(data)


def verify_selection(app, root, compare_exclusion=False, render_only=False):
    if not native.WINDOWS:
        raise RuntimeError("Selection compositor verification requires Windows")
    if not render_only and (input_desktop_name() != "Default" or not session_is_unlocked()):
        raise RuntimeError("Windows is locked or its input desktop is inaccessible; unlock and rerun")
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect()
    desktop.screens = []
    desktop.windows = []
    for screen in QGuiApplication.screens():
        rect = screen.geometry()
        image = QImage(rect.size(), QImage.Format_ARGB32)
        image.fill(QColor("#f4f1ec"))
        desktop.screens.append(ScreenImage(rect, image))
        desktop.bounds = desktop.bounds.united(rect)
    primary = QGuiApplication.primaryScreen().geometry()
    rect = QRect(primary.topLeft() + QPoint(160, 160), primary.size() / 3)
    selected_color = QColor("#f4f1ec")
    get_affinity = native.user.GetWindowDisplayAffinity
    get_affinity.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
    get_affinity.restype = wt.BOOL

    def affinity(overlay):
        value = wt.DWORD()
        if not get_affinity(int(overlay.winId()), ct.byref(value)):
            raise ct.WinError(ct.get_last_error())
        return value.value

    def screen_pixel(point):
        region = native.to_physical(QRect(point, QPoint(point.x()+1, point.y()+1)))
        with mss.MSS() as capture:
            return QColor(*capture.grab(region).pixel(0, 0))

    def inspect(overlay):
        if render_only:
            rendered = overlay.grab().toImage()

            def pixel(point):
                local = (point - desktop.bounds.topLeft()) * rendered.devicePixelRatio()
                return rendered.pixelColor(local)
        else:
            pixel = screen_pixel
        selected = pixel(rect.center())
        border = pixel(QPoint(rect.left(), rect.center().y()))
        outside = pixel(QPoint(rect.left()-25, rect.center().y()))
        return {
            "affinity": affinity(overlay),
            "geometry": list(overlay.geometry().getRect()),
            "is_visible": overlay.isVisible(),
            "selection": list(overlay.selection.getRect()),
            "selected": selected.name(),
            "border": border.name(),
            "outside": outside.name(),
            "visible": (selected == selected_color and border == QColor("#48d6be" if overlay.recording else "#1688ef")
                        and outside.lightness() < selected.lightness() - 60),
        }

    report = {"native_compositor_checked": not render_only}
    for recording in (False, True):
        overlay = SelectionOverlay(desktop, recording=recording)
        try:
            overlay.show()
            if compare_exclusion:
                native.exclude_from_capture(overlay)
            QTest.qWait(250)
            start = rect.topLeft() - desktop.bounds.topLeft()
            end = rect.bottomRight() - desktop.bounds.topLeft()
            QTest.mouseMove(overlay, start)
            QTest.qWait(50)
            QTest.mousePress(overlay, Qt.LeftButton, pos=start)
            QTest.mouseMove(overlay, end)
            QTest.qWait(100)
            during = inspect(overlay)
            assert overlay.selection == rect.translated(-desktop.bounds.topLeft()), during
            QTest.mouseRelease(overlay, Qt.LeftButton, pos=end)
            QTest.qWait(100)
            released = inspect(overlay)
            assert overlay.selection == rect.translated(-desktop.bounds.topLeft()), released
            mode = "recording" if recording else "screenshot"
            report[mode] = {"during_drag": during, "released": released}
            if compare_exclusion:
                native.user.SetWindowDisplayAffinity(int(overlay.winId()), 0)
                overlay.repaint()
                QTest.qWait(250)
                report[mode]["exclusion_cleared"] = inspect(overlay)
                overlay.grab().save(str(root / f"{mode}-widget.png"))
            else:
                assert during["affinity"] == released["affinity"] == 0, report
                assert during["visible"] and released["visible"], report
                assert overlay.toolbar.isVisible(), report
                if render_only:
                    image = overlay.grab().toImage()
                else:
                    with mss.MSS() as capture:
                        frame = capture.grab(native.to_physical(primary))
                        image = QImage(frame.bgra, frame.width, frame.height,
                                       QImage.Format_RGB32).copy()
                image.save(str(root / f"{mode}.png"))
            # The export must contain only the frozen desktop, never the border/mask.
            exported = desktop.crop(rect)
            assert exported.pixelColor(0, 0) == selected_color
            assert exported.pixelColor(exported.width()-1, exported.height()-1) == selected_color
        finally:
            overlay.close()
            overlay.deleteLater()
            app.processEvents()
    (root / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compare-exclusion", action="store_true")
    parser.add_argument("--render-only", action="store_true",
                        help="Check Qt painting and native affinity without testing the desktop compositor")
    args = parser.parse_args()
    if args.render_only and args.compare_exclusion:
        parser.error("Exclusion comparison requires compositor verification")
    native.enable_dpi()
    app = QApplication([])
    app.setStyle("Fusion")
    load_fonts()
    app.setStyleSheet(STYLE)
    print(json.dumps(verify_selection(app, ".test-artifacts/selection",
                                     args.compare_exclusion, args.render_only), indent=2))
