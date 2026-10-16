from PySide6.QtCore import Qt, QRectF
from PySide6.QtGui import QColor, QFont, QFontDatabase, QIcon, QPainter, QPen, QPixmap

TEAL = "#48d6be"
STYLE = """
QWidget { color: #e6edf4; font-family: 'Microsoft YaHei', 'Segoe UI'; font-size: 13px; }
QMainWindow, QDialog { background: #101820; }
QFrame#sidebar { background: #0c131a; border-right: 1px solid #25313e; }
QFrame#card, QGroupBox { background: #18232f; border: 1px solid #2a3948; border-radius: 12px; }
QGroupBox { margin-top: 16px; padding: 18px 12px 12px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
QLabel#title { font-size: 28px; font-weight: 700; }
QLabel#subtitle, QLabel#muted { color: #92a4b6; }
QLabel#brand { font-size: 22px; font-weight: 700; color: #48d6be; }
QLabel#badge { color: #48d6be; background: #18382f; border-radius: 5px; padding: 4px 8px; }
QPushButton, QToolButton { background: #223140; border: 1px solid #32475a;
 border-radius: 7px; padding: 9px 14px; color: #e6edf4; }
QPushButton:hover, QToolButton:hover { background: #2d4354; border-color: #48d6be; }
QPushButton:pressed, QToolButton:checked { background: #174b43; border-color: #48d6be; }
QPushButton#primary { background: #48d6be; color: #0c2722; border: none; font-weight: 700; }
QPushButton#primary:hover { background: #78e7d4; }
QPushButton#danger { background: #5c2a35; border-color: #944355; }
QPushButton:disabled, QToolButton:disabled { color: #647586; background: #192530; border-color: #263440; }
QPushButton#nav { text-align: left; background: transparent; border: none; padding: 12px 18px; }
QPushButton#nav:checked { background: #193930; color: #66e6cf; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit { background: #0f1b25; border: 1px solid #334658;
 border-radius: 6px; padding: 7px; selection-background-color: #217566; }
QComboBox QAbstractItemView { background: #192a38; color: #e6edf4; selection-background-color: #266859; }
QCheckBox { spacing: 8px; padding: 5px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #557081; border-radius: 4px; background: #14212b; }
QCheckBox::indicator:checked { background: #48d6be; border: 2px solid #277e70; }
QListWidget { background: #121e28; border: 1px solid #2a3948; border-radius: 10px; padding: 6px; outline: none; }
QListWidget::item { padding: 8px; border-radius: 6px; }
QListWidget::item:selected { background: #244f46; }
QListWidget::item:hover { background: #223444; }
QScrollArea { border: none; background: transparent; }
QWidget#scrollContent, QWidget#toolbarOptions { background: #14252d; }
QTableWidget { background: #0f1b25; alternate-background-color: #192a38; gridline-color: #334658; }
QHeaderView::section { background: #223140; color: #e6edf4; padding: 6px; border: 1px solid #334658; }
QScrollBar:vertical { background: #12202b; width: 10px; }
QScrollBar::handle:vertical { background: #3a5264; border-radius: 5px; min-height: 20px; }
QMenu { background: #172430; border: 1px solid #3a4e60; padding: 5px; }
QMenu::item { padding: 8px 24px; }
QMenu::item:selected { background: #24564c; }
QToolTip { background: #243442; border: 1px solid #557081; color: white; padding: 6px; }
QStatusBar { color: #91a6b8; background: #0e171f; }
QTabWidget::pane { border: 1px solid #2a3948; }
QTabBar::tab { background: #18232f; padding: 10px 18px; }
QTabBar::tab:selected { background: #24564c; color: #70ecd6; }
QSlider::groove:horizontal { height: 4px; background: #314454; }
QSlider::handle:horizontal { width: 14px; margin: -5px 0; border-radius: 7px; background: #48d6be; }
"""


def app_icon(size=64):
    pixmap = QPixmap(size, size)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.Antialiasing)
    painter.setBrush(QColor("#163a33"))
    painter.setPen(Qt.NoPen)
    painter.drawRoundedRect(QRectF(0, 0, size, size), size / 4, size / 4)
    painter.setPen(QPen(QColor(TEAL), size / 17, Qt.SolidLine, Qt.RoundCap))
    x, y = size * .25, size * .23
    length = size * .17
    for a, b, dx, dy in ((x, y, 1, 1), (size-x, y, -1, 1),
                          (x, size-y, 1, -1), (size-x, size-y, -1, -1)):
        painter.drawLine(int(a), int(b), int(a+dx*length), int(b))
        painter.drawLine(int(a), int(b), int(a), int(b+dy*length))
    painter.setPen(Qt.NoPen)
    painter.setBrush(QColor(TEAL))
    painter.drawEllipse(QRectF(size*.43, size*.43, size*.14, size*.14))
    painter.end()
    return QIcon(pixmap)


def load_fonts():
    # The offscreen Qt platform doesn't enumerate Windows fonts automatically.
    import os
    from pathlib import Path
    fonts = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"
    for name in ("msyh.ttc", "msyhbd.ttc", "segoeui.ttf"):
        path = fonts / name
        if path.is_file():
            QFontDatabase.addApplicationFont(str(path))


def label(text, name=None):
    from PySide6.QtWidgets import QLabel
    widget = QLabel(text)
    if name:
        widget.setObjectName(name)
    widget.setWordWrap(True)
    return widget


def button(text, callback=None, primary=False):
    from PySide6.QtWidgets import QPushButton
    widget = QPushButton(text)
    if primary:
        widget.setObjectName("primary")
    if callback:
        widget.clicked.connect(lambda checked=False: callback())
    return widget
