"""Check capture-to-pin placement on real monitor coordinates using synthetic pixels."""
import ctypes
import json
import sys
import tempfile
import mss
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QPoint, QPointF, QRect, QSize, QTimer, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QMessageBox, QWidget
from floatshot import native, __version__
from floatshot.app import MainWindow
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.storage import Store
from floatshot.theme import STYLE, load_fonts
from verify_selection import input_desktop_name, session_is_unlocked


def verify_position(app, root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    desktop = Desktop.__new__(Desktop)
    desktop.bounds, desktop.screens, desktop.windows = QRect(), [], []
    report = []
    compositor_available = input_desktop_name() == "Default" and session_is_unlocked()
    for screen in QGuiApplication.screens():
        area = screen.geometry()
        ratio = screen.devicePixelRatio()
        image = QImage(round(area.width()*ratio), round(area.height()*ratio), QImage.Format_ARGB32)
        image.fill(QColor("#e7eef4"))
        desktop.screens.append(ScreenImage(area, image))
        desktop.bounds = desktop.bounds.united(area)
    with tempfile.TemporaryDirectory(prefix="pin-position-", dir=root) as folder:
        window = MainWindow(Store(Path(folder)), testing=True)
        window.notify = lambda message: None
        backing = QWidget(None, Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        backing.setStyleSheet("background: white; border: none;")
        try:
            for screen in QGuiApplication.screens():
                area = screen.geometry()
                selected = QRect(area.topLeft()+QPoint(100, 100), QSize(240, 180))
                if compositor_available:
                    backing.setGeometry(selected.adjusted(-30, -30, 30, 30))
                    backing.show()
                selector = SelectionOverlay(desktop, store=window.store, output_handler=window.capture_output)
                selector.selection = selected.translated(-desktop.bounds.topLeft())
                window.overlay = selector
                selector.completed.connect(window.selection_done)
                selector.show()
                selector.ensure_annotation()
                selector.choose_tool("line")
                selector.annotation.begin_stroke(QPointF(20, 50))
                selector.annotation.end_stroke(QPointF(90, 50))
                dialog_centers = []
                def inspect_prompt():
                    prompt = QApplication.activeModalWidget()
                    dialog_centers.append(prompt.frameGeometry().center())
                    prompt.done(QMessageBox.Cancel)
                QTimer.singleShot(50, inspect_prompt)
                assert not selector.confirm_discard()
                assert (dialog_centers[0]-selected.center()).manhattanLength() <= 1
                QTest.qWait(50)
                window.pin_clipboard()
                pin = window.pins.windows[0]
                pin.raise_()
                QTest.qWait(50)
                assert pin.content_geometry() == selected
                rectangle = native.wt.RECT()
                assert native.user.GetWindowRect(native.wt.HWND(int(pin.winId())), ctypes.byref(rectangle))
                physical = native.to_physical(pin.geometry())
                assert abs(rectangle.left-physical["left"]) <= 1
                assert abs(rectangle.top-physical["top"]) <= 1
                assert abs(rectangle.right-rectangle.left-physical["width"]) <= 1
                assert abs(rectangle.bottom-rectangle.top-physical["height"]) <= 1
                if compositor_available:
                    for logical_point in (selected.topLeft()+QPoint(5, 5), selected.center(), selected.bottomRight()-QPoint(5, 5)):
                        sample_rect = native.to_physical(QRect(logical_point, QSize(2, 2)))
                        with mss.MSS() as capture:
                            pixel = QColor(*capture.grab(sample_rect).pixel(0, 0))
                        assert pixel == QColor("#e7eef4"), (screen.name(), logical_point, pixel.name())
                    for distance, expected in ((1, (170, 207, 249)), (2, (194, 220, 251)), (10, (253, 254, 255))):
                        logical_point = QPoint(selected.center().x(), selected.top()-distance)
                        sample_rect = native.to_physical(QRect(logical_point, QSize(2, 2)))
                        with mss.MSS() as capture:
                            rgb = capture.grab(sample_rect).pixel(0, 0)
                        assert max(abs(a-b) for a, b in zip(rgb, expected)) <= 2, (screen.name(), distance, rgb)
                report.append({"monitor": screen.name(), "dpi_ratio": screen.devicePixelRatio(),
                               "selection": list(selected.getRect()), "pin_window": list(pin.geometry().getRect()),
                               "image_origin": [pin.content_geometry().x(), pin.content_geometry().y()],
                               "native_window_rect": [rectangle.left, rectangle.top, rectangle.right, rectangle.bottom],
                               "position_and_size_preserved": True, "native_geometry_checked": True,
                               "native_border_and_shadow_checked": compositor_available,
                               "exit_dialog_centered_on_selection": True,
                               "native_compositor_checked": compositor_available})
                window.pins.handle(pin, "destroy")
                backing.hide()
                app.processEvents()
        finally:
            backing.hide()
            backing.deleteLater()
            if window.overlay:
                window.overlay.hide()
                window.overlay.deleteLater()
                window.overlay = None
            for pin in list(window.pins.windows): window.pins.handle(pin, "destroy")
            window.pins.timer.stop()
            window.hotkeys.clear()
            app.removeNativeEventFilter(window.hotkeys)
            window.tray.hide()
            window.close()
            window.deleteLater()
            app.processEvents()
    (root/"report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    native.enable_dpi()
    app = QApplication([])
    app.setQuitOnLastWindowClosed(False)
    app.setStyle("Fusion")
    load_fonts()
    app.setStyleSheet(STYLE)
    print(json.dumps(verify_position(app, f".test-artifacts/pin-position-v{__version__}"), indent=2))
