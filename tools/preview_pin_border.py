"""Render the actual pin widget on a synthetic white desktop for visual review."""
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtWidgets import QApplication
from floatshot import __version__
from floatshot.pins import PinWindow
from floatshot.theme import load_fonts


def main():
    app = QApplication.instance() or QApplication([])
    load_fonts()
    image = QImage(440, 220, QImage.Format_ARGB32)
    image.fill(QColor("#ffffff"))
    painter = QPainter(image)
    painter.fillRect(QRect(136, 0, 304, 220), QColor("#f3f5f7"))
    painter.fillRect(QRect(136, 0, 3, 220), QColor("#51ab57"))
    painter.setPen(QColor("#28343d"))
    painter.setFont(QFont("Microsoft YaHei", 13))
    painter.drawText(QRect(159, 28, 260, 52), Qt.AlignLeft, "F3 贴图\n保持截图原位置")
    painter.setFont(QFont("Microsoft YaHei", 10))
    painter.drawText(QRect(159, 108, 260, 80), Qt.AlignLeft, "1 像素浅蓝边框\n外侧柔和蓝色阴影")
    painter.end()
    pin = PinWindow(image, source_rect=QRect(100, 100, 440, 220))
    pin.show()
    app.processEvents()
    rendered = pin.grab().toImage()
    # Composite the window's transparent outer margin onto a plain desktop.
    preview = QImage(rendered.size(), QImage.Format_ARGB32)
    preview.fill(Qt.white)
    painter = QPainter(preview)
    painter.drawImage(QPoint(), rendered)
    painter.end()
    root = Path(__file__).resolve().parent.parent/"docs/screenshots"
    root.mkdir(parents=True, exist_ok=True)
    preview.save(str(root/f"pin-border-v{__version__}.png"))
    detail = preview.copy(QRect(0, 0, 176, 112))
    detail.scaled(detail.width()*3, detail.height()*3, Qt.IgnoreAspectRatio,
                  Qt.FastTransformation).save(str(root/f"pin-border-detail-v{__version__}.png"))
    pin.hide()
    pin.deleteLater()
    app.processEvents()
    print(root/f"pin-border-v{__version__}.png")


if __name__ == "__main__":
    main()
