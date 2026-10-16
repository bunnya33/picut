"""Controls embedded in the desktop screenshot overlay."""
import json
from PySide6.QtCore import QMimeData, QPoint, QPointF, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QColor, QCursor, QDrag, QFont, QFontDatabase, QIcon, QPainter, QPainterPath, QPen, QPixmap, QPolygonF
from PySide6.QtWidgets import (QApplication, QBoxLayout, QButtonGroup, QColorDialog, QFrame, QHBoxLayout, QLabel,
                               QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout,
                               QGridLayout, QMenu, QPlainTextEdit, QScrollArea,
                               QSlider, QSpinBox, QStyledItemDelegate, QToolButton, QVBoxLayout, QWidget)

from .theme import button
from .editor import DEFAULT_ANNOTATION_COLOR, Mark, paint_reference_arrow


TOOLS = [("select", "移动 / 调整选区"), ("rect", "矩形"), ("ellipse", "椭圆"),
         ("line", "直线"), ("arrow", "箭头"), ("polyline", "折线（Enter 完成）"),
         ("pen", "画笔"), ("marker", "高亮"), ("text", "文字"),
         ("mosaic", "马赛克"), ("blur", "模糊"), ("eraser", "擦除")]
TOOLS += [("number", "序号"), ("magnify", "放大镜标注"), ("objects", "自由选中标注对象")]


class PaletteButton(QToolButton):
    alpha_changed = Signal(QColor)
    reordered = Signal(int, int)

    def __init__(self, value, index):
        super().__init__()
        self.value, self.index = QColor(value), index
        self.setAcceptDrops(True)

    def wheelEvent(self, event):
        self.value.setAlpha(max(0, min(255, self.value.alpha()+(10 if event.angleDelta().y() > 0 else -10))))
        self.alpha_changed.emit(QColor(self.value))

    def mousePressEvent(self, event):
        self.drag_origin = event.position().toPoint()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.LeftButton and (event.position().toPoint()-self.drag_origin).manhattanLength() > 8:
            drag = QDrag(self)
            mime = QMimeData()
            mime.setData("application/x-floatshot-palette", str(self.index).encode())
            drag.setMimeData(mime)
            drag.exec(Qt.MoveAction)

    def dragEnterEvent(self, event):
        if event.mimeData().hasFormat("application/x-floatshot-palette"): event.acceptProposedAction()

    def dropEvent(self, event):
        self.reordered.emit(int(bytes(event.mimeData().data("application/x-floatshot-palette"))), self.index)
        event.acceptProposedAction()


def tool_icon(kind, color="#e6edf4"):
    pixmap = QPixmap(22, 22)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(QPen(QColor(color), 1.7, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    if kind == "shapes":
        p.drawRect(QRectF(2, 4, 12, 10))
        p.setBrush(QColor("#eeeeee"))
        p.drawEllipse(QRectF(10, 10, 10, 7))
    elif kind == "rect":
        p.drawRect(QRectF(4, 5, 14, 12))
    elif kind == "ellipse":
        p.drawEllipse(QRectF(4, 4, 14, 14))
    elif kind in ("line", "arrow", "pen", "marker"):
        if kind == "marker":
            p.setPen(QPen(QColor(color), 6, Qt.SolidLine, Qt.FlatCap))
        p.drawLine(QPointF(5, 17), QPointF(17, 5))
        if kind == "arrow":
            p.drawPolyline(QPolygonF([QPointF(10, 5), QPointF(17, 5), QPointF(17, 12)]))
        elif kind == "pen":
            p.drawPolyline(QPolygonF([QPointF(5, 13), QPointF(4, 18), QPointF(9, 17)]))
    elif kind == "polyline":
        p.drawPolyline(QPolygonF([QPointF(3, 16), QPointF(8, 7), QPointF(13, 13), QPointF(19, 5)]))
    elif kind == "text":
        font = p.font()
        font.setPixelSize(20)
        font.setBold(True)
        p.setFont(font)
        p.drawText(QRectF(1, 0, 20, 22), Qt.AlignCenter, "T")
    elif kind == "mosaic":
        for x in range(3):
            for y in range(3):
                if (x+y) % 2 == 0:
                    p.fillRect(QRectF(4+x*5, 4+y*5, 5, 5), QColor(color))
        p.drawRect(QRectF(4, 4, 15, 15))
    elif kind == "blur":
        for size in (4, 9, 15):
            p.drawEllipse(QRectF(11-size/2, 11-size/2, size, size))
    elif kind in ("number", "magnify", "objects"):
        p.drawEllipse(QRectF(3, 3, 15, 15))
        if kind == "number": p.drawText(QRectF(3, 3, 15, 15), Qt.AlignCenter, "1")
        elif kind == "magnify": p.drawLine(QPointF(16, 16), QPointF(21, 21))
        else: p.drawRect(QRectF(8, 8, 12, 12))
    elif kind == "more":
        p.drawText(QRectF(0, 0, 22, 22), Qt.AlignCenter, "···")
    elif kind == "eraser":
        p.drawPolygon(QPolygonF([QPointF(4, 14), QPointF(12, 4), QPointF(19, 10),
                                QPointF(12, 18), QPointF(8, 18)]))
        p.drawLine(QPointF(8, 10), QPointF(15, 15))
    elif kind in ("undo", "redo"):
        if kind == "redo":
            p.translate(22, 0)
            p.scale(-1, 1)
        p.drawArc(QRectF(5, 7, 13, 11), 0, 180*16)
        p.drawPolyline(QPolygonF([QPointF(8, 3), QPointF(4, 8), QPointF(10, 9)]))
    elif kind == "save":
        p.drawRect(QRectF(4, 3, 14, 16))
        p.drawRect(QRectF(7, 3, 8, 6))
        p.drawRect(QRectF(7, 13, 8, 6))
    elif kind == "copy":
        p.drawRect(QRectF(8, 7, 11, 12))
        p.drawPolyline(QPolygonF([QPointF(5, 15), QPointF(3, 15), QPointF(3, 3), QPointF(14, 3)]))
    elif kind == "pin":
        p.drawPolyline(QPolygonF([QPointF(7, 4), QPointF(15, 4), QPointF(14, 9),
                                 QPointF(17, 12), QPointF(5, 12), QPointF(8, 9), QPointF(7, 4)]))
        p.drawLine(QPointF(11, 12), QPointF(11, 20))
    elif kind == "cancel":
        p.drawLine(QPointF(5, 5), QPointF(17, 17))
        p.drawLine(QPointF(17, 5), QPointF(5, 17))
    else:
        p.drawLine(QPointF(3, 11), QPointF(19, 11))
        p.drawLine(QPointF(11, 3), QPointF(11, 19))
        for a, b, c in [((3, 11), (6, 8), (6, 14)), ((19, 11), (16, 8), (16, 14)),
                        ((11, 3), (8, 6), (14, 6)), ((11, 19), (8, 16), (14, 16))]:
            p.drawPolyline(QPolygonF([QPointF(*b), QPointF(*a), QPointF(*c)]))
    p.end()
    return QIcon(pixmap)


class RecordingToolbar(QFrame):
    tool_selected = Signal(str)
    action_requested = Signal(str)
    color_selected = Signal(QColor)
    width_changed = Signal(int)
    font_changed = Signal(int)
    rotation_changed = Signal(int)
    styles_changed = Signal(dict)

    def __init__(self, parent, recording=False):
        super().__init__(parent)
        self.recording = recording
        self.setObjectName("captureToolbar")
        self.setStyleSheet("""
            QFrame#captureToolbar { background: #14252d; border: 1px solid #48d6be; border-radius: 7px; }
            QToolButton { padding: 3px; border-radius: 4px; }
            QSpinBox { padding: 3px; min-height: 20px; }
            QLabel { border: none; font-size: 12px; }
        """)
        root = QVBoxLayout(self)
        root.setContentsMargins(7, 6, 7, 6)
        root.setSpacing(5)
        self.main = QBoxLayout(QBoxLayout.LeftToRight)
        self.main.setSpacing(6)
        root.addLayout(self.main)
        self.tool_buttons = {}
        self.actions = {}
        if not recording:
            tool_widget = QWidget(self)
            tools = QHBoxLayout(tool_widget)
            tools.setContentsMargins(0, 0, 0, 0)
            tools.setSpacing(2)
            group = QButtonGroup(self)
            for kind, title in TOOLS:
                control = self.icon_button(kind, title)
                control.setCheckable(True)
                control.setChecked(kind == "select")
                control.clicked.connect(lambda checked=False, k=kind: self.tool_selected.emit(k))
                group.addButton(control)
                tools.addWidget(control)
                self.tool_buttons[kind] = control
            self.main.addWidget(tool_widget)
        action_widget = QWidget(self)
        actions = QHBoxLayout(action_widget)
        actions.setContentsMargins(0, 0, 0, 0)
        actions.setSpacing(2)
        entries = [("record", "开始录制 →"), ("cancel", "取消 · Esc")] if recording else [
            ("undo", "撤销 · Ctrl+Z"), ("redo", "重做 · Ctrl+Y"), ("cancel", "取消 · Esc"),
            ("more", "更多：尺寸、画板、识别"), ("pin", "贴图 · Ctrl+T"), ("save", "保存 · Ctrl+S"), ("copy", "复制 · Enter")]
        for action, title in entries:
            if action == "record":
                control = button(title, lambda: self.action_requested.emit("record"), True)
            else:
                control = self.icon_button(action, title)
                control.clicked.connect(lambda checked=False, a=action: self.action_requested.emit(
                    "quick_save" if a == "save" and QApplication.keyboardModifiers() & Qt.ShiftModifier else a))
                if action == "copy":
                    control.setText("复制")
                    control.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
                    control.setFixedWidth(66)
                    control.setStyleSheet("QToolButton { background: #48d6be; color: #0c2722; border-color: #48d6be; }")
                    control.setIcon(tool_icon("copy", "#0c2722"))
            actions.addWidget(control)
            self.actions[action] = control
        self.main.addWidget(action_widget)
        self.options = QWidget(self)
        self.options.setObjectName("toolbarOptions")
        options = QHBoxLayout(self.options)
        options.setContentsMargins(0, 0, 0, 0)
        options.setSpacing(5)
        self.color_group = QButtonGroup(self)
        self.palette_layout = QHBoxLayout()
        self.palette_layout.setSpacing(2)
        options.addLayout(self.palette_layout)
        self.store = getattr(parent, "store", None)
        self.palette = list(self.store.config.get("palette", []) if self.store else []) or [
            "#f45b69", "#ffb84d", "#f4e56a", "#48d6be", "#5aabff", "#bd8bfa", "#ffffff", "#151b25"]
        self.rebuild_palette()
        self.custom_color = button("颜色…", self.choose_custom_color)
        self.custom_color.setStyleSheet("padding: 3px 5px;")
        options.addWidget(self.custom_color)
        self.style_button = button("样式…", self.choose_style)
        self.style_button.setStyleSheet("padding: 3px 5px;")
        options.addWidget(self.style_button)
        options.addSpacing(5)
        options.addWidget(QLabel("粗细"))
        self.pen_width = QSpinBox()
        self.pen_width.setRange(1, 40)
        self.pen_width.setValue(4)
        self.pen_width.setFixedWidth(50)
        self.pen_width.valueChanged.connect(self.width_changed.emit)
        options.addWidget(self.pen_width)
        self.tip_row = QHBoxLayout()
        options.addLayout(self.tip_row)
        self.rebuild_tips()
        self.font_label = QLabel("字号")
        self.font_size = QSpinBox()
        self.font_size.setRange(10, 120)
        self.font_size.setValue(22)
        self.font_size.setFixedWidth(55)
        self.font_size.valueChanged.connect(self.font_changed.emit)
        options.addWidget(self.font_label)
        options.addWidget(self.font_size)
        self.rotation_label = QLabel("角度")
        self.rotation = QSpinBox()
        self.rotation.setRange(-180, 180)
        self.rotation.setSuffix("°")
        self.rotation.setFixedWidth(64)
        self.rotation.valueChanged.connect(self.rotation_changed.emit)
        options.addWidget(self.rotation_label)
        options.addWidget(self.rotation)
        self.current_color = QColor("#f45b69")
        self.session = None
        options.addStretch()
        self.options_scroll = QScrollArea()
        self.options_scroll.setWidget(self.options)
        self.options_scroll.setWidgetResizable(True)
        self.options_scroll.setFrameShape(QFrame.NoFrame)
        self.options_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.options_scroll.setFixedHeight(46)
        root.addWidget(self.options_scroll)
        self.options_scroll.hide()
        self.set_tool("select")
        self.set_history(False, False)
        self.adjustSize()
        self.hide()

    def rebuild_palette(self):
        while self.palette_layout.count():
            widget = self.palette_layout.takeAt(0).widget()
            self.color_group.removeButton(widget)
            widget.deleteLater()
        for index, value in enumerate(self.palette):
            control = PaletteButton(value, index)
            control.setCheckable(True)
            control.setChecked(value == "#f45b69")
            control.setFixedSize(22, 22)
            control.setToolTip(value.upper())
            control.setStyleSheet(f"QToolButton {{ background: {value}; border: 2px solid #415464; }}"
                                 "QToolButton:checked { border: 2px solid white; }")
            control.clicked.connect(lambda checked=False, c=control: self.color_selected.emit(QColor(c.value)))
            control.alpha_changed.connect(self.color_selected.emit)
            control.reordered.connect(self.reorder_palette)
            self.color_group.addButton(control)
            self.palette_layout.addWidget(control)

    def reorder_palette(self, first, last):
        self.palette.insert(last, self.palette.pop(first))
        self.rebuild_palette()
        if self.store:
            self.store.config["palette"] = self.palette
            self.store.save_config()

    def rebuild_tips(self):
        while self.tip_row.count(): self.tip_row.takeAt(0).widget().deleteLater()
        for value in (self.store.config.get("pen_tips", [2, 4, 8]) if self.store else [2, 4, 8]):
            control = QToolButton()
            control.setText(str(value))
            control.setFixedSize(22, 22)
            control.setToolTip(f"笔尖 {value} px")
            control.clicked.connect(lambda checked=False, v=value: self.pen_width.setValue(v))
            self.tip_row.addWidget(control)

    def icon_button(self, kind, title):
        control = QToolButton()
        control.setIcon(tool_icon(kind))
        control.setToolTip(title)
        control.setFixedSize(28, 30)
        return control

    def set_tool(self, kind):
        if self.recording:
            return
        self.tool_buttons[kind].setChecked(True)
        self.options_scroll.setVisible(kind != "select")
        self.font_label.setVisible(kind in ("text", "number"))
        self.font_size.setVisible(kind in ("text", "number"))
        self.rotation_label.setVisible(kind not in ("select", "line", "arrow", "polyline"))
        self.rotation.setVisible(kind not in ("select", "line", "arrow", "polyline"))
        self.adjustSize()

    def set_history(self, undo, redo):
        if not self.recording:
            self.actions["undo"].setEnabled(undo)
            self.actions["redo"].setEnabled(redo)

    def fit_width(self, available):
        self.main.setDirection(QBoxLayout.TopToBottom if available < 760 else QBoxLayout.LeftToRight)
        self.options_scroll.setMaximumWidth(max(300, min(available-20, 850)))
        self.adjustSize()

    def choose_custom_color(self):
        color = QColorDialog.getColor(self.current_color, self, "标注颜色", QColorDialog.ShowAlphaChannel)
        if color.isValid():
            self.set_color(color)
            self.color_selected.emit(color)

    def set_color(self, color):
        self.current_color = QColor(color)
        self.custom_color.setStyleSheet(f"padding: 3px 5px; border: 2px solid {color.name()};")
        for control in self.color_group.buttons(): control.setChecked(control.value == color)

    def sync_session(self, session):
        self.session = session
        self.set_tool(session.tool)
        self.set_color(session.color)
        for control, value in ((self.pen_width, session.pen_width), (self.font_size, session.font_size),
                               (self.rotation, session.rotation)):
            control.blockSignals(True)
            control.setValue(value)
            control.blockSignals(False)

    def choose_style(self):
        if not self.session: return
        dialog = QDialog(self)
        dialog.setWindowTitle("标注样式")
        form = QFormLayout(dialog)
        controls = {}
        for key, title, choices in [
            ("dash", "线条", [("实线", "solid"), ("虚线", "dash"), ("点线", "dot")]),
            ("arrow_mode", "箭头方向", [("正向", "forward"), ("反向", "reverse"), ("双向", "both")]),
            ("arrow_head", "箭头样式", [("实心", "filled"), ("开放", "open"), ("圆点", "circle")]),
            ("area_shape", "高亮 / 擦除 / 像素处理形状", [("笔画", "stroke"), ("矩形", "rect"), ("椭圆", "ellipse")])]:
            combo = QComboBox()
            for text, value in choices: combo.addItem(text, value)
            combo.setCurrentIndex(max(0, combo.findData(getattr(self.session, key))))
            controls[key] = combo
            form.addRow(title, combo)
        for key, title, low, high in [("radius", "矩形圆角", 0, 200), ("outline_width", "文字描边宽度", 0, 12),
                                      ("number", "下一个序号", 1, 999), ("magnification", "放大镜倍数", 2, 8)]:
            spin = QSpinBox()
            spin.setRange(low, high)
            spin.setValue(getattr(self.session, key))
            controls[key] = spin
            form.addRow(title, spin)
        tips = []
        values = self.store.config.get("pen_tips", [2, 4, 8]) if self.store else [2, 4, 8]
        for i in range(5):
            spin = QSpinBox()
            spin.setRange(0, 40)
            spin.setValue(values[i] if i < len(values) else 0)
            tips.append(spin)
            form.addRow(f"笔尖 {i+1}（0 表示关闭）", spin)
        colors = {key: getattr(self.session, key) for key in ("text_background", "text_outline")}
        for key, title in (("text_background", "文字背景（可透明）"), ("text_outline", "文字描边颜色")):
            def pick(checked=False, k=key):
                color = QColorDialog.getColor(QColor(colors[k]), dialog, "颜色", QColorDialog.ShowAlphaChannel)
                if color.isValid(): colors[k] = color.name(QColor.HexArgb)
            form.addRow(title, button("选择颜色…", pick))
        form.addRow(button("将当前颜色加入色板", lambda: self.add_palette_color()))
        form.addRow(button("导出色板…", self.export_palette))
        form.addRow(button("导入色板…", self.import_palette))
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)
        if dialog.exec() == QDialog.Accepted:
            values = {key: control.currentData() if isinstance(control, QComboBox) else control.value()
                      for key, control in controls.items()}
            self.styles_changed.emit({**values, **colors})
            if self.store:
                self.store.config["pen_tips"] = [spin.value() for spin in tips if spin.value()] or [4]
                self.store.save_config()
                self.rebuild_tips()
        dialog.deleteLater()

    def add_palette_color(self):
        if len(self.palette) < 32:
            self.palette.append(self.current_color.name(QColor.HexArgb))
            self.reorder_palette(len(self.palette)-1, len(self.palette)-1)

    def export_palette(self):
        path, _ = QFileDialog.getSaveFileName(self, "导出色板", "FloatShot-palette.json", "JSON (*.json)")
        if path:
            from pathlib import Path
            Path(path).write_text(json.dumps(self.palette), encoding="utf-8")

    def import_palette(self):
        path, _ = QFileDialog.getOpenFileName(self, "导入色板", "", "JSON (*.json)")
        if not path: return
        from pathlib import Path
        from PySide6.QtWidgets import QMessageBox
        try:
            values = json.loads(Path(path).read_text(encoding="utf-8"))
            if not isinstance(values, list) or not 1 <= len(values) <= 32 or not all(isinstance(v, str) and QColor(v).isValid() for v in values):
                raise ValueError("色板应包含 1–32 个有效颜色值。")
            self.palette = values
            self.reorder_palette(0, 0)
        except (OSError, ValueError) as error:
            QMessageBox.warning(self, "导入失败", str(error))


TOOL_GROUPS = [
    ("shapes", "形状 · Ctrl+1", ("rect", "ellipse")),
    ("lines", "线条 / 箭头 · Ctrl+2", ("line", "arrow", "polyline")),
    ("pen", "画笔 · Ctrl+3 / B", ("pen",)),
    ("marker", "记号笔 · Ctrl+4", ("marker",)),
    ("pixels", "马赛克 / 模糊 · Ctrl+5", ("mosaic", "blur")),
    ("text", "文本 · Ctrl+6 / T", ("text",)),
    ("eraser", "橡皮擦 · Ctrl+7", ("eraser",)),
]


def annotation_shortcut(key, modifiers):
    if modifiers == Qt.ControlModifier and Qt.Key_1 <= key <= Qt.Key_7:
        return TOOL_GROUPS[key-Qt.Key_1][0]
    if not modifiers:
        return {Qt.Key_B: "pen", Qt.Key_T: "text"}.get(key)
    return None


def line_style_icon(data):
    kind, head = data.split(":")
    pixmap = QPixmap(48, 22)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(QPen(QColor("#252525"), 2, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    if kind == "polyline":
        p.drawPolyline(QPolygonF([QPointF(4, 16), QPointF(16, 6), QPointF(28, 16), QPointF(42, 6)]))
    elif head in ("tapered", "filled", "hollow"):
        paint_reference_arrow(p, Mark("arrow", [QPointF(5, 11), QPointF(42, 11)], QColor("#252525"), 2.6, arrow_head=head))
    else:
        p.drawLine(QPointF(4, 11), QPointF(40, 11))
    if kind == "arrow" and head not in ("tapered", "filled", "hollow"):
        points = [QPointF(30, 5), QPointF(40, 11), QPointF(30, 17)]
        if head == "circle":
            p.setBrush(QColor("#252525"))
            p.drawEllipse(QPointF(38, 11), 4, 4)
        elif head == "open":
            p.drawPolyline(QPolygonF(points))
        elif head == "diamond":
            p.drawPolygon(QPolygonF([QPointF(30, 5), QPointF(40, 11), QPointF(30, 17), QPointF(20, 11)]))
        else:
            p.setBrush(QColor("#252525"))
            p.drawPolygon(QPolygonF(points))
    p.end()
    return QIcon(pixmap)


class LineStyleDelegate(QStyledItemDelegate):
    def sizeHint(self, option, index):
        return QSize(72, 30)


class LineStyleBox(QComboBox):
    """Four icon-only choices, in the same order as the reference dropdown."""
    def __init__(self):
        super().__init__()
        for title, data in (("直线", "line:"), ("渐宽实心箭头", "arrow:tapered"),
                            ("等宽实心箭头", "arrow:filled"), ("渐宽空心箭头", "arrow:hollow")):
            self.addItem(line_style_icon(data), "", data)
            self.setItemData(self.count()-1, title, Qt.ToolTipRole)
        self.setIconSize(QSize(48, 22))
        self.setFixedSize(76, 30)
        self.setItemDelegate(LineStyleDelegate(self))
        self.setMaxVisibleItems(4)
        self.currentIndexChanged.connect(lambda index: self.setToolTip(self.itemData(index, Qt.ToolTipRole) or "高级线型"))


def direction_icon(mode, enabled=True):
    pixmap = QPixmap(24, 22)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(QPen(QColor("#252525" if enabled else "#aaaaaa"), 1.8, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    for y, reverse in ((7, mode == "reverse"), (15, mode != "forward")):
        p.drawLine(QPointF(3, y), QPointF(21, y))
        tip, offset = (3, 4) if reverse else (21, -4)
        p.drawPolyline(QPolygonF([QPointF(tip+offset, y-3), QPointF(tip, y), QPointF(tip+offset, y+3)]))
    p.end()
    return QIcon(pixmap)


def capture_icon(kind, color="#252525"):
    if kind not in ("mosaic", "marker", "pin", "undo", "redo"):
        return tool_icon(kind, color)
    pixmap = QPixmap(22, 22)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing)
    p.setPen(QPen(QColor(color), 1.5, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    if kind == "mosaic":
        p.fillRect(QRectF(3, 3, 8, 8), QColor(color))
        p.fillRect(QRectF(11, 11, 8, 8), QColor(color))
        p.drawRect(QRectF(3, 3, 16, 16))
    elif kind == "marker":
        p.drawPolygon(QPolygonF([QPointF(5, 13), QPointF(13, 4), QPointF(19, 9), QPointF(10, 18)]))
        p.drawLine(QPointF(11, 6), QPointF(17, 11))
        p.drawPolygon(QPolygonF([QPointF(5, 13), QPointF(10, 18), QPointF(3, 19)]))
    elif kind == "pin":
        p.translate(11, 11)
        p.rotate(40)
        p.setBrush(QColor(color))
        p.drawPolygon(QPolygonF([QPointF(-5, -8), QPointF(5, -8), QPointF(3, -6),
                                QPointF(3, -1), QPointF(6, 2), QPointF(-6, 2),
                                QPointF(-3, -1), QPointF(-3, -6)]))
        p.drawLine(QPointF(0, 2), QPointF(0, 10))
    else:
        if kind == "redo": p.translate(22, 0); p.scale(-1, 1)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(color))
        path = QPainterPath(QPointF(2, 9))
        path.lineTo(10, 2)
        path.lineTo(10, 6)
        path.cubicTo(19, 6, 21, 12, 18, 19)
        path.cubicTo(18, 12, 14, 11, 10, 11)
        path.lineTo(10, 15)
        path.closeSubpath()
        p.drawPath(path)
    p.end()
    return QIcon(pixmap)


class ColorSwatchButton(QToolButton):
    def __init__(self):
        super().__init__()
        self.color = QColor(DEFAULT_ANNOTATION_COLOR)
        self.setFixedSize(32, 32)

    def paintEvent(self, event):
        p = QPainter(self)
        p.fillRect(self.rect().adjusted(1, 1, -1, -1), Qt.white)
        p.fillRect(self.rect().adjusted(2, 2, -2, -2), self.color)
        p.setPen(QColor("#777777"))
        p.drawRect(self.rect().adjusted(1, 1, -2, -2))
        corner = QPainterPath(QPointF(30, 21))
        corner.lineTo(30, 30)
        corner.lineTo(21, 30)
        corner.closeSubpath()
        p.setClipPath(corner)
        for y, value in enumerate(("#2366d7", "#28b55c", "#ffe632", "#ff7a16", "#ea235c")):
            p.fillRect(QRectF(21, 21+y*2, 10, 2), QColor(value))


class BrushSizeButton(QToolButton):
    """A size swatch; its wheel and popup drive the same width model."""
    def __init__(self, model, parent=None, compact=False):
        super().__init__(parent)
        self.model = model
        self.compact = compact
        self.setFixedSize(32, 34) if compact else self.setFixedSize(36, 36)
        if compact:
            self.setStyleSheet("QToolButton { background: #167be4; border: none; border-radius: 0; }")
        self.setToolTip("画笔粗细 · 滚轮 / 1、2\n单击展开笔尖")
        self.model.valueChanged.connect(lambda _: self.update())

    def wheelEvent(self, event):
        if event.angleDelta().y():
            self.model.setValue(self.model.value()+(1 if event.angleDelta().y() > 0 else -1))
        event.accept()

    def paintEvent(self, event):
        super().paintEvent(event)
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        radius = min(12, max(1, self.model.value()/2))
        p.setPen(Qt.NoPen)
        p.setBrush(Qt.white if self.compact else QColor("#252525"))
        p.drawEllipse(QPointF(16, 17) if self.compact else QPointF(18, 14), radius, radius)
        if self.compact: return
        font = p.font()
        font.setPixelSize(10)
        p.setFont(font)
        p.setPen(QColor("#252525"))
        p.drawText(QRectF(0, 25, 36, 10), Qt.AlignCenter, str(self.model.value()))


class FontFamilyBox(QComboBox):
    """Enumerate system fonts only when the user opens the font list."""
    currentFontChanged = Signal(QFont)
    families = None

    def __init__(self):
        super().__init__()
        self.addItems(["Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI"])
        self.currentTextChanged.connect(lambda text: self.currentFontChanged.emit(QFont(text)))

    def currentFont(self):
        return QFont(self.currentText())

    def setCurrentFont(self, font):
        family = font.family()
        if self.findText(family) < 0: self.addItem(family)
        self.setCurrentText(family)

    def showPopup(self):
        family = self.currentText()
        if self.families is None:
            type(self).families = QFontDatabase.families()
        self.blockSignals(True)
        for name in self.families:
            if self.findText(name) < 0: self.addItem(name)
        self.setCurrentText(family)
        self.blockSignals(False)
        super().showPopup()


_rotation_cursor = None


def editing_cursor(target):
    global _rotation_cursor
    if target == "rotate":
        if _rotation_cursor is None:
            pixmap = QPixmap(28, 28)
            pixmap.fill(Qt.transparent)
            p = QPainter(pixmap)
            p.setRenderHint(QPainter.Antialiasing)
            for color, width in ((Qt.white, 4), (Qt.black, 2)):
                p.setPen(QPen(color, width, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
                p.drawArc(QRectF(5, 5, 18, 18), 25*16, 270*16)
                p.drawPolyline(QPolygonF([QPointF(18, 4), QPointF(24, 6), QPointF(23, 12)]))
            p.end()
            _rotation_cursor = QCursor(pixmap, 14, 14)
        return _rotation_cursor
    return {"nw": Qt.SizeFDiagCursor, "se": Qt.SizeFDiagCursor,
            "ne": Qt.SizeBDiagCursor, "sw": Qt.SizeBDiagCursor,
            "n": Qt.SizeVerCursor, "s": Qt.SizeVerCursor,
            "e": Qt.SizeHorCursor, "w": Qt.SizeHorCursor, "move": Qt.SizeAllCursor,
            "start": Qt.SizeAllCursor, "end": Qt.SizeAllCursor}.get(target)


class CaptureToolbar(RecordingToolbar):
    """Seven tool groups with parameters edited beside the selection."""
    def __init__(self, parent, recording=False):
        QFrame.__init__(self, parent)
        self.recording = False
        self.store = getattr(parent, "store", None)
        self.session = None
        self.current_tool = "select"
        self.current_color = QColor(DEFAULT_ANNOTATION_COLOR)
        self.group_choice = {"shapes": "rect", "lines": "arrow", "pixels": "mosaic"}
        saved_groups = self.store.config.get("annotation_tool_groups", {}) if self.store else {}
        for name, _, kinds in TOOL_GROUPS:
            if saved_groups.get(name) in kinds: self.group_choice[name] = saved_groups[name]
        if self.store:
            self.current_color = QColor(self.store.config.get("annotation_color", DEFAULT_ANNOTATION_COLOR))
        self.tip_sizes = list(self.store.config.get("pen_tips", [2, 4, 8]) if self.store else [2, 4, 8])
        self.available_width = 800
        self.setObjectName("captureToolbar")
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setStyleSheet("""
            QFrame#captureToolbar { background: transparent; border: none; }
            QFrame#captureMain, QFrame#toolbarOptions, QFrame#annotationPopup {
                background: #eeeeee; border: 1px solid #aaa; border-radius: 0; }
            QToolButton { background: transparent; color: #222; border: 1px solid transparent;
                padding: 2px; border-radius: 1px; }
            QToolButton:hover { background: #dceafa; border-color: #90bdf0; }
            QToolButton:checked { background: #167be4; border-color: #126ecb; }
            QToolButton:disabled { color: #aaa; background: transparent; border: none; }
            QComboBox, QSpinBox { background: #fafafa; color: #222; border: 1px solid #bbb;
                padding: 2px; border-radius: 1px; min-height: 24px; }
            QComboBox QAbstractItemView { background: #fafafa; color: #222; selection-background-color: #167be4; }
            QLabel { color: #222; border: none; background: transparent; }
            QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
        """)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(3)
        self.main_frame = QFrame(self)
        self.main_frame.setObjectName("captureMain")
        self.main = QBoxLayout(QBoxLayout.LeftToRight, self.main_frame)
        self.main.setContentsMargins(1, 1, 1, 1)
        self.main.setSpacing(0)
        root.addWidget(self.main_frame, 0, Qt.AlignLeft)
        self.tool_buttons = {}
        self.actions = {}
        self.group_buttons = {}
        group = QButtonGroup(self)
        self.tool_group = group
        tools_widget = QWidget(self)
        tools_row = QHBoxLayout(tools_widget)
        tools_row.setContentsMargins(0, 0, 0, 0)
        tools_row.setSpacing(1)
        for name, title, kinds in TOOL_GROUPS:
            icon = {"shapes": "shapes", "lines": "polyline", "pixels": "mosaic"}.get(name, name)
            control = self.icon_button(icon, title)
            control.setCheckable(True)
            control.clicked.connect(lambda checked=False, n=name: self.activate_group(n))
            group.addButton(control)
            tools_row.addWidget(control)
            self.group_buttons[name] = control
            for kind in kinds: self.tool_buttons[kind] = control
        self.main.addWidget(tools_widget)
        separator = QFrame()
        separator.setFrameShape(QFrame.VLine)
        separator.setFixedSize(8, 24)
        self.main.addWidget(separator)
        action_widget = QWidget(self)
        action_row = QHBoxLayout(action_widget)
        action_row.setContentsMargins(0, 0, 0, 0)
        action_row.setSpacing(1)
        for action, title in [
            ("undo", "撤销 · Ctrl+Z"), ("redo", "重做 · Ctrl+Y / Ctrl+Shift+Z"),
            ("cancel", "退出截图 · Esc"), ("pin", "贴图 · F3 / Ctrl+T / 中键"),
            ("save", "保存 · Ctrl+S / Ctrl+Shift+S 快速保存"),
            ("copy", "复制 · Enter / Ctrl+C"), ("more", "更多工具与操作 · F10")]:
            control = self.icon_button(action, title)
            control.clicked.connect(lambda checked=False, a=action: self.action_requested.emit(
                "quick_save" if a == "save" and QApplication.keyboardModifiers() & Qt.ShiftModifier else a))
            self.actions[action] = control
            if action == "more":
                control.setParent(self)
                control.hide()
                continue
            if action == "cancel":
                separator = QFrame()
                separator.setFrameShape(QFrame.VLine)
                separator.setFixedSize(8, 24)
                action_row.addWidget(separator)
            action_row.addWidget(control)
        self.main.addWidget(action_widget)
        self.options = QFrame(self)
        self.options.setObjectName("toolbarOptions")
        options = QHBoxLayout(self.options)
        options.setContentsMargins(1, 1, 2, 1)
        options.setSpacing(3)
        # The numeric model is deliberately hidden; the swatch is the visible control.
        self.pen_width = QSpinBox(self)
        self.pen_width.setRange(1, 40)
        self.pen_width.setValue(4)
        self.pen_width.hide()
        self.pen_width.valueChanged.connect(self.width_changed.emit)
        self.size_button = BrushSizeButton(self.pen_width, self, compact=True)
        self.size_button.clicked.connect(self.show_size_popup)
        options.addWidget(self.size_button)
        self.specific = {}
        self.shape_type = self.combo(options, "shape", [("矩形", "rect"), ("椭圆", "ellipse")])
        self.shape_type.currentIndexChanged.connect(lambda _: self.tool_selected.emit(self.shape_type.currentData()))
        self.line_type = LineStyleBox()
        self.specific["line"] = self.line_type
        options.addWidget(self.line_type)
        for i in range(self.shape_type.count()):
            self.shape_type.setItemIcon(i, tool_icon(self.shape_type.itemData(i), "#252525"))
        self.line_type.currentIndexChanged.connect(self.change_line_type)
        self.direction = QToolButton()
        self.direction.setFixedSize(32, 30)
        self.direction.setFocusPolicy(Qt.NoFocus)
        self.direction_mode = "forward"
        self.direction.clicked.connect(self.cycle_direction)
        self.specific["direction"] = self.direction
        options.addWidget(self.direction)
        self.color_separator = QFrame()
        self.color_separator.setFrameShape(QFrame.VLine)
        self.color_separator.setFixedSize(7, 24)
        options.addWidget(self.color_separator)
        self.dash = self.combo(options, "dash", [("━━", "solid"), ("┄┄", "dash"), ("····", "dot")])
        self.dash.setToolTip("线条样式")
        self.dash.currentIndexChanged.connect(lambda _: self.styles_changed.emit({"dash": self.dash.currentData()}))
        self.area_shape = self.combo(options, "area", [("笔画", "stroke"), ("矩形区域", "rect"), ("椭圆区域", "ellipse")])
        self.area_shape.currentIndexChanged.connect(lambda _: self.styles_changed.emit({"area_shape": self.area_shape.currentData()}))
        self.pixel_type = self.combo(options, "pixels", [("马赛克", "mosaic"), ("模糊", "blur")])
        self.pixel_type.currentIndexChanged.connect(lambda _: self.tool_selected.emit(self.pixel_type.currentData()))
        self.intensity = QSlider(Qt.Horizontal)
        self.intensity.setRange(1, 40)
        self.intensity.setValue(12)
        self.intensity.setFixedWidth(100)
        self.intensity.setToolTip("处理强度，独立于画笔粗细")
        self.intensity.valueChanged.connect(lambda value: self.styles_changed.emit({"intensity": value}))
        self.specific["intensity"] = self.intensity
        options.addWidget(self.intensity)
        self.bold = self.toggle(options, "bold", "B", "粗体")
        self.italic = self.toggle(options, "italic", "I", "斜体")
        self.outline = self.toggle(options, "outline", "A", "文字描边")
        self.bold.toggled.connect(lambda checked: self.styles_changed.emit({"bold": checked}))
        self.italic.toggled.connect(lambda checked: self.styles_changed.emit({"italic": checked}))
        self.outline.toggled.connect(lambda checked: self.styles_changed.emit(
            {"outline_width": 1 if checked else 0, "text_outline": "#ff000000"}))
        for control, bold, italic in ((self.bold, True, False), (self.italic, False, True)):
            font = control.font()
            font.setBold(bold)
            font.setItalic(italic)
            control.setFont(font)
        self.font_family = FontFamilyBox()
        self.font_family.setFixedWidth(160)
        self.font_family.setToolTip("字体")
        self.font_family.currentFontChanged.connect(lambda font: self.styles_changed.emit({"font_family": font.family()}))
        self.specific["family"] = self.font_family
        options.addWidget(self.font_family)
        self.font_size = QSpinBox()
        self.font_size.setRange(10, 120)
        self.font_size.setValue(22)
        self.font_size.setFixedWidth(50)
        self.font_size.setToolTip("字号 · 滚轮 / 1、2")
        self.font_size.valueChanged.connect(self.font_changed.emit)
        self.specific["font_size"] = self.font_size
        options.addWidget(self.font_size)
        self.custom_color = ColorSwatchButton()
        self.custom_color.setToolTip("当前颜色 · 单击自定义颜色")
        self.custom_color.clicked.connect(self.choose_custom_color)
        options.addWidget(self.custom_color)
        self.color_group = QButtonGroup(self)
        self.palette_widget = QWidget()
        self.palette_layout = QGridLayout(self.palette_widget)
        self.palette_layout.setContentsMargins(0, 0, 0, 0)
        self.palette_layout.setSpacing(1)
        self.palette = list(self.store.config.get("palette", []) if self.store else []) or [
            "#000000", "#606060", "#800000", DEFAULT_ANNOTATION_COLOR, "#ff8000", "#ffff00",
            "#00a650", "#00a8e8", "#2048d0", "#662db8", "#990080",
            "#ffffff", "#c0c0c0", "#a06030", "#ffc0cb", "#ffc04d", "#ffffb0",
            "#b2e37b", "#b7e8ee", "#9ebce0", "#cab9e7", "#e4d9eb"]
        self.rebuild_palette()
        options.addWidget(self.palette_widget)
        self.style_button = self.icon_button("more", "高级参数与色板")
        self.style_button.clicked.connect(self.choose_style)
        self.style_button.setParent(self)
        self.style_button.hide()
        self.rotation = QSpinBox(self)
        self.rotation.setRange(-180, 180)
        self.rotation.hide()
        self.rotation.valueChanged.connect(self.rotation_changed.emit)
        self.options_scroll = QScrollArea()
        self.options_scroll.setWidget(self.options)
        self.options_scroll.setWidgetResizable(True)
        self.options_scroll.setFrameShape(QFrame.NoFrame)
        self.options_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.options_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        self.options_scroll.setFixedHeight(36)
        root.addWidget(self.options_scroll, 0, Qt.AlignLeft)
        self.size_popup = None
        self.style_popup = None
        self.set_tool("select")
        self.set_color(self.current_color)
        self.set_history(False, False)
        self.hide()

    def icon_button(self, kind, title):
        control = QToolButton()
        control.setIcon(capture_icon(kind, "#252525"))
        control.setIconSize(QSize(22, 22))
        control.setToolTip(title)
        control.setFixedSize(32, 32)
        control.setFocusPolicy(Qt.NoFocus)
        return control

    def combo(self, layout, name, values):
        control = QComboBox()
        for title, value in values: control.addItem(title, value)
        control.setMaximumWidth(116)
        self.specific[name] = control
        layout.addWidget(control)
        return control

    def toggle(self, layout, name, title, tooltip):
        control = QToolButton()
        control.setText(title)
        control.setToolTip(tooltip)
        control.setCheckable(True)
        control.setFixedSize(28, 30)
        control.setFocusPolicy(Qt.NoFocus)
        self.specific[name] = control
        layout.addWidget(control)
        return control

    def activate_group(self, name):
        self.tool_selected.emit(self.group_choice.get(name, name))

    def change_line_type(self):
        data = self.line_type.currentData()
        if not data: return
        kind, head = data.split(":")
        if not self.session or self.session.tool != kind:
            self.tool_selected.emit(kind)
        if head: self.styles_changed.emit({"arrow_head": head})

    def cycle_direction(self):
        values = ("forward", "reverse", "both")
        self.styles_changed.emit({"arrow_mode": values[(values.index(self.direction_mode)+1) % 3]})

    def sync_direction(self, mode):
        self.direction_mode = mode
        self.direction.setIcon(direction_icon(mode, self.current_tool == "arrow"))
        title = {"forward": "正向", "reverse": "反向", "both": "双向"}.get(mode, "正向")
        self.direction.setToolTip(f"箭头方向：{title} · 单击切换")

    def contextMenuEvent(self, event):
        menu = QMenu(self)
        menu.addAction("更多工具与操作 · F10", lambda: self.action_requested.emit("more"))
        menu.addAction("高级标注参数与色板", self.choose_style)
        menu.exec(event.globalPos())

    def set_tool(self, kind):
        self.current_tool = kind
        self.tool_group.setExclusive(False)
        for name, _, kinds in TOOL_GROUPS:
            checked = kind in kinds
            self.group_buttons[name].setChecked(checked)
            icon = {"shapes": "shapes", "lines": "polyline", "pixels": "mosaic"}.get(name, name)
            self.group_buttons[name].setIcon(capture_icon(icon, "#ffffff" if checked else "#252525"))
            if checked: self.group_choice[name] = kind
        self.tool_group.setExclusive(True)
        visible = {
            "rect": {"shape", "dash"}, "ellipse": {"shape", "dash"},
            "line": {"line", "direction"}, "arrow": {"line", "direction"},
            "polyline": {"line"}, "marker": {"area"}, "eraser": {"area"},
            "mosaic": {"area", "pixels", "intensity"}, "blur": {"area", "pixels", "intensity"},
            "text": {"bold", "italic", "outline", "family", "font_size"},
            "number": {"font_size"},
        }.get(kind, set())
        for name, control in self.specific.items(): control.setVisible(name in visible)
        self.direction.setEnabled(kind == "arrow")
        self.sync_direction(self.direction_mode)
        self.size_button.setVisible(kind not in ("select", "text", "number", "objects", "magnify"))
        show_color = kind not in ("mosaic", "blur", "eraser", "objects", "select")
        self.custom_color.setVisible(show_color)
        self.palette_widget.setVisible(show_color)
        self.color_separator.setVisible(show_color)
        self.options_scroll.setVisible(kind != "select")
        self.fit_width(self.available_width)

    def fit_width(self, available):
        self.available_width = available
        self.main.setDirection(QBoxLayout.TopToBottom if available < 440 else QBoxLayout.LeftToRight)
        self.main_frame.setFixedHeight(self.main.sizeHint().height())
        wanted = min(max(160, available-20), max(160, self.options.sizeHint().width()+2))
        self.options_scroll.setFixedWidth(wanted)
        self.options_scroll.setFixedHeight(36 if self.options.sizeHint().width()+2 <= wanted else 48)
        self.options.adjustSize()
        self.adjustSize()

    def rebuild_palette(self):
        while self.palette_layout.count():
            widget = self.palette_layout.takeAt(0).widget()
            self.color_group.removeButton(widget)
            widget.deleteLater()
        columns = max(1, (len(self.palette)+1)//2)
        for index, value in enumerate(self.palette):
            control = PaletteButton(value, index)
            control.setCheckable(True)
            control.setFixedSize(16, 16)
            control.setToolTip(value.upper())
            control.setStyleSheet(f"QToolButton {{ background: {value}; border: 1px solid #777; padding: 0; }}"
                                 "QToolButton:checked { border: 2px solid #1688ef; }")
            control.clicked.connect(lambda checked=False, c=control: self.color_selected.emit(QColor(c.value)))
            control.alpha_changed.connect(self.color_selected.emit)
            control.reordered.connect(self.reorder_palette)
            self.color_group.addButton(control)
            self.palette_layout.addWidget(control, index//columns, index % columns)

    def reorder_palette(self, first, last):
        super().reorder_palette(first, last)
        self.fit_width(self.available_width)

    def set_color(self, color):
        self.current_color = QColor(color)
        self.custom_color.color = QColor(color)
        self.custom_color.update()
        for control in self.color_group.buttons(): control.setChecked(control.value == color)

    def sync_session(self, session):
        self.session = session
        controls = [
            (self.pen_width, session.pen_width), (self.font_size, session.font_size),
            (self.rotation, session.rotation), (self.intensity, session.intensity),
            (self.bold, session.bold), (self.italic, session.italic), (self.outline, bool(session.outline_width))]
        for control, value in controls:
            control.blockSignals(True)
            control.setChecked(value) if isinstance(control, QToolButton) else control.setValue(value)
            control.blockSignals(False)
        for control, value in [
            (self.shape_type, session.tool), (self.pixel_type, session.tool),
            (self.area_shape, session.area_shape), (self.dash, session.dash),
            (self.line_type, session.tool+":"+(session.arrow_head if session.tool == "arrow" else ""))]:
            control.blockSignals(True)
            index = control.findData(value)
            if index >= 0: control.setCurrentIndex(index)
            elif control is self.line_type:
                control.setPlaceholderText("折线" if session.tool == "polyline" else "高级线型")
                control.setCurrentIndex(-1)
            control.blockSignals(False)
        self.font_family.blockSignals(True)
        self.font_family.setCurrentFont(QFont(session.font_family))
        self.font_family.blockSignals(False)
        self.set_color(session.color)
        self.sync_direction(session.arrow_mode)
        self.set_tool(session.tool)

    def show_size_popup(self):
        if self.size_popup:
            self.size_popup.close()
            self.size_popup.deleteLater()
        popup = QFrame(self, Qt.Popup)
        popup.setObjectName("annotationPopup")
        layout = QVBoxLayout(popup)
        slider = QSlider(Qt.Horizontal)
        slider.setRange(1, 40)
        slider.setValue(self.pen_width.value())
        slider.setFixedWidth(180)
        slider.valueChanged.connect(self.pen_width.setValue)
        self.pen_width.valueChanged.connect(slider.setValue)
        layout.addWidget(slider)
        row = QHBoxLayout()
        for value in self.tip_sizes:
            model = QSpinBox(popup)
            model.setValue(value)
            model.hide()
            tip = BrushSizeButton(model)
            tip.setToolTip(f"笔尖 {value} px")
            tip.clicked.connect(lambda checked=False, v=value: self.pen_width.setValue(v))
            row.addWidget(tip)
        layout.addLayout(row)
        popup.adjustSize()
        self.position_popup(popup, self.size_button)
        self.size_popup = popup

    def position_popup(self, popup, control):
        area = self.screen().availableGeometry()
        point = control.mapToGlobal(QPoint(0, control.height()+4))
        if point.y()+popup.height() > area.bottom():
            point.setY(control.mapToGlobal(QPoint()).y()-popup.height()-4)
        point.setX(max(area.left(), min(point.x(), area.right()-popup.width()+1)))
        point.setY(max(area.top(), min(point.y(), area.bottom()-popup.height()+1)))
        popup.move(point)
        popup.show()

    def choose_style(self):
        if not self.session: return
        if self.style_popup:
            self.style_popup.close()
            self.style_popup.deleteLater()
        popup = QFrame(self, Qt.Popup)
        popup.setObjectName("annotationPopup")
        form = QFormLayout(popup)
        if self.current_tool in ("line", "arrow", "polyline"):
            dash = QComboBox()
            for title, value in (("实线", "solid"), ("虚线", "dash"), ("点线", "dot")):
                dash.addItem(title, value)
            dash.setCurrentIndex(max(0, dash.findData(self.session.dash)))
            dash.currentIndexChanged.connect(lambda _: self.styles_changed.emit({"dash": dash.currentData()}))
            form.addRow("线条样式", dash)
            if self.current_tool == "arrow":
                heads = QComboBox()
                for title, value in (("渐宽实心", "tapered"), ("等宽实心", "filled"), ("渐宽空心", "hollow"),
                                     ("开放箭头", "open"), ("空心菱形", "diamond"), ("圆点箭头", "circle")):
                    heads.addItem(title, value)
                heads.setCurrentIndex(max(0, heads.findData(self.session.arrow_head)))
                heads.currentIndexChanged.connect(lambda _: self.styles_changed.emit({"arrow_head": heads.currentData()}))
                form.addRow("其他箭头样式", heads)
        choices = [("rotation", "角度", -180, 180)]
        if self.current_tool == "rect": choices += [("radius", "圆角", 0, 200)]
        if self.current_tool == "text": choices += [("outline_width", "描边宽度", 0, 12)]
        if self.current_tool == "number": choices += [("number", "下一个序号", 1, 999)]
        if self.current_tool == "magnify": choices += [("magnification", "放大倍数", 2, 8)]
        for name, title, low, high in choices:
            spin = QSpinBox()
            spin.setRange(low, high)
            spin.setValue(getattr(self.session, name))
            spin.valueChanged.connect(lambda value, n=name: self.styles_changed.emit({n: value}))
            form.addRow(title, spin)
        if self.size_button.isVisible():
            presets = QWidget()
            row = QHBoxLayout(presets)
            row.setContentsMargins(0, 0, 0, 0)
            controls = []
            for i in range(5):
                spin = QSpinBox()
                spin.setRange(0, 40)
                spin.setFixedWidth(46)
                spin.setValue(self.tip_sizes[i] if i < len(self.tip_sizes) else 0)
                spin.setToolTip("笔尖尺寸；0 表示关闭")
                row.addWidget(spin)
                controls.append(spin)
            def update_tips():
                self.tip_sizes = [spin.value() for spin in controls if spin.value()] or [4]
                if self.store:
                    self.store.config["pen_tips"] = self.tip_sizes
                    self.store.save_config()
            for spin in controls: spin.valueChanged.connect(update_tips)
            form.addRow("笔尖预设", presets)
        if self.current_tool == "text":
            for name, title in (("text_background", "文字背景"), ("text_outline", "描边颜色")):
                control = QToolButton()
                control.setText(title+"…")
                control.clicked.connect(lambda checked=False, n=name: self.pick_text_color(n))
                form.addRow(control)
        for title, callback in (("将当前颜色加入色板", self.add_palette_color),
                                ("导出色板…", self.export_palette), ("导入色板…", self.import_palette)):
            control = QToolButton()
            control.setText(title)
            control.clicked.connect(callback)
            form.addRow(control)
        popup.adjustSize()
        self.position_popup(popup, self.palette_widget if self.palette_widget.isVisible() else self.main_frame)
        self.style_popup = popup

    def pick_text_color(self, name):
        color = QColorDialog.getColor(QColor(getattr(self.session, name)), self, "颜色", QColorDialog.ShowAlphaChannel)
        if color.isValid(): self.styles_changed.emit({name: color.name(QColor.HexArgb)})


class TextInput(QPlainTextEdit):
    commit_requested = Signal()
    cancel_requested = Signal()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Escape:
            self.cancel_requested.emit()
        elif event.key() in (Qt.Key_Return, Qt.Key_Enter) and event.modifiers() & Qt.ControlModifier:
            self.commit_requested.emit()
        else:
            super().keyPressEvent(event)


class InlineTextEntry(QFrame):
    committed = Signal(str)
    cancelled = Signal()

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("inlineTextEntry")
        self.setStyleSheet("QFrame#inlineTextEntry { background: #14252d; border: 1px solid #48d6be; border-radius: 6px; }")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        self.edit = TextInput()
        self.edit.setPlaceholderText("输入标注文字…")
        self.edit.setFixedHeight(80)
        self.edit.commit_requested.connect(self.commit)
        self.edit.cancel_requested.connect(self.cancelled.emit)
        layout.addWidget(self.edit)
        row = QHBoxLayout()
        hint = QLabel("Ctrl+Enter 完成 · Esc 取消")
        hint.setStyleSheet("font-size: 11px; color: #92a4b6;")
        row.addWidget(hint)
        row.addWidget(button("完成", self.commit, True))
        layout.addLayout(row)
        self.setFixedWidth(310)
        self.adjustSize()
        self.hide()

    def commit(self):
        self.committed.emit(self.edit.toPlainText())
