from __future__ import annotations

import uuid
import json
from pathlib import Path

from PySide6.QtCore import QPoint, QPointF, QRect, QRectF, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QGuiApplication, QImage, QPainter, QPen, QTransform, QBitmap, QRegion
from PySide6.QtWidgets import (QApplication, QColorDialog, QDialog, QDialogButtonBox, QDoubleSpinBox,
                              QFormLayout, QHBoxLayout, QInputDialog, QListWidget, QMenu, QMessageBox,
                              QSpinBox, QVBoxLayout, QWidget)
from .theme import button

from . import native
from .capture_ui import CaptureToolbar, InlineTextEntry, annotation_shortcut, editing_cursor
from .editor import AnnotationSession, ImageDocument, Mark, text_rect
from .image_tools import PixelLoupe, color_card, format_color


class PinWindow(QWidget):
    FRAME_MARGIN = 12
    BORDER_COLOR = QColor(170, 207, 249)
    modified = Signal()
    requested = Signal(object, str)
    dropped = Signal(QImage)

    def __init__(self, image, key=None, store=None, source_rect=None):
        super().__init__(None, Qt.FramelessWindowHint | Qt.Tool | Qt.WindowStaysOnTopHint)
        self.key = key or uuid.uuid4().hex
        self.image = image.copy()
        self.image.setDevicePixelRatio(1)
        self.scale = 1.0
        self.alpha = 1.0
        self.through = False
        self.state = "visible"
        self.drag_offset = None
        self.resize_start = None
        self.store = store
        self.annotation = None
        self.editing = False
        self.cropping = False
        self.crop_origin = None
        self.crop_rect = QRect()
        self.right_dragged = False
        self.thumbnail_rect = QRect()
        self.gray = False
        self.inverted = False
        self.topmost = True
        self.group = "默认"
        self.selected = False
        self.color_value = ""
        self.background_mode = "暗色棋盘"
        self.pointer = QPoint()
        self.alt_down = False
        self.rgb_color = False
        self.tool = store.config.get("annotation_last_tool", "arrow") if store else "arrow"
        self.toolbar = CaptureToolbar(self)
        self.toolbar.setCursor(Qt.ArrowCursor)
        self.toolbar.setWindowFlags(Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.toolbar.tool_selected.connect(self.choose_tool)
        self.toolbar.action_requested.connect(self.toolbar_action)
        self.toolbar.color_selected.connect(lambda color: self.set_option("color", color))
        self.toolbar.width_changed.connect(lambda value: self.set_option("pen_width", value))
        self.toolbar.font_changed.connect(lambda value: self.set_option("font_size", value))
        self.toolbar.rotation_changed.connect(lambda value: self.set_option("rotation", value))
        self.toolbar.actions["pin"].setToolTip("完成标注 · Space")
        self.text_entry = InlineTextEntry(self)
        self.text_entry.committed.connect(self.commit_text)
        self.text_entry.cancelled.connect(self.cancel_text)
        self.text_point = None
        self.text_edit_index = None
        self.loupe = PixelLoupe(self)
        self.setMouseTracking(True)
        self.setAcceptDrops(True)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setFocusPolicy(Qt.StrongFocus)
        from .import_images import ImageDownloader
        self.downloader = ImageDownloader(self)
        self.downloader.loaded.connect(self.dropped.emit)
        self.downloader.failed.connect(lambda text: self.setToolTip(text))
        self.toolbar.styles_changed.connect(self.set_styles)
        if source_rect is not None and not source_rect.isEmpty():
            self.scale = source_rect.width()/image.width()
            self.resize(source_rect.size()+QSize(self.FRAME_MARGIN*2, self.FRAME_MARGIN*2))
            self.move(source_rect.topLeft()-QPoint(self.FRAME_MARGIN, self.FRAME_MARGIN))
        else:
            screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
            available = screen.availableGeometry()
            self.set_scale(min(1, (available.width()*.7)/image.width(), (available.height()*.7)/image.height()))
            self.move(available.center()-self.rect().center())

    def set_scale(self, value):
        self.scale = max(.05, min(5, value))
        size = self.thumbnail_rect.size() if not self.thumbnail_rect.isEmpty() else self.image.size()
        self.resize(max(32, round(size.width()*self.scale))+self.FRAME_MARGIN*2,
                    max(24, round(size.height()*self.scale))+self.FRAME_MARGIN*2)
        self.update_input_mask()
        self.update()
        self.modified.emit()

    def content_rect(self):
        m = self.FRAME_MARGIN
        return self.rect().adjusted(m, m, -m, -m)

    def content_geometry(self):
        return self.content_rect().translated(self.pos())

    def replace_image(self, image):
        self.image = image.copy()
        self.annotation = None
        self.gray = False
        self.inverted = False
        self.thumbnail_rect = QRect()
        self.set_scale(self.scale)

    def displayed_image(self):
        image = self.annotation.preview() if self.annotation else self.image
        if self.gray:
            import numpy as np
            image = image.convertToFormat(QImage.Format_ARGB32).copy()
            pixels = np.frombuffer(image.bits(), np.uint8).reshape(image.height(), image.bytesPerLine()//4, 4)
            gray = (pixels[:, :, 2]*.299+pixels[:, :, 1]*.587+pixels[:, :, 0]*.114).astype(np.uint8)
            pixels[:, :, :3] = gray[:, :, None]
        if self.inverted:
            image = image.copy()
            image.invertPixels()
        if not self.thumbnail_rect.isEmpty():
            image = image.copy(self.thumbnail_rect)
        return image

    def export_image(self):
        self.commit_pending_text()
        if self.annotation and self.annotation.draft:
            self.annotation.finish_polyline()
        return self.displayed_image()

    def image_point(self, local):
        image = self.displayed_image()
        area = self.content_rect()
        return QPointF(max(0, min(image.width()-1, (local.x()-area.x())*image.width()/area.width())),
                       max(0, min(image.height()-1, (local.y()-area.y())*image.height()/area.height())))

    def start_editing(self):
        if self.cropping:
            self.cropping = False
            self.crop_rect = QRect()
        if self.gray or self.inverted or not self.thumbnail_rect.isEmpty():
            self.replace_image(self.displayed_image())
        if self.annotation is None:
            if self.gray or self.inverted or not self.thumbnail_rect.isEmpty():
                self.replace_image(self.displayed_image())
            self.annotation = AnnotationSession(ImageDocument(self.image), self.update, self.annotation_changed, recent_only=True)
            if self.store: self.annotation.restore_preferences(self.store.config)
            self.annotation.select_tool(self.tool)
        elif self.store:
            self.annotation.restore_preferences(self.store.config)
            self.annotation.select_tool(self.tool)
        self.annotation.on_preferences_changed = self.save_annotation_options
        self.editing = True
        self.clearMask()
        self.tool = self.annotation.tool
        self.toolbar.sync_session(self.annotation)
        self.save_annotation_options()
        self.position_toolbar()
        self.toolbar.show()
        self.setCursor(Qt.CrossCursor)
        self.setFocus()

    def set_styles(self, values):
        if not self.annotation: return
        for name, value in values.items(): self.annotation.set_parameter(name, value)
        self.toolbar.sync_session(self.annotation)
        self.update_text_font()
        self.position_toolbar()
        self.update()

    def end_editing(self):
        self.commit_pending_text()
        if self.annotation and self.annotation.draft:
            self.annotation.finish_polyline()
        if self.annotation: self.annotation.deselect()
        self.save_annotation_options()
        self.editing = False
        self.update_input_mask()
        self.toolbar.hide()
        self.loupe.hide()
        self.update()
        self.modified.emit()

    def save_annotation_options(self):
        if self.annotation and self.store:
            self.annotation.save_preferences(self.store.config)
            self.store.config["annotation_tool_groups"] = dict(self.toolbar.group_choice)
            self.store.save_config()

    def annotation_changed(self):
        if self.annotation:
            self.toolbar.set_history(bool(self.annotation.document.undo_states), bool(self.annotation.document.redo_states))
        self.update_drawing_cursor()
        self.update()
        self.modified.emit()

    def choose_tool(self, tool):
        self.commit_pending_text()
        if self.annotation.draft:
            self.annotation.finish_polyline()
        self.tool = tool
        self.annotation.select_tool(tool)
        self.toolbar.sync_session(self.annotation)
        self.save_annotation_options()
        self.position_toolbar()
        self.update_drawing_cursor()
        self.setFocus()

    def set_option(self, name, value):
        if self.annotation:
            self.annotation.set_parameter(name, value)
            if name == "color": self.toolbar.set_color(value)
            self.update_text_font()
            self.update()

    def toolbar_action(self, action):
        if action in ("pin", "cancel"):
            self.end_editing()
        elif action in ("undo", "redo"):
            getattr(self.annotation.document, action)()
            self.annotation.selected_index = None
            self.annotation.refresh()
        elif action == "more":
            menu = QMenu(self)
            tools = menu.addMenu("其他工具")
            for title, kind in (("移动", "select"), ("自由选择标注", "objects"), ("折线（Enter 完成）", "polyline"), ("序号", "number"), ("放大镜标注", "magnify")):
                tools.addAction(title, lambda checked=False, k=kind: self.choose_tool(k))
            menu.addAction("高级标注参数与色板…", self.toolbar.choose_style)
            menu.addAction("裁剪贴图", lambda: self.requested.emit(self, "crop"))
            menu.addAction("输入缩放比例…", lambda: self.requested.emit(self, "size"))
            menu.addAction("文字识别…", lambda: self.recognize(False))
            menu.addAction("二维码 / 条形码识别…", lambda: self.recognize(True))
            menu.addAction("带边框 / 阴影保存…", lambda: self.requested.emit(self, "save_decorated"))
            menu.exec(self.toolbar.mapToGlobal(QPoint(0, self.toolbar.height())))
        else:
            self.requested.emit(self, action)

    def position_toolbar(self):
        screen = QGuiApplication.screenAt(self.geometry().center()) or QGuiApplication.primaryScreen()
        area = screen.availableGeometry().adjusted(6, 6, -6, -6)
        self.toolbar.fit_width(area.width())
        x = max(area.left(), min(self.geometry().right()-self.toolbar.width(), area.right()-self.toolbar.width()))
        y = self.geometry().bottom()+10
        if y+self.toolbar.height() > area.bottom(): y = self.y()-self.toolbar.height()-10
        self.toolbar.move(x, max(area.top(), y))

    def recognize(self, codes):
        from .recognition import RecognitionDialog
        dialog = RecognitionDialog(self.export_image(), codes, self)
        dialog.exec()
        dialog.deleteLater()

    def start_text(self, point):
        self.commit_pending_text()
        self.text_point = self.image_point(point)
        index = self.annotation.hit_test(self.text_point)
        self.text_edit_index = index
        self.text_entry.edit.setPlainText(self.annotation.document.marks[index].text if index is not None else "")
        self.update_text_font()
        self.text_entry.setFixedWidth(min(310, max(120, self.width()-12)))
        self.text_entry.move(max(4, min(int(point.x()), self.width()-self.text_entry.width()-4)),
                             max(4, min(int(point.y()), self.height()-self.text_entry.height()-4)))
        self.text_entry.show()
        self.text_entry.edit.setFocus()

    def commit_text(self, text):
        if self.text_edit_index is not None:
            self.annotation.document.edit(self.text_edit_index, text=text)
            self.annotation.refresh()
        elif self.text_point is not None:
            self.annotation.add_text(self.text_point, text)
        self.cancel_text()

    def commit_pending_text(self):
        if self.text_entry.isVisible(): self.commit_text(self.text_entry.edit.toPlainText())

    def cancel_text(self):
        self.text_entry.hide()
        self.text_point = None
        self.text_edit_index = None
        self.setFocus()

    def update_text_font(self):
        if self.annotation:
            _, font = text_rect(self.annotation.new_mark("text", []))
            self.text_entry.edit.setFont(font)

    def update_drawing_cursor(self):
        if self.editing and self.annotation:
            target = self.annotation.edit_target(self.image_point(self.pointer), 8/max(.05, self.scale))
            if self.annotation.edit_origin is not None:
                target = self.annotation.edit_handle if self.annotation.edit_mode != "move" else "move"
            cursor = editing_cursor(target)
            if cursor is not None:
                self.setCursor(cursor)
                return
        self.setCursor(Qt.BlankCursor if self.editing and self.annotation and
                       self.annotation.brush_diameter() is not None else Qt.CrossCursor)

    def moveEvent(self, event):
        super().moveEvent(event)
        if self.editing: self.position_toolbar()

    def hideEvent(self, event):
        self.toolbar.hide()
        self.loupe.hide()
        super().hideEvent(event)

    def showEvent(self, event):
        super().showEvent(event)
        if self.editing:
            self.position_toolbar()
            self.toolbar.show()

    def refresh_loupe(self):
        if self.alt_down:
            image = self.displayed_image()
            point = self.image_point(self.pointer).toPoint()
            point.setX(min(image.width()-1, point.x()))
            point.setY(min(image.height()-1, point.y()))
            self.loupe.present(image, point, self.mapToGlobal(self.pointer), self.rgb_color)
        else:
            self.loupe.hide()

    def set_alpha(self, value):
        self.alpha = max(.12, min(1.0, value))
        self.setWindowOpacity(self.alpha)
        self.modified.emit()

    def set_through(self, value):
        visible = self.isVisible()
        self.through = value
        self.setWindowFlag(Qt.WindowTransparentForInput, value)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, value)
        if visible:
            self.show()
        self.modified.emit()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setCompositionMode(QPainter.CompositionMode_Source)
        painter.fillRect(self.rect(), Qt.transparent)
        painter.setCompositionMode(QPainter.CompositionMode_SourceOver)
        area = self.content_rect()
        frame = QRectF(area).adjusted(-.5, -.5, .5, .5)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setBrush(Qt.NoBrush)
        # Sampled from the reference: a pale one-pixel border and blue shadow
        # that fades outward symmetrically, without an opaque window gutter.
        for distance, alpha in reversed(list(enumerate((97, 68, 45, 30, 19, 12, 6, 4, 2), 1))):
            painter.setPen(QPen(QColor(96, 166, 244, alpha), 1))
            painter.drawRoundedRect(frame.adjusted(-distance, -distance, distance, distance),
                                    distance+1, distance+1)
        painter.save()
        painter.setClipRect(area)
        if self.background_mode in ("暗色棋盘", "亮色棋盘"):
            dark = self.background_mode == "暗色棋盘"
            for x in range(area.left(), area.right()+1, 16):
                for y in range(area.top(), area.bottom()+1, 16):
                    value = (35 if ((x-area.left())//16+(y-area.top())//16) % 2 else 48) if dark else (225 if ((x-area.left())//16+(y-area.top())//16) % 2 else 245)
                    painter.fillRect(QRect(x, y, 16, 16), QColor(value, value, value))
        painter.setRenderHint(QPainter.SmoothPixmapTransform)
        painter.drawImage(area, self.displayed_image())
        painter.restore()
        painter.setPen(QPen(QColor("#69aaf8") if self.selected else self.BORDER_COLOR, 1))
        painter.drawRect(frame)
        if self.cropping and not self.crop_rect.isEmpty():
            painter.setPen(QPen(QColor("#48d6be"), 2))
            painter.drawRect(self.crop_rect)
        if self.editing and self.annotation:
            painter.save()
            painter.translate(area.topLeft())
            painter.scale(area.width()/self.image.width(), area.height()/self.image.height())
            self.annotation.paint_selection(painter)
            painter.restore()
            if area.contains(self.pointer) and self.childAt(self.pointer) is None and not self.text_entry.isVisible():
                if not self.annotation.edit_target(self.image_point(self.pointer), 8/max(.05, self.scale)):
                    painter.save()
                    painter.setClipRect(area)
                    self.annotation.paint_brush(painter, self.pointer, area.width()/self.image.width())
                    painter.restore()

    def update_input_mask(self):
        if self.background_mode == "透明" and not self.editing:
            area = self.content_rect()
            image = self.displayed_image().scaled(area.size())
            region = QRegion(QBitmap.fromImage(image.createAlphaMask())).translated(area.topLeft())
            # Preserve the external frame while transparent image pixels still
            # let mouse input reach the desktop underneath.
            region |= QRegion(area.adjusted(-10, -10, 10, 10))-QRegion(area)
            self.setMask(region)
        else: self.clearMask()

    def mousePressEvent(self, event):
        self.pointer = event.position().toPoint()
        if event.button() == Qt.MiddleButton:
            self.thumbnail_rect = QRect()
            self.set_scale(1)
            self.set_alpha(1)
            return
        if self.cropping or (event.button() == Qt.RightButton and not self.editing):
            self.right_dragged = False
            self.crop_origin = event.position().toPoint()
            self.crop_rect = QRect()
            return
        if self.editing and event.button() == Qt.RightButton:
            if self.annotation.draft:
                self.annotation.finish_polyline() if self.tool == "polyline" else self.annotation.cancel_draft()
            self.annotation.deselect()
            self.update_drawing_cursor()
            return
        if self.editing and event.button() == Qt.LeftButton and self.tool != "select":
            point = self.image_point(event.position())
            if self.annotation.begin_edit(point, bool(event.modifiers() & Qt.ControlModifier), 8/max(.05, self.scale)):
                self.toolbar.sync_session(self.annotation)
                self.update_drawing_cursor()
            elif self.tool == "text": self.start_text(event.position())
            else:
                self.annotation.begin_stroke(point)
            return
        if event.button() == Qt.LeftButton:
            if event.modifiers() & Qt.ControlModifier:
                self.requested.emit(self, "select")
                return
            area = self.content_rect()
            if min(event.position().x()-area.left(), event.position().y()-area.top(),
                   area.right()-event.position().x(), area.bottom()-event.position().y()) < 6:
                edges = (event.position().x() < area.left()+6, event.position().y() < area.top()+6,
                         event.position().x() > area.right()-6, event.position().y() > area.bottom()-6)
                self.resize_start = (event.globalPosition().toPoint(), self.scale, self.geometry(), edges)
            else:
                self.drag_offset = event.globalPosition().toPoint()-self.pos()
            self.setFocus()

    def mouseMoveEvent(self, event):
        self.pointer = event.position().toPoint()
        self.refresh_loupe()
        if self.crop_origin is not None:
            self.crop_rect = QRect(self.crop_origin, self.pointer).normalized().intersected(self.content_rect())
            self.right_dragged = self.crop_rect.width() > 8 and self.crop_rect.height() > 8
            self.update()
        elif self.editing and self.annotation.edit_origin is not None:
            self.annotation.move_edit(self.image_point(event.position()), event.modifiers())
        elif self.editing and self.annotation.draft:
            self.annotation.move_stroke(self.image_point(event.position()), event.modifiers())
        elif self.resize_start:
            start, scale, geometry, edges = self.resize_start
            dx, dy = event.globalPosition().x()-start.x(), event.globalPosition().y()-start.y()
            size = self.displayed_image().size()
            self.set_scale(scale+((-dx if edges[0] else dx)/size.width() if edges[0] or edges[2] else
                                  (-dy if edges[1] else dy)/size.height()))
            if edges[0]: self.move(geometry.right()-self.width()+1, self.y())
            if edges[1]: self.move(self.x(), geometry.bottom()-self.height()+1)
        elif self.drag_offset is not None:
            target = event.globalPosition().toPoint()-self.drag_offset
            if event.modifiers() & Qt.ShiftModifier:
                screen = self.screen().availableGeometry()
                m = self.FRAME_MARGIN
                if abs(target.x()+m-screen.left()) < 16: target.setX(screen.left()-m)
                if abs(target.y()+m-screen.top()) < 16: target.setY(screen.top()-m)
                if abs(target.x()+self.width()-m-screen.right()) < 16: target.setX(screen.right()-self.width()+m+1)
                if abs(target.y()+self.height()-m-screen.bottom()) < 16: target.setY(screen.bottom()-self.height()+m+1)
                self.requested.emit(self, f"snap:{target.x()}:{target.y()}")
                target = getattr(self, "snap_target", target)
            delta = target-self.pos()
            self.move(target)
            self.requested.emit(self, f"move:{delta.x()}:{delta.y()}")
        else:
            if self.editing:
                self.update_drawing_cursor()
                self.update()
            else:
                area = self.content_rect()
                self.setCursor(Qt.SizeFDiagCursor if event.position().x() > area.right()-6 and
                               event.position().y() > area.bottom()-6 else Qt.SizeAllCursor)

    def mouseReleaseEvent(self, event):
        if self.crop_origin is not None:
            if self.crop_rect.width() > 8 and self.crop_rect.height() > 8:
                first = self.image_point(self.crop_rect.topLeft()).toPoint()
                last = self.image_point(self.crop_rect.bottomRight()).toPoint()
                area = QRect(first, last).normalized()
                if self.cropping:
                    self.replace_image(self.displayed_image().copy(area))
                    self.cropping = False
                else:
                    if not self.thumbnail_rect.isEmpty(): area.translate(self.thumbnail_rect.topLeft())
                    self.thumbnail_rect = area
                    self.set_scale(self.scale)
                self.modified.emit()
            self.crop_origin = None
            self.crop_rect = QRect()
            self.update()
            return
        if self.editing and self.annotation.edit_origin is not None:
            self.annotation.end_edit()
            self.update_drawing_cursor()
            return
        if self.editing and self.annotation.draft:
            self.annotation.end_stroke(self.image_point(event.position()), event.modifiers())
            self.update_drawing_cursor()
            return
        self.drag_offset = None
        self.resize_start = None
        self.modified.emit()

    def mouseDoubleClickEvent(self, event):
        if self.editing:
            index = self.annotation.hit_test(self.image_point(event.position()))
            if index is not None and self.annotation.document.marks[index].kind == "text":
                self.annotation.end_edit()
                self.start_text(event.position())
                return
            if self.annotation.draft: self.annotation.finish_polyline()
        elif event.modifiers() & Qt.ShiftModifier:
            if self.thumbnail_rect.isEmpty():
                self.thumbnail_rect = self.image.rect()
                self.set_scale(min(1, 200/self.image.width(), 150/self.image.height()))
            else:
                self.thumbnail_rect = QRect()
                self.set_scale(1)
        else:
            self.requested.emit(self, "close")

    def wheelEvent(self, event):
        steps = event.angleDelta().y()/120
        if self.editing and self.tool != "select":
            self.annotation.adjust_width(1 if steps > 0 else -1)
            self.toolbar.sync_session(self.annotation)
            self.position_toolbar()
        elif event.modifiers() & Qt.ControlModifier:
            self.set_alpha(self.alpha+steps*.08)
            self.requested.emit(self, f"alpha:{steps*.08}")
        else:
            self.set_scale(self.scale*(1.1**steps))

    def keyPressEvent(self, event):
        key = event.key()
        modifiers = event.modifiers()
        if key == Qt.Key_Alt:
            self.alt_down = True
            self.refresh_loupe()
            return
        if key == Qt.Key_Shift and self.alt_down:
            self.rgb_color = not self.rgb_color
            self.refresh_loupe()
            return
        if key == Qt.Key_C and not modifiers and self.loupe.isVisible():
            QApplication.clipboard().setText(format_color(self.loupe.color, self.rgb_color))
            return
        if self.editing:
            group = annotation_shortcut(key, modifiers)
            if group:
                self.toolbar.activate_group(group)
            elif key == Qt.Key_F10:
                self.toolbar_action("more")
            elif key == Qt.Key_Escape:
                if self.text_entry.isVisible(): self.cancel_text()
                elif self.annotation.draft: self.annotation.cancel_draft()
                else: self.end_editing()
            elif key == Qt.Key_Space:
                self.end_editing()
            elif key in (Qt.Key_Enter, Qt.Key_Return):
                if self.annotation.draft: self.annotation.finish_polyline()
                else: self.end_editing()
            elif modifiers & Qt.ControlModifier and key in (Qt.Key_Z, Qt.Key_Y):
                self.toolbar_action("redo" if key == Qt.Key_Y or modifiers & Qt.ShiftModifier else "undo")
            elif not modifiers and key in (Qt.Key_1, Qt.Key_2):
                self.annotation.adjust_width(1 if key == Qt.Key_2 else -1)
                self.toolbar.sync_session(self.annotation)
            elif key == Qt.Key_Delete:
                self.annotation.delete_selected()
            elif modifiers & Qt.ControlModifier and modifiers & Qt.ShiftModifier and key in (Qt.Key_C, Qt.Key_V):
                getattr(self.annotation, "copy_selected" if key == Qt.Key_C else "paste_selected")()
            elif modifiers & Qt.ControlModifier and key in (Qt.Key_C, Qt.Key_S):
                self.requested.emit(self, "copy" if key == Qt.Key_C else "quick_save" if modifiers & Qt.ShiftModifier else "save")
            else: super().keyPressEvent(event)
            return
        if key == Qt.Key_Escape:
            if self.cropping:
                self.cropping = False
                self.crop_rect = QRect()
                self.update()
            else:
                self.requested.emit(self, "destroy" if modifiers & Qt.ShiftModifier else "close")
        elif key == Qt.Key_Space:
            self.start_editing()
        elif modifiers & Qt.ControlModifier and key == Qt.Key_W:
            self.requested.emit(self, "close")
        elif event.modifiers() & Qt.ControlModifier and key in (Qt.Key_C, Qt.Key_S):
            self.requested.emit(self, "copy" if key == Qt.Key_C else
                                "quick_save" if event.modifiers() & Qt.ShiftModifier else "save")
        elif key in (Qt.Key_1, Qt.Key_2):
            self.replace_image(self.displayed_image().transformed(QTransform().rotate(-90 if key == Qt.Key_1 else 90)))
        elif key in (Qt.Key_3, Qt.Key_4):
            self.replace_image(self.displayed_image().flipped(Qt.Horizontal if key == Qt.Key_3 else Qt.Vertical))
        elif key == Qt.Key_5:
            self.gray = not self.gray
            self.update()
            self.modified.emit()
        elif key in (Qt.Key_Plus, Qt.Key_Equal, Qt.Key_Minus):
            if modifiers & Qt.ControlModifier:
                amount = -.08 if key == Qt.Key_Minus else .08
                self.set_alpha(self.alpha+amount)
                self.requested.emit(self, f"alpha:{amount}")
            else:
                self.set_scale(self.scale*(.9 if key == Qt.Key_Minus else 1.1))
        else:
            super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key_Alt:
            self.alt_down = False
            self.loupe.hide()
        else: super().keyReleaseEvent(event)

    def focusNextPrevChild(self, next):
        if self.editing and self.tool in ("line", "arrow", "polyline"):
            self.choose_tool("line" if self.tool == "arrow" else "arrow")
            return True
        return super().focusNextPrevChild(next)

    def contextMenuEvent(self, event):
        if self.editing:
            event.accept()
            return
        if self.right_dragged:
            self.right_dragged = False
            return
        menu = QMenu(self)
        for title, action in [("复制图片", "copy"), ("保存图片…", "save"), ("标注  Space", "edit"),
                              ("恢复原始大小", "reset"), ("旋转 90°", "rotate"),
                              ("水平镜像", "mirror"), ("鼠标穿透", "through"),
                              ("缩略图 / 原图", "thumbnail"), ("裁剪贴图", "crop"),
                              ("灰度显示  5", "gray"), ("反色", "invert"), ("切换置顶", "topmost"),
                              ("输入尺寸 / 比例…", "size"), ("Solo / 恢复其他贴图", "solo"),
                              ("编辑颜色卡片…", "color"),
                              ("文字识别…", "ocr"), ("二维码 / 条形码识别…", "scan"),
                              ("关闭（可恢复）", "close"), ("销毁贴图", "destroy")]:
            menu.addAction(title, lambda checked=False, a=action: self.requested.emit(self, a))
        groups = menu.addMenu("移动到贴图分组")
        for name in (self.store.config.get("pin_groups", ["默认"]) if self.store else ["默认"]):
            groups.addAction(name, lambda checked=False, n=name: self.requested.emit(self, f"group:{n}"))
        background = menu.addMenu("透明背景显示")
        for mode in ("透明", "伪透明", "暗色棋盘", "亮色棋盘"):
            background.addAction(mode, lambda checked=False, m=mode: self.requested.emit(self, f"background:{m}"))
        if self.store and self.store.config.get("external_apps"):
            applications = menu.addMenu("发送到应用")
            for index, item in enumerate(self.store.config["external_apps"]):
                applications.addAction(item.get("name") or item["program"], lambda checked=False, i=index: self.requested.emit(self, f"send:{i}"))
        menu.exec(event.globalPos())

    def closeEvent(self, event):
        event.ignore()
        self.requested.emit(self, "close")

    def dragEnterEvent(self, event):
        from .import_images import image_url
        mime = event.mimeData()
        if mime.hasImage() or mime.hasUrls() or not image_url(mime).isEmpty(): event.acceptProposedAction()

    def dropEvent(self, event):
        from .import_images import image_url
        mime = event.mimeData()
        image = QImage(mime.imageData()) if mime.hasImage() else QImage()
        if image.isNull() and mime.hasUrls():
            for url in mime.urls():
                if url.isLocalFile(): image = QImage(url.toLocalFile())
                if not image.isNull(): break
        if not image.isNull(): self.dropped.emit(image)
        else:
            url = image_url(mime)
            if not url.isEmpty(): self.downloader.load(url)
        event.acceptProposedAction()


class PinManager:
    def __init__(self, store, action):
        self.store = store
        self.action = action
        self.windows = []
        self.closed = []
        self.active_group = "默认"
        self.active_group = self.store.config.get("active_pin_group", "默认")
        self.solo_states = None
        self.timer = QTimer()
        self.timer.setSingleShot(True)
        self.timer.setInterval(350)
        self.timer.timeout.connect(self.persist)

    def create(self, image, data=None, source_rect=None):
        pin = PinWindow(image, data.get("id") if data else None, self.store, source_rect)
        pin.requested.connect(self.handle)
        pin.dropped.connect(lambda image: self.create(image))
        pin.modified.connect(lambda: self.timer.start())
        self.windows.append(pin)
        pin.group = self.active_group
        if data:
            pin.set_scale(data.get("scale", 1))
            pin.set_alpha(data.get("alpha", 1))
            old_margin = data.get("frame_margin", 2)
            offset = old_margin-pin.FRAME_MARGIN
            pin.move(data.get("x", 100)+offset, data.get("y", 100)+offset)
            pin.state = data.get("state", "visible")
            pin.group = data.get("group", "默认")
            pin.topmost = data.get("topmost", True)
            pin.setWindowFlag(Qt.WindowStaysOnTopHint, pin.topmost)
            pin.gray = data.get("gray", False)
            pin.inverted = data.get("inverted", False)
            pin.background_mode = data.get("background", "暗色棋盘")
            pin.color_value = data.get("color_value", "")
            rect = data.get("thumbnail", [])
            if len(rect) == 4: pin.thumbnail_rect = QRect(*rect)
        if pin.state == "visible" and pin.group == self.active_group:
            pin.show()
        self.timer.start()
        return pin

    def handle(self, pin, action):
        if action == "close":
            pin.state = "closed"
            pin.hide()
            if pin not in self.closed:
                self.closed.append(pin)
            while len(self.closed) > max(0, self.store.config["closed_pin_limit"]):
                self.handle(self.closed.pop(0), "destroy")
        elif action == "destroy":
            if pin in self.windows:
                self.windows.remove(pin)
            if pin in self.closed:
                self.closed.remove(pin)
            pin.hide()
            pin.deleteLater()
            (self.store.pins_dir / f"{pin.key}.png").unlink(missing_ok=True)
            (self.store.pins_dir / f"{pin.key}.json").unlink(missing_ok=True)
        elif action == "reset":
            pin.thumbnail_rect = QRect()
            pin.set_scale(1)
            pin.set_alpha(1)
        elif action == "rotate":
            pin.replace_image(pin.displayed_image().transformed(QTransform().rotate(90)))
        elif action == "mirror":
            pin.replace_image(pin.displayed_image().flipped(Qt.Horizontal))
        elif action == "through":
            pin.set_through(not pin.through)
        elif action == "edit":
            pin.start_editing()
        elif action in ("ocr", "scan"):
            pin.recognize(action == "scan")
        elif action == "crop":
            if pin.editing: pin.end_editing()
            pin.cropping = True
            pin.setCursor(Qt.CrossCursor)
        elif action == "thumbnail":
            if pin.thumbnail_rect.isEmpty():
                pin.thumbnail_rect = pin.image.rect()
                pin.set_scale(min(1, 200/pin.image.width(), 150/pin.image.height()))
            else:
                pin.thumbnail_rect = QRect()
                pin.set_scale(1)
        elif action in ("gray", "invert"):
            name = "gray" if action == "gray" else "inverted"
            setattr(pin, name, not getattr(pin, name))
            pin.update()
        elif action == "topmost":
            visible = pin.isVisible()
            pin.topmost = not pin.topmost
            pin.setWindowFlag(Qt.WindowStaysOnTopHint, pin.topmost)
            if visible: pin.show()
        elif action == "size":
            self.configure_size(pin)
        elif action == "color":
            if pin.color_value:
                color = QColorDialog.getColor(QColor(pin.color_value), pin, "颜色卡片", QColorDialog.ShowAlphaChannel)
                if color.isValid():
                    pin.color_value = color.name(QColor.HexArgb)
                    pin.replace_image(color_card(color))
        elif action == "select":
            pin.selected = not pin.selected
            pin.update()
        elif action.startswith("move:"):
            if pin.selected:
                _, x, y = action.split(":")
                for other in self.windows:
                    if other is not pin and other.selected and other.state == "visible":
                        other.move(other.pos()+QPoint(int(x), int(y)))
        elif action.startswith("snap:"):
            _, x, y = action.split(":")
            m = pin.FRAME_MARGIN
            x, y = int(x)+m, int(y)+m
            width, height = pin.content_rect().width(), pin.content_rect().height()
            for other in self.windows:
                if other is pin or not other.isVisible(): continue
                r = other.content_geometry()
                if abs(x-r.left()) < 12: x = r.left()
                elif abs(x+width-r.right()-1) < 12: x = r.right()-width+1
                elif abs(x-r.right()-1) < 12: x = r.right()+1
                elif abs(x+width-r.left()) < 12: x = r.left()-width
                if abs(y-r.top()) < 12: y = r.top()
                elif abs(y+height-r.bottom()-1) < 12: y = r.bottom()-height+1
                elif abs(y-r.bottom()-1) < 12: y = r.bottom()+1
                elif abs(y+height-r.top()) < 12: y = r.top()-height
            pin.snap_target = QPoint(x-m, y-m)
        elif action.startswith("alpha:"):
            if pin.selected:
                for other in self.windows:
                    if other is not pin and other.selected: other.set_alpha(other.alpha+float(action.split(":")[1]))
        elif action.startswith("group:"):
            pin.group = action.partition(":")[2]
            groups = self.store.config.setdefault("pin_groups", ["默认"])
            if pin.group not in groups:
                groups.append(pin.group)
                self.store.save_config()
            if pin.group != self.active_group: pin.hide()
        elif action.startswith("background:"):
            pin.background_mode = action.partition(":")[2]
            pin.update_input_mask()
            pin.update()
        elif action == "solo":
            if self.solo_states is None:
                self.solo_states = {p.key: p.isVisible() for p in self.windows}
                chosen = [p for p in self.windows if p.selected] if pin.selected else [pin]
                for other in self.windows:
                    if other not in chosen: other.hide()
            else:
                for other in self.windows:
                    if self.solo_states.get(other.key) and other.state == "visible" and other.group == self.active_group: other.show()
                self.solo_states = None
        else:
            self.action(pin, action)
        self.timer.start()

    def toggle_all(self):
        any_visible = any(p.state == "visible" for p in self.windows)
        for pin in self.windows:
            if pin.state == "closed":
                continue
            pin.state = "hidden" if any_visible else "visible"
            pin.hide() if any_visible or pin.group != self.active_group else pin.show()
        self.timer.start()

    def restore_closed(self):
        if self.closed:
            pin = self.closed.pop()
            pin.state = "visible"
            pin.group = self.active_group
            pin.set_through(False)
            pin.show()
            self.timer.start()
            return True
        return False

    def clear_through(self):
        for pin in self.windows:
            if pin.through:
                pin.set_through(False)

    def toggle_through_at_cursor(self):
        point = QCursor.pos()
        pin = next((p for p in reversed(self.windows) if p.isVisible() and p.geometry().contains(point)), None)
        if pin:
            pin.set_through(not pin.through)
        else:
            self.clear_through()

    def find_lost(self):
        screens = [s.availableGeometry() for s in QGuiApplication.screens()]
        for pin in self.windows:
            if not any(r.intersects(pin.geometry()) for r in screens):
                pin.move(screens[0].topLeft()+QPoint(40, 40))
        self.timer.start()

    def restore(self):
        for data in self.store.workspace():
            image = QImage(str(self.store.pins_dir / f"{data['id']}.png"))
            if not image.isNull():
                pin = self.create(image, data)
                sidecar = self.store.pins_dir / f"{pin.key}.json"
                try:
                    marks = json.loads(sidecar.read_text(encoding="utf-8")) if sidecar.exists() else []
                    if marks:
                        pin.annotation = AnnotationSession(ImageDocument(image), pin.update, pin.annotation_changed, recent_only=True)
                        pin.annotation.document.marks = [Mark.from_dict(mark) for mark in marks]
                        pin.annotation.refresh()
                except (OSError, ValueError, TypeError, KeyError): pass
                pin.set_scale(pin.scale)
        self.find_lost()

    def set_group(self, name):
        self.active_group = name
        self.solo_states = None
        self.store.config["active_pin_group"] = name
        self.store.save_config()
        for pin in self.windows:
            pin.show() if pin.group == name and pin.state == "visible" else pin.hide()

    def add_group(self):
        name, accepted = QInputDialog.getText(None, "新建贴图分组", "名称：")
        name = name.strip()
        if accepted and name:
            groups = self.store.config.setdefault("pin_groups", ["默认"])
            if name not in groups: groups.append(name)
            self.store.save_config()
            self.set_group(name)

    def configure_size(self, pin):
        dialog = QDialog(pin)
        dialog.setWindowTitle("贴图尺寸")
        form = QFormLayout(dialog)
        size = pin.displayed_image().size()
        width, height = QSpinBox(), QSpinBox()
        width.setRange(16, round(size.width()*5))
        height.setRange(16, round(size.height()*5))
        percent = QDoubleSpinBox()
        percent.setRange(5, 500)
        percent.setSuffix(" %")
        percent.setValue(pin.scale*100)
        def update(value):
            for control, number in ((width, round(size.width()*value/100)), (height, round(size.height()*value/100))):
                control.blockSignals(True)
                control.setValue(number)
                control.blockSignals(False)
        update(percent.value())
        width.valueChanged.connect(lambda value: percent.setValue(value/size.width()*100))
        height.valueChanged.connect(lambda value: percent.setValue(value/size.height()*100))
        percent.valueChanged.connect(update)
        form.addRow("宽度（显示像素，保持比例）", width)
        form.addRow("高度", height)
        form.addRow("比例", percent)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)
        if dialog.exec() == QDialog.Accepted: pin.set_scale(percent.value()/100)
        dialog.deleteLater()

    def manage_groups(self):
        dialog = QDialog()
        dialog.setWindowTitle("管理贴图分组")
        dialog.resize(420, 360)
        layout = QVBoxLayout(dialog)
        listing = QListWidget()
        listing.setDragDropMode(QListWidget.InternalMove)
        groups = self.store.config.setdefault("pin_groups", ["默认"])
        listing.addItems(groups)
        layout.addWidget(listing)
        row = QHBoxLayout()
        def rename():
            item = listing.currentItem()
            if not item or item.text() == "默认": return
            original = item.text()
            name, accepted = QInputDialog.getText(dialog, "重命名分组", "名称：", text=original)
            name = name.strip()
            if accepted and name and name not in groups:
                groups[groups.index(original)] = name
                item.setText(name)
                for pin in self.windows:
                    if pin.group == original: pin.group = name
                if self.active_group == original: self.set_group(name)
        def remove():
            item = listing.currentItem()
            if not item or item.text() == "默认": return
            name = item.text()
            if QMessageBox.question(dialog, "删除分组", "删除该分组，并将其中贴图移动到默认分组？") != QMessageBox.Yes: return
            groups.remove(name)
            listing.takeItem(listing.row(item))
            for pin in self.windows:
                if pin.group == name: pin.group = "默认"
            self.set_group("默认")
        row.addWidget(button("重命名", rename))
        row.addWidget(button("删除分组", remove))
        row.addWidget(button("完成", dialog.accept, True))
        layout.addLayout(row)
        dialog.exec()
        self.store.config["pin_groups"] = [listing.item(i).text() for i in range(listing.count())]
        self.store.save_config()
        self.persist()
        dialog.deleteLater()

    def persist(self):
        data = []
        for pin in self.windows:
            if pin.state == "closed":
                continue
            path = self.store.pins_dir / f"{pin.key}.png"
            exported = pin.annotation.document.original if pin.annotation else pin.image
            if not path.exists() or getattr(pin, "persisted_key", None) != exported.cacheKey():
                if not exported.save(str(path), "PNG"):
                    continue
                pin.persisted_key = exported.cacheKey()
            from .storage import atomic_json
            atomic_json(self.store.pins_dir / f"{pin.key}.json", [m.to_dict() for m in pin.annotation.document.marks] if pin.annotation else [])
            data.append({"id": pin.key, "x": pin.x(), "y": pin.y(), "scale": pin.scale,
                         "frame_margin": pin.FRAME_MARGIN,
                         "alpha": pin.alpha, "state": pin.state, "group": pin.group,
                         "topmost": pin.topmost, "gray": pin.gray, "inverted": pin.inverted,
                         "color_value": pin.color_value, "background": pin.background_mode,
                         "thumbnail": list(pin.thumbnail_rect.getRect()) if not pin.thumbnail_rect.isEmpty() else []})
        self.store.save_workspace(data)
