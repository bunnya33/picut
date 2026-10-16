"""Shared image operations for screenshots and pins."""
import re

from PySide6.QtCore import QPoint, QRect, QSize, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen, QTextDocument
from PySide6.QtWidgets import QWidget


def parse_color(text):
    text = text.strip()
    named = QColor(text)
    if named.isValid():
        return named
    match = re.fullmatch(r"(?:rgb\s*\()?\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)\s*\)?", text, re.I)
    if match:
        values = match.groups()
        try:
            if any("." in value for value in values):
                numbers = [float(value) for value in values]
                if all(0 <= value <= 1 for value in numbers):
                    return QColor.fromRgbF(*numbers)
            else:
                numbers = [int(value) for value in values]
                if all(0 <= value <= 255 for value in numbers):
                    return QColor(*numbers)
        except ValueError:
            pass
    return QColor()


def color_card(color, title=""):
    image = QImage(300, 180, QImage.Format_ARGB32)
    image.fill(color)
    p = QPainter(image)
    p.setPen(Qt.black if color.lightness() > 130 else Qt.white)
    font = p.font()
    font.setPixelSize(18)
    p.setFont(font)
    p.drawText(image.rect().adjusted(16, 14, -16, -14), Qt.AlignCenter,
               title or f"{color.name().upper()}\n{color.red()}, {color.green()}, {color.blue()}")
    p.end()
    return image


def text_card(text, html=False):
    doc = QTextDocument()
    doc.setDefaultStyleSheet("body { color: #182d3a; font-family: 'Microsoft YaHei UI'; font-size: 16px; }")
    doc.setTextWidth(420)
    doc.setHtml(text) if html else doc.setPlainText(text)
    size = QSize(460, max(80, min(2000, round(doc.size().height())+40)))
    image = QImage(size, QImage.Format_ARGB32)
    image.fill(QColor("#fff7dc"))
    p = QPainter(image)
    p.translate(20, 20)
    doc.drawContents(p)
    p.end()
    return image


def format_color(color, rgb=False):
    return f"{color.red()}, {color.green()}, {color.blue()}" if rgb else color.name().upper()


def decorate_image(image):
    from PIL import Image, ImageFilter
    rgba = image.convertToFormat(QImage.Format_RGBA8888)
    source = Image.frombytes("RGBA", (rgba.width(), rgba.height()), bytes(rgba.constBits()), "raw", "RGBA", rgba.bytesPerLine())
    padding = 18
    shadow = Image.new("RGBA", (source.width+padding*2, source.height+padding*2), (0, 0, 0, 0))
    alpha = Image.new("L", shadow.size, 0)
    alpha.paste(source.getchannel("A").point(lambda v: v*90//255), (padding, padding+4))
    shadow.putalpha(alpha.filter(ImageFilter.GaussianBlur(7)))
    shadow.alpha_composite(source, (padding, padding))
    result = QImage(shadow.tobytes(), shadow.width, shadow.height, shadow.width*4, QImage.Format_RGBA8888).copy()
    painter = QPainter(result)
    painter.setPen(QPen(QColor("#7f919f"), 1))
    painter.drawRect(QRect(padding, padding, image.width()-1, image.height()-1))
    painter.end()
    return result


class PixelLoupe(QWidget):
    """Non-interactive loupe owned by a pin, never part of the exported image."""
    def __init__(self, parent):
        super().__init__(parent, Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint |
                         Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput)
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WA_ShowWithoutActivating)
        self.setFixedSize(180, 232)
        self.sample = QImage()
        self.color = QColor()
        self.position = QPoint()
        self.rgb = False

    def present(self, image, image_point, global_point, rgb=False):
        self.sample = image.copy(QRect(image_point-QPoint(5, 5), QSize(11, 11)))
        self.color = image.pixelColor(image_point)
        self.position = global_point
        self.rgb = rgb
        screen = self.screen().geometry()
        x = global_point.x()+24
        y = global_point.y()+24
        if x+self.width() > screen.right(): x = global_point.x()-self.width()-24
        if y+self.height() > screen.bottom(): y = global_point.y()-self.height()-24
        self.move(max(screen.left(), x), max(screen.top(), y))
        self.show()
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor("#14252d"))
        p.drawImage(QRect(12, 10, 156, 156), self.sample)
        p.setPen(QPen(QColor("#48d6be"), 1))
        p.drawLine(12, 88, 168, 88)
        p.drawLine(90, 10, 90, 166)
        p.setPen(Qt.black)
        p.drawRect(QRect(83, 81, 14, 14))
        p.setPen(Qt.white)
        p.drawText(QRect(10, 173, 160, 24), Qt.AlignCenter, format_color(self.color, self.rgb))
        p.drawText(QRect(10, 197, 160, 18), Qt.AlignCenter, f"({self.position.x()}, {self.position.y()})")
        font = p.font()
        font.setPixelSize(11)
        p.setFont(font)
        p.drawText(QRect(3, 216, 174, 16), Qt.AlignCenter, "C 复制颜色 · Shift 切换 RGB/HEX")
