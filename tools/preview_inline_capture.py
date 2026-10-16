"""Render the screenshot workflow using a synthetic desktop, without private content."""
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QPoint, QRect, QTimer, Qt
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QMessageBox

from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.theme import STYLE, load_fonts


app = QApplication([])
app.setStyle("Fusion")
load_fonts()
app.setStyleSheet(STYLE)
root = Path(".test-artifacts/inline-capture")
root.mkdir(parents=True, exist_ok=True)
desktop = Desktop.__new__(Desktop)
desktop.bounds = QRect(0, 0, 1280, 840)
desktop.windows = []
image = QImage(desktop.bounds.size(), QImage.Format_ARGB32)
image.fill(QColor("#273545"))
p = QPainter(image)
p.setPen(Qt.NoPen)
p.setBrush(QColor("#f5f7fa"))
p.drawRoundedRect(QRect(70, 55, 1100, 680), 12, 12)
p.fillRect(QRect(70, 55, 1100, 56), QColor("#dde5ee"))
p.setPen(QColor("#314b60"))
font = QFont("Microsoft YaHei UI")
font.setPixelSize(15)
p.setFont(font)
p.drawText(QRect(94, 65, 1000, 32), Qt.AlignVCenter, "FloatShot / Project files")
p.drawText(QRect(110, 144, 900, 35), Qt.AlignVCenter, "File name                              Modified               Type                   Size")
rows = [("_internal", "Folder", ""), ("docs", "Folder", ""),
        ("FloatShot.exe", "Application", "7.5 MB"), ("README.md", "Markdown", "8 KB"),
        ("THIRD_PARTY_NOTICES.md", "Markdown", "2 KB")]
for i, (name, kind, size) in enumerate(rows):
    y = 196+i*50
    if i == 2:
        p.fillRect(QRect(98, y, 1020, 42), QColor("#d7ebe9"))
    p.fillRect(QRect(115, y+13, 16, 16), QColor("#42a894" if i == 2 else "#e7b658"))
    p.setPen(QColor("#223b50"))
    p.drawText(QRect(143, y, 410, 42), Qt.AlignVCenter, name)
    p.drawText(QRect(553, y, 210, 42), Qt.AlignVCenter, "2026-10-06 14:30")
    p.drawText(QRect(783, y, 190, 42), Qt.AlignVCenter, kind)
    p.drawText(QRect(1000, y, 100, 42), Qt.AlignVCenter, size)
p.end()
desktop.screens = [ScreenImage(desktop.bounds, image)]
overlay = SelectionOverlay(desktop)
overlay.screen_rect = lambda point: desktop.bounds
overlay.show()
QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(95, 136))
QTest.mouseMove(overlay, QPoint(1135, 665))
QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(1135, 665))
overlay.choose_tool("arrow")
for start, end in [(QPoint(685, 465), QPoint(312, 318)), (QPoint(268, 565), QPoint(300, 391))]:
    QTest.mousePress(overlay, Qt.LeftButton, pos=start)
    QTest.mouseMove(overlay, end)
    QTest.mouseRelease(overlay, Qt.LeftButton, pos=end)
QTest.mouseMove(overlay, QPoint(764, 377))
app.processEvents()
overlay.grab().save(str(root / "inline-selection.png"))


def capture_prompt():
    prompt = app.activeModalWidget()
    assert isinstance(prompt, QMessageBox)
    result = overlay.grab().toImage()
    painter = QPainter(result)
    painter.drawPixmap(prompt.mapToGlobal(QPoint())-overlay.geometry().topLeft(), prompt.grab())
    painter.end()
    result.save(str(root / "exit-confirm.png"))
    prompt.button(QMessageBox.Cancel).click()


QTimer.singleShot(80, capture_prompt)
overlay.cancel()
assert overlay.isVisible()
overlay.close()
app.processEvents()
print(root)
