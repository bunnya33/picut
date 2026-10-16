from __future__ import annotations

import math
import json
from dataclasses import dataclass, field, replace

from PIL import Image, ImageFilter
from PySide6.QtCore import QMimeData, QPointF, QRectF, QSizeF, Qt, Signal
from PySide6.QtGui import QColor, QFont, QFontMetricsF, QImage, QPainter, QPainterPath, QPen, QPolygonF, QTransform
from PySide6.QtWidgets import QApplication
from PySide6.QtWidgets import (QButtonGroup, QColorDialog, QDialog, QHBoxLayout, QInputDialog,
                               QLabel, QScrollArea, QSpinBox, QToolButton, QVBoxLayout, QWidget)

from .theme import button

DEFAULT_ANNOTATION_COLOR = "#ff0004"
REFERENCE_ARROW_HEADS = {"tapered", "filled", "hollow"}


@dataclass
class Mark:
    kind: str
    points: list[QPointF] = field(default_factory=list)
    color: QColor = field(default_factory=lambda: QColor(DEFAULT_ANNOTATION_COLOR))
    width: int = 4
    text: str = ""
    font_size: int = 22
    rotation: int = 0
    dash: str = "solid"
    radius: int = 0
    arrow_mode: str = "forward"
    arrow_head: str = "filled"
    area_shape: str = "stroke"
    text_background: str = "#00000000"
    text_outline: str = "#00000000"
    outline_width: int = 0
    number: int = 1
    magnification: int = 2
    font_family: str = "Microsoft YaHei UI"
    bold: bool = False
    italic: bool = False
    intensity: int | None = None

    def to_dict(self):
        return {**self.__dict__, "points": [[p.x(), p.y()] for p in self.points],
                "color": self.color.name(QColor.HexArgb)}

    @classmethod
    def from_dict(cls, data):
        allowed = {k: v for k, v in data.items() if k in cls.__dataclass_fields__}
        allowed["points"] = [QPointF(*p) for p in allowed.get("points", [])]
        allowed["color"] = QColor(allowed.get("color", "#f45b69"))
        # Preserve the strength of annotations saved before the independent slider.
        if "intensity" not in allowed:
            allowed["intensity"] = max(6, allowed.get("width", 4)*3) if allowed.get("kind") == "mosaic" else max(4, allowed.get("width", 4)*2)
        return cls(**allowed)


def text_rect(mark):
    font = QFont(mark.font_family)
    font.setPixelSize(mark.font_size)
    font.setBold(mark.bold)
    font.setItalic(mark.italic)
    metrics = QFontMetricsF(font)
    lines = mark.text.split("\n") or [""]
    return QRectF(0, 0, max(1, max(metrics.horizontalAdvance(line) for line in lines)),
                  metrics.lineSpacing()*len(lines)), font


def arrow_path(mark):
    """One closed silhouette for both the body and head of a reference arrow."""
    first, last = mark.points[0], mark.points[-1]
    if mark.arrow_mode == "reverse": first, last = last, first
    delta = last-first
    length = math.hypot(delta.x(), delta.y())
    path = QPainterPath()
    if length < .01:
        path.addEllipse(first, mark.width/2, mark.width/2)
        return path
    tapered = mark.arrow_head in ("tapered", "hollow")
    width = min(mark.width, length*.25)
    head_length = min(max(12, width*(5 if tapered else 4.5)), length*(.4 if mark.arrow_mode == "both" else .65))
    head_half = min(max(6, width*(2.5 if tapered else 2)), length*.32)
    neck_half = min(width if tapered else width/2, head_half*.65)
    neck = head_length*.82
    tail_half = min(.5, width/8) if tapered else width/2
    path.moveTo(0, tail_half)
    if mark.arrow_mode == "both":
        path = QPainterPath(QPointF(0, 0))
        path.lineTo(head_length, head_half)
        path.lineTo(neck, neck_half)
        if tapered: path.lineTo(length/2, tail_half)
    path.lineTo(length-neck, neck_half)
    path.lineTo(length-head_length, head_half)
    path.lineTo(length, 0)
    path.lineTo(length-head_length, -head_half)
    path.lineTo(length-neck, -neck_half)
    if mark.arrow_mode == "both":
        if tapered: path.lineTo(length/2, -tail_half)
        path.lineTo(neck, -neck_half)
        path.lineTo(head_length, -head_half)
        path.lineTo(0, 0)
    else:
        path.lineTo(0, -tail_half)
        path.cubicTo(-tail_half*1.34, -tail_half, -tail_half*1.34, tail_half, 0, tail_half)
    path.closeSubpath()
    if mark.dash != "solid" and mark.arrow_head != "hollow":
        # Keep the arrowhead solid while clipping gaps into the same silhouette.
        # A single filled path also avoids alpha overlap at the neck.
        mask = QPainterPath()
        mask.setFillRule(Qt.WindingFill)
        step, painted = (width*6, width*4) if mark.dash == "dash" else (width*3, width)
        x = 0
        while x < length:
            mask.addRect(QRectF(x, -head_half-1, painted, head_half*2+2))
            x += step
        mask.addRect(QRectF(length-head_length, -head_half-1, head_length+1, head_half*2+2))
        if mark.arrow_mode == "both": mask.addRect(QRectF(-1, -head_half-1, head_length+1, head_half*2+2))
        path = path.intersected(mask)
    transform = QTransform().translate(first.x(), first.y()).rotate(math.degrees(math.atan2(delta.y(), delta.x())))
    return transform.map(path)


def arrow_outline_width(mark):
    return max(1, mark.width*.45)


def paint_reference_arrow(painter, mark):
    if mark.arrow_head == "hollow":
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(mark.color, arrow_outline_width(mark),
                            {"solid": Qt.SolidLine, "dash": Qt.DashLine, "dot": Qt.DotLine}.get(mark.dash, Qt.SolidLine),
                            Qt.RoundCap, Qt.RoundJoin))
    else:
        painter.setPen(Qt.NoPen)
        painter.setBrush(mark.color)
    painter.drawPath(arrow_path(mark))


class ImageDocument:
    def __init__(self, image: QImage):
        self.original = image.convertToFormat(QImage.Format_ARGB32)
        self.original.setDevicePixelRatio(1)
        self.marks = []
        self.redo_marks = []
        self.undo_states = []
        self.redo_states = []

    @staticmethod
    def clone(marks):
        return [replace(mark, points=[QPointF(point) for point in mark.points], color=QColor(mark.color)) for mark in marks]

    def checkpoint(self):
        self.undo_states.append(self.clone(self.marks))
        del self.undo_states[:-100]
        self.redo_states.clear()
        self.redo_marks.clear()

    def add(self, mark):
        self.checkpoint()
        self.marks.append(mark)

    def undo(self):
        if self.undo_states:
            self.redo_states.append(self.clone(self.marks))
            self.marks = self.undo_states.pop()
            self.redo_marks = self.clone(self.redo_states[-1])

    def redo(self):
        if self.redo_states:
            self.undo_states.append(self.clone(self.marks))
            self.marks = self.redo_states.pop()
            self.redo_marks = self.clone(self.redo_states[-1]) if self.redo_states else []

    def transform_marks(self, transform):
        self.marks = [transform(mark) for mark in self.marks]
        self.undo_states = [[transform(mark) for mark in state] for state in self.undo_states]
        self.redo_states = [[transform(mark) for mark in state] for state in self.redo_states]
        self.redo_marks = self.clone(self.redo_states[-1]) if self.redo_states else []

    def edit(self, index, **changes):
        if 0 <= index < len(self.marks):
            self.checkpoint()
            self.marks[index] = replace(self.marks[index], **changes)

    def delete(self, index):
        if 0 <= index < len(self.marks):
            self.checkpoint()
            self.marks.pop(index)

    def render(self, draft=None):
        result = self.original.copy()
        for mark in [*self.marks, *([draft] if draft else [])]:
            self.paint_mark(result, mark)
        return result

    def paint_mark(self, image, mark):
        if not mark.points:
            return
        first, last = mark.points[0], mark.points[-1]
        rect = QRectF(first, last).normalized()
        if mark.kind in ("mosaic", "blur"):
            stroke_path = None
            if mark.area_shape == "stroke":
                from PySide6.QtGui import QPainterPathStroker
                path = QPainterPath(first)
                for point in mark.points[1:]: path.lineTo(point)
                stroker = QPainterPathStroker()
                stroker.setWidth(max(12, mark.width*5))
                stroker.setCapStyle(Qt.RoundCap)
                stroke_path = stroker.createStroke(path)
                rect = stroke_path.boundingRect()
            area = rect.toAlignedRect().intersected(image.rect())
            if area.width() < 2 or area.height() < 2:
                return
            crop = image.copy(area)
            if mark.kind == "mosaic":
                block = max(2, mark.intensity if mark.intensity is not None else max(6, mark.width*3))
                crop = crop.scaled(max(1, area.width()//block), max(1, area.height()//block),
                                   Qt.IgnoreAspectRatio, Qt.FastTransformation)
                crop = crop.scaled(area.size(), Qt.IgnoreAspectRatio, Qt.FastTransformation)
            else:
                rgba = crop.convertToFormat(QImage.Format_RGBA8888)
                pixels = Image.frombytes("RGBA", (rgba.width(), rgba.height()),
                                         bytes(rgba.constBits()), "raw", "RGBA", rgba.bytesPerLine())
                pixels = pixels.filter(ImageFilter.GaussianBlur(max(1, mark.intensity if mark.intensity is not None else max(4, mark.width*2))))
                crop = QImage(pixels.tobytes(), pixels.width, pixels.height, pixels.width*4,
                              QImage.Format_RGBA8888).copy()
            painter = QPainter(image)
            if mark.area_shape == "ellipse":
                path = QPainterPath()
                path.addEllipse(rect)
                painter.setClipPath(path)
            elif stroke_path is not None: painter.setClipPath(stroke_path)
            painter.drawImage(area, crop)
            painter.end()
            return
        painter = QPainter(image)
        painter.setRenderHint(QPainter.Antialiasing)
        color = QColor(mark.color)
        width = mark.width
        if mark.kind == "marker":
            color.setAlpha(round(color.alpha()/3))
            width *= 5
        painter.setPen(QPen(color, width, {"solid": Qt.SolidLine, "dash": Qt.DashLine,
                                         "dot": Qt.DotLine}.get(mark.dash, Qt.SolidLine), Qt.RoundCap, Qt.RoundJoin))
        painter.setBrush(Qt.NoBrush)
        if mark.rotation and mark.kind not in ("line", "arrow", "polyline", "text"):
            painter.translate(rect.center())
            painter.rotate(mark.rotation)
            painter.translate(-rect.center())
        if mark.kind == "rect":
            painter.drawRoundedRect(rect, mark.radius, mark.radius)
        elif mark.kind == "ellipse":
            painter.drawEllipse(rect)
        elif mark.kind in ("line", "arrow"):
            if mark.kind == "arrow" and mark.arrow_head in REFERENCE_ARROW_HEADS:
                paint_reference_arrow(painter, mark)
            else:
                painter.drawLine(first, last)
            if mark.kind == "arrow" and mark.arrow_head not in REFERENCE_ARROW_HEADS and first != last:
                for start, end in ([(first, last)] if mark.arrow_mode == "forward" else
                                   [(last, first)] if mark.arrow_mode == "reverse" else [(first, last), (last, first)]):
                    angle = math.atan2(end.y()-start.y(), end.x()-start.x())
                    size = max(12, width*4)
                    left = end-QPointF(math.cos(angle-.48)*size, math.sin(angle-.48)*size)
                    right = end-QPointF(math.cos(angle+.48)*size, math.sin(angle+.48)*size)
                    if mark.arrow_head == "open":
                        painter.drawPolyline(QPolygonF([left, end, right]))
                    elif mark.arrow_head == "circle":
                        painter.setBrush(color)
                        painter.drawEllipse(end, width*2, width*2)
                    elif mark.arrow_head == "diamond":
                        painter.setBrush(Qt.NoBrush)
                        back = end-QPointF(math.cos(angle)*size*1.5, math.sin(angle)*size*1.5)
                        painter.drawPolygon(QPolygonF([end, left, back, right]))
                    else:
                        painter.setBrush(color)
                        painter.drawPolygon(QPolygonF([end, left, right]))
        elif mark.kind == "text":
            box, font = text_rect(mark)
            painter.setFont(font)
            painter.translate(first)
            painter.rotate(mark.rotation)
            painter.fillRect(box.adjusted(-4, -2, 4, 2), QColor(mark.text_background))
            metrics = QFontMetricsF(font)
            path = QPainterPath()
            for i, line in enumerate(mark.text.split("\n")):
                path.addText(QPointF(0, metrics.ascent()+i*metrics.lineSpacing()), font, line)
            painter.setPen(QPen(QColor(mark.text_outline), mark.outline_width*2) if mark.outline_width else Qt.NoPen)
            painter.setBrush(color)
            painter.drawPath(path)
        elif mark.kind == "number":
            radius = max(12, mark.font_size*.65)
            painter.setBrush(color)
            painter.drawEllipse(first, radius, radius)
            painter.setPen(Qt.white if color.lightness() < 160 else Qt.black)
            font = QFont("Microsoft YaHei UI")
            font.setPixelSize(mark.font_size)
            font.setBold(True)
            painter.setFont(font)
            painter.drawText(QRectF(first.x()-radius, first.y()-radius, radius*2, radius*2),
                             Qt.AlignCenter, str(mark.number))
        elif mark.kind == "magnify":
            radius = max(16, math.hypot(last.x()-first.x(), last.y()-first.y()))
            target = QRectF(first.x()-radius, first.y()-radius, radius*2, radius*2)
            source = QRectF(first.x()-radius/mark.magnification, first.y()-radius/mark.magnification,
                            radius*2/mark.magnification, radius*2/mark.magnification)
            path = QPainterPath()
            path.addEllipse(target)
            painter.save()
            painter.setClipPath(path)
            painter.drawImage(target, self.original, source)
            painter.restore()
            painter.drawEllipse(target)
        elif mark.kind == "eraser":
            # Restore original pixels under the stroke, removing preceding annotations.
            path = QPainterPath(first)
            for point in mark.points[1:]:
                path.lineTo(point)
            from PySide6.QtGui import QPainterPathStroker
            stroker = QPainterPathStroker()
            stroker.setWidth(max(12, width*5))
            stroker.setCapStyle(Qt.RoundCap)
            if mark.area_shape in ("rect", "ellipse"):
                path = QPainterPath()
                path.addEllipse(rect) if mark.area_shape == "ellipse" else path.addRect(rect)
            else:
                path = stroker.createStroke(path)
            painter.setClipPath(path)
            painter.setCompositionMode(QPainter.CompositionMode_Source)
            painter.drawImage(0, 0, self.original)
        elif mark.kind == "marker" and mark.area_shape in ("rect", "ellipse"):
            painter.setPen(Qt.NoPen)
            painter.setBrush(color)
            painter.drawEllipse(rect) if mark.area_shape == "ellipse" else painter.drawRect(rect)
        else:
            path = QPainterPath(first)
            for point in mark.points[1:]:
                path.lineTo(point)
            if len(mark.points) == 1:
                painter.drawPoint(first)
            else:
                painter.drawPath(path)
        painter.end()


class AnnotationSession:
    """Drawing state shared by the desktop selector and the image editor."""

    def __init__(self, document, on_update=lambda: None, on_changed=lambda: None, recent_only=False):
        self.document = document
        self.tool = "arrow"
        self.color = QColor(DEFAULT_ANNOTATION_COLOR)
        self.pen_width = 4
        self.font_size = 22
        self.rotation = 0
        self.dash = "solid"
        self.radius = 0
        self.arrow_mode = "forward"
        self.arrow_head = "filled"
        self.area_shape = "stroke"
        self.text_background = "#00000000"
        self.text_outline = "#00000000"
        self.outline_width = 0
        self.number = 1
        self.magnification = 2
        self.font_family = "Microsoft YaHei UI"
        self.bold = False
        self.italic = False
        self.intensity = 12
        self.polyline_hover = None
        self.draft = None
        self.cached = self.document.render()
        self.on_update = on_update
        self.on_changed = on_changed
        self.options = {}
        self.on_preferences_changed = lambda: None
        self.selected_index = None
        self.edit_origin = None
        self.edit_original = None
        self.edit_mode = "move"
        self.edit_checkpoint = False
        self.edit_handle = ""
        self.recent_only = recent_only
        self.confirm_click = False
        self.suppress_click = False

    def select_tool(self, tool):
        pending = self.selected_index if self.recent_only and tool == "objects" else None
        if self.tool != "select":
            self.options[self.tool] = self.parameters()
        self.tool = tool
        defaults = {"color": DEFAULT_ANNOTATION_COLOR, "pen_width": 4, "font_size": 22, "rotation": 0,
                    "dash": "solid", "radius": 0, "arrow_mode": "forward", "arrow_head": "filled",
                    "area_shape": "rect" if tool in ("mosaic", "blur") else "stroke",
                    "text_background": "#00000000", "text_outline": "#00000000", "outline_width": 0,
                    "number": 1, "magnification": 2, "font_family": "Microsoft YaHei UI",
                    "bold": False, "italic": False, "intensity": 8 if tool == "blur" else 12}
        values = {**defaults, **self.options.get(tool, {})}
        # Color follows the latest choice across tools; their other parameters
        # remain independent.
        values["color"] = QColor(self.color)
        for name, value in values.items():
            setattr(self, name, QColor(value) if name == "color" else value)
        self.selected_index = pending
        self.on_preferences_changed()

    def restore_preferences(self, config):
        # The constructor's default parameters are not user choices and must
        # never overwrite the tool preferences we are about to load.
        self.tool = "select"
        self.options = dict(config.get("annotation_options", {}))
        legacy = self.options.get(config.get("annotation_last_tool", "arrow"), {})
        self.color = QColor(config.get("annotation_color", legacy.get("color", DEFAULT_ANNOTATION_COLOR)))

    def save_preferences(self, config):
        if self.tool not in ("select", "objects"):
            self.options[self.tool] = self.parameters()
            config["annotation_last_tool"] = self.tool
        config["annotation_options"] = dict(self.options)
        config["annotation_color"] = self.color.name(QColor.HexArgb)

    def parameters(self):
        return {"color": self.color.name(QColor.HexArgb), "pen_width": self.pen_width,
                "font_size": self.font_size, "rotation": self.rotation,
                **{name: getattr(self, name) for name in ("dash", "radius", "arrow_mode", "arrow_head", "area_shape",
                   "text_background", "text_outline", "outline_width", "number", "magnification",
                   "font_family", "bold", "italic", "intensity")}}

    def new_mark(self, kind, points, **values):
        options = self.parameters()
        options["color"] = QColor(self.color)
        options["width"] = options.pop("pen_width")
        options.update(values)
        return Mark(kind, points, **options)

    def adjust_width(self, steps):
        name = "font_size" if self.tool in ("text", "number") else "pen_width"
        low, high = (10, 120) if name == "font_size" else (1, 40)
        self.set_parameter(name, max(low, min(high, getattr(self, name)+steps)))

    def set_parameter(self, name, value):
        setattr(self, name, value)
        mark_name = "width" if name == "pen_width" else name
        if self.draft:
            self.draft = replace(self.draft, **{mark_name: value})
        else:
            self.change_selected(**{mark_name: value})
        self.on_update()

        self.on_preferences_changed()

    def deselect(self):
        self.selected_index = None
        self.on_update()

    def refresh(self):
        self.cached = self.document.render()
        self.on_update()
        self.on_changed()

    def begin_stroke(self, point):
        if self.tool == "objects":
            return
        if self.tool == "polyline" and self.draft:
            self.draft.points.append(point)
            self.on_update()
            return
        self.suppress_click = self.recent_only and (self.confirm_click or self.selected_index is not None)
        self.confirm_click = False
        self.draft = self.new_mark(self.tool, [point])
        self.selected_index = None
        self.on_update()

    def move_stroke(self, point, modifiers=Qt.NoModifier):
        if not self.draft:
            return
        if self.tool == "polyline":
            self.polyline_hover = point
            self.on_update()
            return
        if modifiers & Qt.ShiftModifier:
            first = self.draft.points[0]
            dx, dy = point.x()-first.x(), point.y()-first.y()
            if self.tool in ("rect", "ellipse"):
                size = max(abs(dx), abs(dy))
                point = first+QPointF(math.copysign(size, dx), math.copysign(size, dy))
            elif self.tool in ("arrow", "line", "pen", "marker", "eraser"):
                angle = round(math.atan2(dy, dx)/(math.pi/4))*(math.pi/4)
                length = math.hypot(dx, dy)
                point = first+QPointF(math.cos(angle)*length, math.sin(angle)*length)
        if self.tool in ("pen", "marker", "eraser", "mosaic", "blur") and self.area_shape == "stroke" and not modifiers & Qt.ShiftModifier:
            self.draft.points.append(point)
        else:
            self.draft.points = [self.draft.points[0], point]
        self.on_update()

    def end_stroke(self, point, modifiers=Qt.NoModifier):
        if self.tool == "polyline":
            if self.draft and len(self.draft.points) == 1 and (point-self.draft.points[0]).manhattanLength() > 3:
                self.draft.points.append(point)
                self.finish_polyline()
            return
        if self.draft:
            self.move_stroke(point, modifiers)
            distance = max((p-self.draft.points[0]).manhattanLength() for p in self.draft.points)
            if distance < 2 and (self.tool in ("rect", "ellipse", "line", "arrow", "magnify")
                                 or self.suppress_click and self.tool != "number"):
                self.cancel_draft()
                return
            self.document.add(self.draft)
            self.selected_index = len(self.document.marks)-1
            if self.tool == "number":
                self.number += 1
            self.draft = None
            self.refresh()

    def finish_polyline(self):
        if self.draft and len(self.draft.points) >= 2:
            self.document.add(self.draft)
            self.selected_index = len(self.document.marks)-1
        self.draft = None
        self.polyline_hover = None
        self.refresh()

    def cancel_draft(self):
        self.draft = None
        self.polyline_hover = None
        self.on_update()

    def add_text(self, point, text):
        if text.strip():
            self.document.add(self.new_mark("text", [point], text=text))
            self.selected_index = len(self.document.marks)-1
            self.refresh()

    def preview(self):
        draft = self.draft
        if draft and self.tool == "polyline" and self.polyline_hover:
            from dataclasses import replace
            draft = replace(draft, points=[*draft.points, self.polyline_hover])
        return self.document.render(draft) if draft else self.cached

    @staticmethod
    def bounds(mark):
        if mark.kind == "arrow" and mark.arrow_head in REFERENCE_ARROW_HEADS:
            margin = arrow_outline_width(mark)/2 if mark.arrow_head == "hollow" else 0
            return arrow_path(mark).boundingRect().adjusted(-margin, -margin, margin, margin)
        if mark.kind == "text":
            box, _ = text_rect(mark)
            transform = QTransform().translate(mark.points[0].x(), mark.points[0].y()).rotate(mark.rotation)
            return transform.mapRect(box)
        if mark.kind in ("number", "magnify"):
            first, last = mark.points[0], mark.points[-1]
            radius = max(12, mark.font_size*.65) if mark.kind == "number" else max(16, math.hypot(last.x()-first.x(), last.y()-first.y()))
            return QRectF(first.x()-radius, first.y()-radius, radius*2, radius*2)
        xs, ys = [p.x() for p in mark.points], [p.y() for p in mark.points]
        bounds = QRectF(min(xs), min(ys), max(1, max(xs)-min(xs)), max(1, max(ys)-min(ys))).adjusted(-6, -6, 6, 6)
        if mark.rotation and mark.kind not in ("line", "arrow", "polyline"):
            center = QRectF(mark.points[0], mark.points[-1]).normalized().center()
            transform = QTransform().translate(center.x(), center.y()).rotate(mark.rotation).translate(-center.x(), -center.y())
            bounds = transform.mapRect(bounds)
        return bounds

    @classmethod
    def painted_hit(cls, mark, point, tolerance=3):
        from PySide6.QtGui import QPainterPathStroker
        if not mark.points: return False
        if mark.kind == "arrow" and mark.arrow_head in REFERENCE_ARROW_HEADS:
            outline = arrow_path(mark)
            stroke = QPainterPathStroker()
            stroke.setJoinStyle(Qt.RoundJoin)
            stroke.setCapStyle(Qt.RoundCap)
            stroke.setWidth(arrow_outline_width(mark) if mark.arrow_head == "hollow" else 0)
            if mark.arrow_head == "hollow":
                stroke.setDashPattern({"solid": Qt.SolidLine, "dash": Qt.DashLine, "dot": Qt.DotLine}.get(mark.dash, Qt.SolidLine))
            painted = stroke.createStroke(outline) if mark.arrow_head == "hollow" else outline
            if painted.contains(point): return True
            if mark.dash != "solid": return False
            if mark.arrow_head == "hollow" and outline.contains(point): return False
            if tolerance:
                stroke.setWidth(tolerance*2)
                return stroke.createStroke(painted).contains(point)
            return False
        if mark.kind == "text":
            return cls.bounds(mark).contains(point)
        if mark.kind in ("number", "magnify"):
            box = cls.bounds(mark)
            delta = point-box.center()
            return math.hypot(delta.x(), delta.y()) <= box.width()/2+tolerance
        if mark.rotation and mark.kind not in ("line", "arrow", "polyline"):
            inverse, _ = cls.shape_transform(mark).inverted()
            point = inverse.map(point)
        r = QRectF(mark.points[0], mark.points[-1]).normalized()
        path = QPainterPath()
        if mark.kind == "rect": path.addRoundedRect(r, mark.radius, mark.radius)
        elif mark.kind == "ellipse": path.addEllipse(r)
        elif mark.kind in ("marker", "mosaic", "blur", "eraser") and mark.area_shape in ("rect", "ellipse"):
            path.addEllipse(r) if mark.area_shape == "ellipse" else path.addRect(r)
            return path.contains(point)
        else:
            path.moveTo(mark.points[0])
            for endpoint in mark.points[1:]: path.lineTo(endpoint)
            if len(mark.points) == 1:
                radius = mark.width/2*(5 if mark.kind in ("marker", "mosaic", "blur", "eraser") else 1)
                return (point-mark.points[0]).manhattanLength() <= radius+tolerance
        stroke = QPainterPathStroker()
        width = mark.width*(5 if mark.kind in ("marker", "mosaic", "blur", "eraser") else 1)
        stroke.setWidth(max(12, width) if mark.kind in ("mosaic", "blur", "eraser") else width)
        stroke.setCapStyle(Qt.RoundCap)
        stroke.setJoinStyle(Qt.RoundJoin)
        if mark.kind not in ("mosaic", "blur", "eraser"):
            stroke.setDashPattern({"solid": Qt.SolidLine, "dash": Qt.DashLine, "dot": Qt.DotLine}.get(mark.dash, Qt.SolidLine))
        hit_margin = QPainterPathStroker()
        hit_margin.setWidth(tolerance*2)
        def near_path(shape):
            painted = stroke.createStroke(shape)
            if mark.dash != "solid" and mark.kind not in ("mosaic", "blur", "eraser"):
                return painted.contains(point)
            return painted.contains(point) or hit_margin.createStroke(painted).contains(point)
        if near_path(path): return True
        if mark.kind == "arrow":
            first, last = mark.points[0], mark.points[-1]
            for start, end in ([(first, last)] if mark.arrow_mode == "forward" else
                               [(last, first)] if mark.arrow_mode == "reverse" else [(first, last), (last, first)]):
                angle = math.atan2(end.y()-start.y(), end.x()-start.x())
                size = max(12, mark.width*4)
                left = end-QPointF(math.cos(angle-.48)*size, math.sin(angle-.48)*size)
                right = end-QPointF(math.cos(angle+.48)*size, math.sin(angle+.48)*size)
                head = QPainterPath(left)
                head.lineTo(end)
                head.lineTo(right)
                if mark.arrow_head == "circle": head.addEllipse(end, mark.width*2, mark.width*2)
                elif mark.arrow_head == "diamond":
                    head.lineTo(end-QPointF(math.cos(angle)*size*1.5, math.sin(angle)*size*1.5))
                    head.closeSubpath()
                elif mark.arrow_head != "open": head.closeSubpath()
                if near_path(head) or mark.arrow_head in ("filled", "circle") and head.contains(point):
                    return True
        return False

    def hit_test(self, point, tolerance=3):
        indexes = ([self.selected_index] if self.selected_index is not None else []) if self.recent_only else range(len(self.document.marks)-1, -1, -1)
        for index in indexes:
            if not 0 <= index < len(self.document.marks): continue
            mark = self.document.marks[index]
            if self.tool != "objects" and mark.kind != self.tool:
                continue
            if self.painted_hit(mark, point, tolerance):
                return index
        return None

    def edit_target(self, point, tolerance=8):
        if self.draft: return ""
        if self.selected_index is not None and 0 <= self.selected_index < len(self.document.marks):
            mark = self.document.marks[self.selected_index]
            handle = next((name for name, pos in self.control_points(mark).items()
                           if (point-pos).manhattanLength() < tolerance), "")
            if handle: return handle
        return "move" if self.hit_test(point, max(1, tolerance/3)) is not None else ""

    def begin_edit(self, point, clone=False, tolerance=8):
        if not clone and self.selected_index is not None and self.selected_index < len(self.document.marks):
            mark = self.document.marks[self.selected_index]
            handle = next((name for name, pos in self.control_points(mark).items()
                           if (point-pos).manhattanLength() < tolerance), "")
            mode = ("endpoint" if handle in ("start", "end") else
                    "rotate" if handle == "rotate" else "resize" if handle else "")
            if mode:
                self.edit_origin, self.edit_original = point, self.document.clone([mark])[0]
                self.edit_mode, self.edit_checkpoint = mode, False
                self.edit_handle = handle
                return True
        index = self.hit_test(point, max(1, tolerance/3))
        if index is None:
            self.confirm_click = self.recent_only and self.selected_index is not None
            self.selected_index = None
            self.on_update()
            return False
        self.edit_checkpoint = clone
        if clone:
            self.document.checkpoint()
            self.document.marks.append(self.document.clone([self.document.marks[index]])[0])
            index = len(self.document.marks)-1
        self.selected_index = index
        self.confirm_click = False
        self.edit_origin = point
        self.edit_original = self.document.clone([self.document.marks[index]])[0]
        self.edit_mode = "move"
        for name, value in self.edit_original.to_dict().items():
            if name in self.parameters(): setattr(self, name, QColor(value) if name == "color" else value)
        self.pen_width = self.edit_original.width
        self.on_update()
        return True

    def move_edit(self, point, modifiers=Qt.NoModifier):
        if self.edit_origin is not None:
            delta = point-self.edit_origin
            if delta.manhattanLength() < 1 and not self.edit_checkpoint: return
            if not self.edit_checkpoint:
                self.document.checkpoint()
                self.edit_checkpoint = True
            mark = self.edit_original
            if self.edit_mode == "endpoint":
                index = 0 if self.edit_handle == "start" else -1
                points = [QPointF(p) for p in mark.points]
                endpoint = points[index]+delta
                if modifiers & Qt.ShiftModifier:
                    anchor = points[-1 if index == 0 else 0]
                    vector = endpoint-anchor
                    angle = round(math.atan2(vector.y(), vector.x())/(math.pi/4))*(math.pi/4)
                    length = math.hypot(vector.x(), vector.y())
                    endpoint = anchor+QPointF(math.cos(angle)*length, math.sin(angle)*length)
                points[index] = endpoint
                changed = replace(mark, points=points)
            elif self.edit_mode == "rotate":
                center = self.bounds(mark).center()
                angle = math.degrees(math.atan2(point.y()-center.y(), point.x()-center.x())-
                                     math.atan2(self.edit_origin.y()-center.y(), self.edit_origin.x()-center.x()))
                changed = replace(mark, rotation=round(mark.rotation+angle))
            elif self.edit_mode == "resize":
                if mark.kind in ("rect", "ellipse"):
                    changed = self.resize_shape(mark, point, modifiers)
                else:
                    bounds = self.bounds(mark)
                    factor = max(.1, min(10, (point-bounds.center()).manhattanLength()/
                                         max(1, (self.edit_origin-bounds.center()).manhattanLength())))
                    changed = replace(mark, font_size=max(10, min(120, round(mark.font_size*factor))),
                                      rotation=0 if modifiers & Qt.ShiftModifier else mark.rotation) if mark.kind == "text" else replace(
                        mark, points=[bounds.center()+(p-bounds.center())*factor for p in mark.points])
            else:
                changed = replace(mark, points=[p+delta for p in mark.points])
            self.document.marks[self.selected_index] = changed
            self.cached = self.document.render()
            self.on_update()

    def end_edit(self):
        self.edit_origin = None
        self.edit_original = None
        self.refresh()

    def change_selected(self, **changes):
        changes = {name: value for name, value in changes.items() if name in Mark.__dataclass_fields__}
        if self.selected_index is not None and self.selected_index < len(self.document.marks):
            if not changes or all(getattr(self.document.marks[self.selected_index], name) == value for name, value in changes.items()):
                return
            self.document.edit(self.selected_index, **changes)
            self.refresh()

    def delete_selected(self):
        if self.selected_index is not None:
            self.document.delete(self.selected_index)
            self.selected_index = None
            self.refresh()

    def copy_selected(self):
        if self.selected_index is None or self.selected_index >= len(self.document.marks): return False
        mime = QMimeData()
        mime.setData("application/x-floatshot-mark", json.dumps(self.document.marks[self.selected_index].to_dict()).encode())
        QApplication.clipboard().setMimeData(mime)
        return True

    def paste_selected(self):
        mime = QApplication.clipboard().mimeData()
        if not mime.hasFormat("application/x-floatshot-mark"): return False
        try:
            mark = Mark.from_dict(json.loads(bytes(mime.data("application/x-floatshot-mark"))))
        except (ValueError, TypeError, KeyError): return False
        mark.points = [p+QPointF(16, 16) for p in mark.points]
        self.document.add(mark)
        self.selected_index = len(self.document.marks)-1
        self.refresh()
        return True

    @staticmethod
    def shape_transform(mark):
        center = QRectF(mark.points[0], mark.points[-1]).normalized().center()
        return QTransform().translate(center.x(), center.y()).rotate(mark.rotation).translate(-center.x(), -center.y())

    @classmethod
    def control_points(cls, mark):
        if mark.kind in ("line", "arrow"):
            return {"start": mark.points[0], "end": mark.points[-1]}
        if mark.kind in ("rect", "ellipse"):
            r = QRectF(mark.points[0], mark.points[-1]).normalized()
            positions = {"nw": r.topLeft(), "n": QPointF(r.center().x(), r.top()), "ne": r.topRight(),
                         "w": QPointF(r.left(), r.center().y()), "e": QPointF(r.right(), r.center().y()),
                         "sw": r.bottomLeft(), "s": QPointF(r.center().x(), r.bottom()), "se": r.bottomRight(),
                         "rotate": QPointF(r.center().x(), r.top()-20)}
            transform = cls.shape_transform(mark)
            return {name: transform.map(pos) for name, pos in positions.items()}
        r = cls.bounds(mark)
        points = {"nw": r.topLeft(), "ne": r.topRight(), "sw": r.bottomLeft(), "se": r.bottomRight()}
        if mark.kind not in ("line", "arrow", "polyline"):
            points["rotate"] = r.topRight()+QPointF(0, -20)
        return points

    def resize_shape(self, mark, point, modifiers):
        r = QRectF(mark.points[0], mark.points[-1]).normalized()
        transform = self.shape_transform(mark)
        inverse, _ = transform.inverted()
        delta = inverse.map(point)-inverse.map(self.edit_origin)
        handle = self.edit_handle
        if "w" in handle: r.setLeft(min(r.right()-2, r.left()+delta.x()))
        if "e" in handle: r.setRight(max(r.left()+2, r.right()+delta.x()))
        if "n" in handle: r.setTop(min(r.bottom()-2, r.top()+delta.y()))
        if "s" in handle: r.setBottom(max(r.top()+2, r.bottom()+delta.y()))
        if modifiers & Qt.ShiftModifier and len(handle) == 2:
            original = QRectF(mark.points[0], mark.points[-1]).normalized()
            height = r.width()*original.height()/max(1, original.width())
            if "n" in handle: r.setTop(r.bottom()-height)
            else: r.setBottom(r.top()+height)
        offset = transform.map(r.center())-r.center()
        return replace(mark, points=[r.topLeft()+offset, r.bottomRight()+offset])

    def paint_selection(self, painter):
        if self.selected_index is None or self.selected_index >= len(self.document.marks): return
        mark = self.document.marks[self.selected_index]
        if mark.kind in ("line", "arrow"):
            painter.setBrush(Qt.NoBrush)
            for pos in self.control_points(mark).values():
                painter.setPen(QPen(QColor("#888888"), 1))
                painter.drawRect(QRectF(pos.x()-3, pos.y()-3, 6, 6))
                painter.setPen(QPen(Qt.white, 1))
                painter.drawRect(QRectF(pos.x()-2, pos.y()-2, 4, 4))
            return
        bounds = self.bounds(mark)
        painter.setPen(QPen(QColor("#1688ef"), 1, Qt.DashLine))
        painter.setBrush(Qt.NoBrush)
        if mark.kind in ("rect", "ellipse"):
            transform = self.shape_transform(mark)
            r = QRectF(mark.points[0], mark.points[-1]).normalized()
            painter.drawPolygon(transform.map(QPolygonF([r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()])))
        elif mark.kind not in ("line", "arrow"):
            painter.drawRect(bounds)
        points = self.control_points(mark)
        if "rotate" in points:
            painter.drawLine(points.get("n", bounds.topRight()), points["rotate"])
        painter.setBrush(Qt.white)
        painter.setPen(QPen(QColor("#1688ef"), 1))
        for pos in points.values():
            painter.drawEllipse(pos, 4, 4)

    def brush_diameter(self):
        if self.tool in ("pen", "line", "arrow", "polyline"):
            return self.pen_width
        if self.tool == "marker" and self.area_shape == "stroke":
            return self.pen_width*5
        if self.tool in ("eraser", "mosaic", "blur") and self.area_shape == "stroke":
            return max(12, self.pen_width*5)
        return None

    def paint_brush(self, painter, point, scale=1):
        diameter = self.brush_diameter()
        if diameter is None or self.draft or self.edit_origin is not None:
            return False
        painter.save()
        painter.setRenderHint(QPainter.Antialiasing)
        color = QColor(self.color) if self.tool not in ("mosaic", "blur", "eraser") else QColor("#1688ef")
        radius = max(.5, diameter*scale/2)
        painter.setPen(QPen(color, 1))
        painter.setBrush(color if self.tool not in ("mosaic", "blur", "eraser") else Qt.NoBrush)
        painter.drawEllipse(QPointF(point), radius, radius)
        painter.setPen(QPen(color, 1))
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            painter.drawLine(QPointF(point)+QPointF(dx*(radius+3), dy*(radius+3)),
                             QPointF(point)+QPointF(dx*(radius+8), dy*(radius+8)))
        painter.restore()
        return True


class ImageCanvas(QWidget):
    changed = Signal()

    def __init__(self, document):
        super().__init__()
        self.document = document
        self.session = AnnotationSession(document, self.update, self.changed.emit)
        self.zoom = 1.0
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMouseTracking(True)
        self.setCursor(Qt.CrossCursor)
        self.fit(1000, 600)

    def fit(self, width, height):
        image = self.session.cached
        self.zoom = min(1.0, width/image.width(), height/image.height())
        self.setFixedSize(max(1, round(image.width()*self.zoom)), max(1, round(image.height()*self.zoom)))
        self.update()

    def refresh(self):
        self.session.refresh()

    def finish_polyline(self):
        self.session.finish_polyline()

    def image_point(self, event):
        point = event.position()/self.zoom
        return QPointF(max(0, min(self.session.cached.width()-1, point.x())),
                       max(0, min(self.session.cached.height()-1, point.y())))

    def mousePressEvent(self, event):
        if event.button() == Qt.RightButton and self.session.draft and self.session.tool == "polyline":
            self.session.finish_polyline()
            return
        if event.button() != Qt.LeftButton:
            return
        self.setFocus()
        point = self.image_point(event)
        if self.session.tool == "text":
            text, accepted = QInputDialog.getMultiLineText(self, "文字标注", "输入文字：")
            if accepted:
                self.session.add_text(point, text)
        else:
            self.session.begin_stroke(point)

    def mouseMoveEvent(self, event):
        self.session.move_stroke(self.image_point(event), event.modifiers())

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.session.end_stroke(self.image_point(event), event.modifiers())

    def keyPressEvent(self, event):
        if self.session.draft and self.session.tool == "polyline" and event.key() in (Qt.Key_Return, Qt.Key_Enter):
            self.finish_polyline()
        elif self.session.draft and event.key() == Qt.Key_Escape:
            self.session.cancel_draft()
        else:
            super().keyPressEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.SmoothPixmapTransform)
        painter.drawImage(self.rect(), self.session.preview())


class EditorDialog(QDialog):
    output = Signal(QImage, str)

    def __init__(self, image, parent=None, pin_edit=False):
        super().__init__(parent)
        self.setWindowTitle("浮光 · 标注图片")
        self.setAttribute(Qt.WA_DeleteOnClose)
        self.document = ImageDocument(image)
        self.canvas = ImageCanvas(self.document)
        self.pin_edit = pin_edit
        self.resize(1120, 780)
        layout = QVBoxLayout(self)
        title = QLabel(f"标注图片    {image.width()} × {image.height()} px")
        title.setStyleSheet("font-size: 18px; font-weight: 600; padding: 8px;")
        layout.addWidget(title)
        tools = QHBoxLayout()
        group = QButtonGroup(self)
        for kind, text in [("rect", "矩形"), ("ellipse", "椭圆"), ("line", "直线"),
                           ("arrow", "箭头"), ("polyline", "折线"), ("pen", "画笔"), ("marker", "高亮"),
                           ("text", "文字"), ("mosaic", "马赛克"), ("blur", "模糊"), ("eraser", "擦除")]:
            tool = QToolButton()
            tool.setText(text)
            tool.setCheckable(True)
            tool.setChecked(kind == "arrow")
            tool.clicked.connect(lambda checked=False, k=kind: setattr(self.canvas.session, "tool", k))
            group.addButton(tool)
            tools.addWidget(tool)
        tools.addStretch()
        layout.addLayout(tools)
        options = QHBoxLayout()
        self.color_button = button("颜色", self.choose_color)
        options.addWidget(self.color_button)
        options.addWidget(QLabel("粗细"))
        width = QSpinBox()
        width.setRange(1, 40)
        width.setValue(4)
        width.valueChanged.connect(lambda value: setattr(self.canvas.session, "pen_width", value))
        options.addWidget(width)
        options.addWidget(QLabel("字号"))
        font_size = QSpinBox()
        font_size.setRange(10, 120)
        font_size.setValue(22)
        font_size.valueChanged.connect(lambda value: setattr(self.canvas.session, "font_size", value))
        options.addWidget(font_size)
        options.addWidget(QLabel("文字角度"))
        rotation = QSpinBox()
        rotation.setRange(-180, 180)
        rotation.setSuffix("°")
        rotation.valueChanged.connect(lambda value: setattr(self.canvas.session, "rotation", value))
        options.addWidget(rotation)
        options.addStretch()
        options.addWidget(button("撤销", self.undo))
        options.addWidget(button("重做", self.redo))
        layout.addLayout(options)
        scroll = QScrollArea()
        scroll.setAlignment(Qt.AlignCenter)
        scroll.setWidget(self.canvas)
        scroll.setStyleSheet("QScrollArea { background: #0a1118; border: 1px solid #32475a; }")
        layout.addWidget(scroll, 1)
        bottom = QHBoxLayout()
        hint = QLabel("Shift 约束形状  ·  Ctrl+Z 撤销  ·  Ctrl+S 保存")
        hint.setObjectName("muted")
        bottom.addWidget(hint)
        bottom.addStretch()
        bottom.addWidget(button("取消", self.close))
        for text, action in (("保存", "save"), ("贴图", "pin"), ("复制", "copy")):
            bottom.addWidget(button(text, lambda checked=False, a=action: self.finish(a), action == "copy"))
        if pin_edit:
            bottom.addWidget(button("应用到贴图", lambda: self.finish("apply"), True))
        layout.addLayout(bottom)
        self.canvas.fit(1060, 535)

    def choose_color(self):
        color = QColorDialog.getColor(self.canvas.session.color, self, "标注颜色", QColorDialog.ShowAlphaChannel)
        if color.isValid():
            self.canvas.session.color = color
            self.color_button.setStyleSheet(f"border: 2px solid {color.name()};")

    def undo(self):
        self.document.undo()
        self.canvas.refresh()

    def redo(self):
        self.document.redo()
        self.canvas.refresh()

    def finish(self, action):
        self.canvas.finish_polyline()
        self.output.emit(self.document.render(), action)

    def keyPressEvent(self, event):
        if event.modifiers() & Qt.ControlModifier:
            if event.key() == Qt.Key_S and event.modifiers() & Qt.ShiftModifier:
                self.finish("quick_save")
                return
            actions = {Qt.Key_Z: self.undo, Qt.Key_Y: self.redo,
                       Qt.Key_C: lambda: self.finish("copy"), Qt.Key_S: lambda: self.finish("save"),
                       Qt.Key_T: lambda: self.finish("pin")}
            if event.key() in actions:
                actions[event.key()]()
                return
        super().keyPressEvent(event)
