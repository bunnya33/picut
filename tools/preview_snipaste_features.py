"""Render the new controls against synthetic content for layout review."""
import os
import sys
import tempfile
from pathlib import Path
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtWidgets import QApplication
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot import __version__
from floatshot.dialogs import SettingsDialog
from floatshot.editor import Mark
from floatshot.pins import PinManager
from floatshot.storage import Store
from floatshot.theme import STYLE, load_fonts

app = QApplication([])
app.setStyle("Fusion")
load_fonts()
app.setStyleSheet(STYLE)
destination = Path("docs/screenshots")
destination.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="floatshot-preview-") as folder:
    store = Store(Path(folder))
    image = QImage(1280, 840, QImage.Format_ARGB32)
    image.fill(QColor("#e9edf3"))
    painter = QPainter(image)
    font = QFont("Microsoft YaHei UI")
    font.setPixelSize(28)
    painter.setFont(font)
    painter.setPen(QColor("#34465a"))
    painter.drawText(QRect(170, 160, 900, 120), Qt.AlignVCenter, "浮光 FloatShot · 就地截图与标注")
    font.setPixelSize(18)
    painter.setFont(font)
    painter.drawText(QRect(170, 320, 900, 120), Qt.AlignVCenter, "序号、虚线、圆角、双向箭头、文字描边与背景")
    for i in range(6): painter.fillRect(QRect(170+i*140, 510, 105, 75), QColor.fromHsv(i*50, 130, 220))
    painter.end()
    desktop = Desktop.__new__(Desktop)
    desktop.bounds, desktop.windows = image.rect(), []
    desktop.screens = [ScreenImage(image.rect(), image)]
    overlay = SelectionOverlay(desktop, store=store)
    overlay.screen_rect = lambda _: image.rect()
    overlay.selection = QRect(120, 130, 1030, 500)
    overlay.show()
    overlay.choose_tool("rect")
    overlay.annotation.document.add(Mark("rect", [QPointF(25, 25), QPointF(940, 165)], radius=25, dash="dash"))
    overlay.annotation.document.add(Mark("number", [QPointF(70, 110)], number=1))
    overlay.annotation.document.add(Mark("number", [QPointF(70, 275)], number=2))
    overlay.annotation.document.add(Mark("arrow", [QPointF(260, 210), QPointF(780, 210)], arrow_mode="both"))
    overlay.annotation.refresh()
    overlay.pointer = QPoint(750, 500)
    app.processEvents()
    overlay.grab().save(str(destination/f"snipaste-selection-v{__version__}.png"))
    overlay.annotation.selected_index = 0
    overlay.grab().save(str(destination/f"shape-handles-v{__version__}.png"))
    overlay.choose_tool("arrow")
    overlay.annotation.selected_index = 3
    overlay.pointer = QPoint(1050, 450)
    overlay.grab().save(str(destination/f"line-endpoints-v{__version__}.png"))
    overlay.choose_tool("pen")
    overlay.toolbar.pen_width.setValue(20)
    overlay.pointer = QPoint(850, 400)
    overlay.grab().save(str(destination/f"brush-preview-v{__version__}.png"))
    overlay.choose_tool("text")
    overlay.toolbar.bold.click()
    overlay.toolbar.italic.click()
    overlay.grab().save(str(destination/f"text-toolbar-v{__version__}.png"))
    overlay.choose_tool("mosaic")
    overlay.grab().save(str(destination/f"pixel-toolbar-v{__version__}.png"))
    overlay.choose_tool("select")
    overlay.pointer = QPoint(620, 380)
    overlay.alt_magnifier = True
    overlay.grab().save(str(destination/f"magnifier-v{__version__}.png"))
    overlay.grab().toImage().copy(overlay.magnifier_rect()).save(str(destination/f"magnifier-detail-v{__version__}.png"))
    overlay.hide()
    overlay.deleteLater()
    settings = SettingsDialog(store)
    settings.resize(790, 640)
    settings.show()
    app.processEvents()
    settings.grab().save(str(destination/f"settings-v{__version__}.png"))
    settings.findChildren(__import__('PySide6.QtWidgets', fromlist=['QTabWidget']).QTabWidget)[0].setCurrentIndex(2)
    app.processEvents()
    settings.grab().save(str(destination/f"commands-v{__version__}.png"))
    settings.close()
    manager = PinManager(store, lambda *args: None)
    pin = manager.create(image.copy(QRect(120, 130, 850, 450)))
    pin.move(130, 180)
    pin.start_editing()
    pin.choose_tool("text")
    pin.annotation.add_text(QPointF(45, 340), "Space 标注 · Alt 取色 · 滚轮缩放")
    app.processEvents()
    preview = QImage(1280, 840, QImage.Format_ARGB32)
    preview.fill(QColor("#243341"))
    painter = QPainter(preview)
    painter.drawPixmap(pin.pos(), pin.grab())
    painter.drawPixmap(pin.toolbar.pos(), pin.toolbar.grab())
    painter.end()
    preview.save(str(destination/f"pin-inline-v{__version__}.png"))
    manager.handle(pin, "destroy")
    app.processEvents()
print(destination)
