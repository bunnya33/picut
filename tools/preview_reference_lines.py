"""Render the four reference styles and actual toolbar against synthetic content."""
import os
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtWidgets import QApplication
from floatshot import __version__
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.editor import Mark
from floatshot.storage import Store
from floatshot.theme import STYLE, load_fonts


app = QApplication([])
app.setStyle("Fusion")
load_fonts()
app.setStyleSheet(STYLE)
with tempfile.TemporaryDirectory(prefix="reference-lines-") as folder:
    store = Store(Path(folder))
    store.config["magnifier_enabled"] = False
    desktop = Desktop.__new__(Desktop)
    image = QImage(900, 850, QImage.Format_ARGB32)
    image.fill(QColor("#edf0f2"))
    desktop.bounds, desktop.windows = image.rect(), []
    desktop.screens = [ScreenImage(image.rect(), image)]
    overlay = SelectionOverlay(desktop, store=store)
    overlay.selection = QRect(100, 65, 498, 442)
    overlay.show()
    overlay.choose_tool("line")
    session = overlay.annotation
    session.document.original.fill(Qt.white)
    for y, head in ((65, "tapered"), (175, "filled"), (285, "hollow"), (395, "")):
        session.document.add(Mark("arrow" if head else "line", [QPointF(55, y), QPointF(290, y)],
                                  width=9, arrow_head=head or "filled"))
    session.set_parameter("pen_width", 9)
    session.selected_index = 3
    session.refresh()
    overlay.toolbar.sync_session(session)
    overlay.pointer = QPoint(730, 130)
    overlay.show_toolbar()
    app.processEvents()
    crop = QRect(88, 18, 522, 660)
    root = Path(__file__).resolve().parent.parent/"docs/screenshots"
    root.mkdir(parents=True, exist_ok=True)
    overlay.grab().toImage().copy(crop).save(str(root/f"reference-lines-v{__version__}.png"))
    overlay.toolbar.line_type.showPopup()
    app.processEvents()
    # Include the real popup, which is a separate Qt window.
    from PySide6.QtGui import QPainter
    preview = overlay.grab().toImage()
    painter = QPainter(preview)
    popup = overlay.toolbar.line_type.view().window()
    painter.drawPixmap(popup.pos()-overlay.pos(), popup.grab())
    painter.end()
    preview.copy(crop).save(str(root/f"reference-lines-dropdown-v{__version__}.png"))
    overlay.toolbar.line_type.hidePopup()
    overlay.hide()
    overlay.deleteLater()
    app.processEvents()
print(root/f"reference-lines-dropdown-v{__version__}.png")
