"""Measure Windows snapshot-to-first-paint latency; never save desktop images."""
import json
import argparse
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QRect
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication
from floatshot import native
from floatshot.app import MainWindow
from floatshot.capture import Desktop, SelectionOverlay
from floatshot.storage import Store
from floatshot.theme import STYLE, load_fonts


class MeasuredOverlay(SelectionOverlay):
    first_paint_at = None

    def paintEvent(self, event):
        super().paintEvent(event)
        if self.first_paint_at is None:
            self.first_paint_at = time.perf_counter()


def verify_latency(app, root, show_home=True):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    report = []
    with tempfile.TemporaryDirectory(prefix="latency-", dir=root) as folder:
        window = MainWindow(Store(Path(folder)), testing=True)
        if show_home:
            window.show()
            QTest.qWait(150)
            window.hide()
            QTest.qWait(150)
        try:
            for _ in range(3):
                start = time.perf_counter()
                desktop = Desktop()
                snapshot = time.perf_counter()
                overlay = MeasuredOverlay(desktop)
                controls = time.perf_counter()
                overlay.show()
                for _ in range(100):
                    if overlay.first_paint_at: break
                    app.processEvents()
                assert overlay.first_paint_at is not None
                assert overlay._toolbar is None and overlay._text_entry is None
                first_frame = overlay.first_paint_at
                overlay.selection = QRect(120, 100, 480, 300)
                selection_start = time.perf_counter()
                overlay.ensure_annotation()
                overlay.show_toolbar()
                app.processEvents()
                report.append({
                    "snapshot_ms": round((snapshot-start)*1000, 1),
                    "controls_before_selection_ms": round((controls-snapshot)*1000, 1),
                    "first_paint_ms": round((first_frame-controls)*1000, 1),
                    "total_ms": round((first_frame-start)*1000, 1),
                    "toolbar_after_selection_ms": round((time.perf_counter()-selection_start)*1000, 1),
                })
                overlay.close()
                overlay.deleteLater()
                app.processEvents()
                QTest.qWait(500)
        finally:
            window.hotkeys.clear()
            app.removeNativeEventFilter(window.hotkeys)
            window.tray.hide()
            window.close()
            window.deleteLater()
            app.processEvents()
    (root/"latency.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tray", action="store_true", help="Measure without rendering the home window first")
    args = parser.parse_args()
    native.enable_dpi()
    app = QApplication([])
    app.setQuitOnLastWindowClosed(False)
    app.setStyle("Fusion")
    load_fonts()
    app.setStyleSheet(STYLE)
    report_root = ".test-artifacts/capture-v0.4.1-tray" if args.tray else ".test-artifacts/capture-v0.4.1"
    print(json.dumps(verify_latency(app, report_root, not args.tray), indent=2))
