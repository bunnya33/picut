from __future__ import annotations

from dataclasses import dataclass, replace

from PySide6.QtCore import QEvent, QPoint, QPointF, QRect, QRectF, QSize, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QCursor, QGuiApplication, QImage, QKeySequence, QPainter, QPainterPath, QPen, QShortcut
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QMenu, QMessageBox, QSpinBox, QWidget)

from . import native
from .capture_ui import CaptureToolbar, RecordingToolbar, InlineTextEntry, TOOL_GROUPS, annotation_shortcut, editing_cursor
from .editor import AnnotationSession, ImageDocument, text_rect


@dataclass
class ScreenImage:
    rect: QRect
    image: QImage
    cursor_image: QImage | None = None


class Desktop:
    def __init__(self, include_cursor=False, detect_elements=True):
        self.screens = []
        self.bounds = QRect()
        self.include_cursor = include_cursor
        self.detect_elements = detect_elements
        self.foreground_handle = native.user.GetForegroundWindow() if native.WINDOWS and detect_elements else None
        for screen in QGuiApplication.screens():
            rect = screen.geometry()
            image = screen.grabWindow(0).toImage()
            image.setDevicePixelRatio(1)
            cursor_image = None
            if (include_cursor or detect_elements) and native.WINDOWS and not image.isNull():
                import numpy as np
                cursor_image = image.convertToFormat(QImage.Format_ARGB32).copy()
                frame = np.frombuffer(cursor_image.bits(), dtype=np.uint8).reshape(cursor_image.height(), cursor_image.bytesPerLine()//4, 4)
                region = dict(native.physical_monitors()).get(screen.name())
                if region:
                    native.composite_cursor(frame, {"left": region.x(), "top": region.y(),
                                                    "width": region.width(), "height": region.height()})
            if image.isNull():
                raise RuntimeError("无法读取屏幕，请检查截图权限或当前桌面会话。")
            self.screens.append(ScreenImage(rect, image, cursor_image))
            self.bounds = self.bounds.united(rect)
        self.windows = native.desktop_windows()

    def crop(self, selection: QRect):
        selection = selection.intersected(self.bounds)
        if selection.width() < 2 or selection.height() < 2:
            return QImage()
        scale = max((s.image.width()/s.rect.width() for s in self.screens
                     if s.rect.intersects(selection)), default=1)
        result = QImage(round(selection.width()*scale), round(selection.height()*scale),
                        QImage.Format_ARGB32)
        result.fill(Qt.black)
        painter = QPainter(result)
        for screen in self.screens:
            overlap = screen.rect.intersected(selection)
            if overlap.isEmpty():
                continue
            sx, sy = screen.image.width()/screen.rect.width(), screen.image.height()/screen.rect.height()
            source = QRectF((overlap.x()-screen.rect.x())*sx, (overlap.y()-screen.rect.y())*sy,
                            overlap.width()*sx, overlap.height()*sy)
            dest = QRectF((overlap.x()-selection.x())*scale, (overlap.y()-selection.y())*scale,
                          overlap.width()*scale, overlap.height()*scale)
            painter.drawImage(dest, self.screen_image(screen), source)
        painter.end()
        return result

    def color(self, point: QPoint):
        for screen in self.screens:
            if screen.rect.contains(point):
                sx, sy = screen.image.width()/screen.rect.width(), screen.image.height()/screen.rect.height()
                return self.screen_image(screen).pixelColor(min(screen.image.width()-1, int((point.x()-screen.rect.x())*sx)),
                                               min(screen.image.height()-1, int((point.y()-screen.rect.y())*sy)))
        return QColor("black")

    def screen_image(self, screen):
        return screen.cursor_image if getattr(self, "include_cursor", False) and screen.cursor_image is not None else screen.image


class RegionMessageBox(QMessageBox):
    """Position the dialog around the captured region, including on other screens."""
    def exec(self):
        self.ensurePolished()
        self.adjustSize()
        # Give QDialog an explicit position before its first show, so its
        # automatic parent/screen centering cannot replace the region position.
        self.move(self.region.center()-self.rect().center())
        self.setAttribute(Qt.WA_Moved)
        return super().exec()

    def showEvent(self, event):
        super().showEvent(event)
        self.center_on_region()
        QTimer.singleShot(0, self.center_on_region)

    def center_on_region(self):
        if self.isVisible():
            self.setAttribute(Qt.WA_Moved)
            frame = self.frameGeometry()
            self.windowHandle().setFramePosition(self.region.center()-(frame.center()-frame.topLeft()))

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.isVisible(): QTimer.singleShot(0, self.center_on_region)


class SelectionOverlay(QWidget):
    completed = Signal(QImage, QRect, str)
    cancelled = Signal()

    def __init__(self, desktop=None, recording=False, last_rect=None, history=None, store=None,
                 output_handler=None):
        super().__init__(None, Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
        self.desktop = desktop or Desktop(include_cursor=bool(store and not recording and store.config["capture_cursor"]),
                                          detect_elements=not recording)
        self.recording = recording
        self.selection = QRect()
        self.hover = QRect()
        self.origin = None
        self.previous = QRect()
        self.operation = ""
        self.handle = ""
        self.last_rect = last_rect
        self.history = history or []
        self.history_index = -1
        self.replay = None
        self.store = store
        self.output_handler = output_handler
        self.annotation = None
        self.document_rect = QRect()
        self.tool = "select"
        self.pointer = None
        self.rgb_color = not recording
        self.text_point = None
        self.alt_magnifier = False
        self.space_origin = None
        self.space_rect = QRect()
        self.text_edit_index = None
        self.aspect_ratio = None
        self.rounding = 0
        self.decoration = False
        self.board = "desktop"
        self.caption_rect = QRect()
        self.refresh_pending = False
        self.modal_depth = 0
        self.detection_token = None
        self.region_preview_rect = QRect()
        self.region_preview_image = None
        self.setGeometry(self.desktop.bounds)
        self.setMouseTracking(True)
        self.setCursor(Qt.CrossCursor)
        self.setFocusPolicy(Qt.StrongFocus)
        # Screenshot controls are needed only after the region is chosen. Font
        # and palette layout must not delay the first frozen desktop frame.
        self._toolbar = None
        self._text_entry = None
        if recording:
            self.toolbar
            self.text_entry
        self.shortcuts = []
        actions = {"Escape": self.escape, "Ctrl+Z": self.undo, "Ctrl+Y": self.redo,
                   "Ctrl+Shift+Z": self.redo, "Ctrl+C": self.copy_selection_or_text,
                   "Ctrl+S": lambda: self.finish("save"), "Ctrl+Shift+S": lambda: self.finish("quick_save"),
                   "Ctrl+T": lambda: self.finish("pin")}
        if not recording:
            actions.update({"F3": lambda: self.finish("pin"),
                            "Ctrl+Shift+C": lambda: self.annotation and self.annotation.copy_selected(),
                            "Ctrl+Shift+V": lambda: self.annotation and self.annotation.paste_selected(),
                            "Ctrl+A": self.select_screen, "F10": self.open_more,
                            **{f"Ctrl+{i+1}": lambda n=name: self.toolbar.activate_group(n)
                               for i, (name, _, _) in enumerate(TOOL_GROUPS)}})
        for sequence, callback in actions.items():
            shortcut = QShortcut(QKeySequence(sequence), self)
            shortcut.setContext(Qt.WidgetWithChildrenShortcut)
            shortcut.activated.connect(callback)
            self.shortcuts.append(shortcut)

    @property
    def toolbar(self):
        if self._toolbar is None:
            self._toolbar = RecordingToolbar(self, True) if self.recording else CaptureToolbar(self)
            self._toolbar.setCursor(Qt.ArrowCursor)
            self._toolbar.tool_selected.connect(self.choose_tool)
            self._toolbar.action_requested.connect(self.toolbar_action)
            self._toolbar.color_selected.connect(self.choose_color)
            self._toolbar.width_changed.connect(lambda value: self.set_option("pen_width", value))
            self._toolbar.font_changed.connect(lambda value: self.set_option("font_size", value))
            self._toolbar.rotation_changed.connect(lambda value: self.set_option("rotation", value))
            self._toolbar.styles_changed.connect(self.set_styles)
        return self._toolbar

    @property
    def text_entry(self):
        if self._text_entry is None:
            self._text_entry = InlineTextEntry(self)
            self._text_entry.committed.connect(self.commit_text)
            self._text_entry.cancelled.connect(self.cancel_text)
        return self._text_entry

    def text_active(self):
        return self._text_entry is not None and self._text_entry.isVisible()

    def update_annotation_preview(self):
        self.update_drawing_cursor()
        self.update()

    def showEvent(self, event):
        super().showEvent(event)
        self.activateWindow()
        self.setFocus()
        if not self.recording and getattr(self.desktop, "detect_elements", False) and self.detection_token is None:
            from .accessibility import element_detector
            detector = element_detector()
            detector.ready.connect(self.elements_detected)
            self.detection_token = detector.request(self.desktop.foreground_handle)
        # Keep the selector visible in remote desktops and screen-sharing views.
        # Exports crop the snapshot taken before this window exists; the selector
        # never enters the screenshot and does not need capture exclusion.

    def elements_detected(self, token, rectangles):
        if token != self.detection_token or not self.isVisible(): return
        self.desktop.windows = [QRect(*rectangle) for rectangle in rectangles]+self.desktop.windows
        if self.selection.isEmpty() and self.pointer is not None:
            global_point = self.pointer+self.geometry().topLeft()
            self.hover = next((r.intersected(self.geometry()).translated(-self.geometry().topLeft())
                               for r in self.desktop.windows if r.contains(global_point)), QRect())
            self.update()

    def global_rect(self):
        return self.selection.translated(self.geometry().topLeft())

    def event(self, event):
        if event.type() == QEvent.WindowDeactivate and hasattr(self, "recording") and not self.recording:
            QTimer.singleShot(150, self.check_deactivated)
        return super().event(event)

    def check_deactivated(self):
        if not self.isVisible() or self.refresh_pending or self.modal_depth or not self.store or not self.store.config["cancel_capture_on_deactivate"]: return
        active = QApplication.activeWindow()
        if active is not None and (active is self or self.isAncestorOf(active)): return
        # Modal controls belong to this screenshot and must keep it alive.
        if QApplication.activeModalWidget() or QApplication.activePopupWidget(): return
        self.cancel()

    def ensure_annotation(self):
        if self.recording or self.selection.isEmpty():
            return
        if self.annotation is not None and self.document_rect == self.selection:
            return
        image = self.replay.copy() if self.replay is not None else self.desktop.crop(self.global_rect())
        if image.isNull():
            return
        if self.annotation is None:
            self.annotation = AnnotationSession(ImageDocument(image), self.update_annotation_preview, self.annotation_changed, recent_only=True)
            self.annotation.tool = self.tool
            if self.store: self.annotation.restore_preferences(self.store.config)
            self.annotation.select_tool(self.tool)
            self.toolbar.sync_session(self.annotation)
            self.annotation.on_preferences_changed = self.save_tool_options
        elif self.replay is None:
            old = self.document_rect
            original = self.annotation.document.original
            sx, sy = original.width()/old.width(), original.height()/old.height()
            nx, ny = image.width()/self.selection.width(), image.height()/self.selection.height()
            delta = old.topLeft()-self.selection.topLeft() if old.size() != self.selection.size() else QPoint()
            def transform(mark):
                return replace(mark, points=[QPointF((point.x()/sx+delta.x())*nx,
                                                      (point.y()/sy+delta.y())*ny) for point in mark.points])
            doc = self.annotation.document
            doc.transform_marks(transform)
            doc.original = image
            self.annotation.refresh()
        if self.board != "desktop":
            self.annotation.document.original.fill(Qt.transparent if self.board == "transparent" else Qt.white)
            self.annotation.refresh()
        self.document_rect = QRect(self.selection)
        self.annotation_changed()

    def image_point(self, point, clamp=True):
        image = self.annotation.document.original
        x = (point.x()-self.selection.x())*image.width()/self.selection.width()
        y = (point.y()-self.selection.y())*image.height()/self.selection.height()
        return QPointF(max(0, min(image.width()-1, x)), max(0, min(image.height()-1, y))) if clamp else QPointF(x, y)

    def selection_preview(self):
        if not self.annotation: return self.replay
        if self.origin is None or self.document_rect == self.selection or self.replay is not None:
            return self.annotation.preview()
        if self.region_preview_rect == self.selection and self.region_preview_image is not None:
            return self.region_preview_image
        image = self.desktop.crop(self.global_rect())
        if image.isNull(): return None
        if self.board != "desktop": image.fill(Qt.transparent if self.board == "transparent" else Qt.white)
        original = self.annotation.document.original
        old = self.document_rect
        sx, sy = original.width()/old.width(), original.height()/old.height()
        nx, ny = image.width()/self.selection.width(), image.height()/self.selection.height()
        delta = old.topLeft()-self.selection.topLeft() if old.size() != self.selection.size() else QPoint()
        doc = ImageDocument(image)
        doc.marks = [replace(mark, points=[QPointF((p.x()/sx+delta.x())*nx, (p.y()/sy+delta.y())*ny)
                                         for p in mark.points]) for mark in self.annotation.document.marks]
        self.region_preview_rect = QRect(self.selection)
        self.region_preview_image = doc.render()
        return self.region_preview_image

    def annotation_changed(self):
        if self.annotation:
            doc = self.annotation.document
            self.toolbar.set_history(bool(doc.undo_states), bool(doc.redo_states))
        self.update_drawing_cursor()
        self.update()

    def choose_tool(self, kind):
        self.commit_pending_text()
        self.ensure_annotation()
        if self.annotation and self.annotation.draft:
            self.annotation.finish_polyline()
        self.tool = kind
        if self.annotation:
            self.annotation.select_tool(kind)
            self.toolbar.sync_session(self.annotation)
        else:
            self.toolbar.set_tool(kind)
        self.save_tool_options()
        self.show_toolbar()
        self.update_drawing_cursor()
        self.setFocus()
        self.update()

    def choose_color(self, color):
        self.set_option("color", color)

    def set_styles(self, values):
        self.ensure_annotation()
        if not self.annotation: return
        for name, value in values.items(): self.annotation.set_parameter(name, value)
        self.toolbar.sync_session(self.annotation)
        self.update_text_font()
        self.show_toolbar()
        self.update_drawing_cursor()

    def set_option(self, name, value):
        self.ensure_annotation()
        if self.annotation:
            self.annotation.set_parameter(name, value)
        if name == "color":
            self.toolbar.set_color(value)
        self.update_text_font()
        self.update()

    def toolbar_action(self, action):
        if action == "cancel":
            self.cancel()
        elif action == "undo":
            self.undo()
        elif action == "redo":
            self.redo()
        elif action == "more":
            self.show_more_menu(self.toolbar.mapToGlobal(QPoint(0, self.toolbar.height())))
        else:
            self.finish(action)

    def undo(self):
        if self.text_active():
            self.text_entry.edit.undo()
        elif self.annotation:
            if self.annotation.draft:
                self.annotation.cancel_draft()
            else:
                self.annotation.document.undo()
                self.annotation.selected_index = None
                self.annotation.refresh()

    def redo(self):
        if self.text_active():
            self.text_entry.edit.redo()
        elif self.annotation:
            self.annotation.document.redo()
            self.annotation.selected_index = None
            self.annotation.refresh()

    def start_text(self, point):
        self.commit_pending_text()
        self.text_point = self.image_point(point)
        self.text_entry.edit.clear()
        self.update_text_font()
        available = self.screen_rect(point).adjusted(8, 8, -8, -8)
        self.text_entry.move(max(available.left(), min(point.x(), available.right()-self.text_entry.width()+1)),
                             max(available.top(), min(point.y(), available.bottom()-self.text_entry.height()+1)))
        self.text_entry.show()
        self.text_entry.raise_()
        self.text_entry.edit.setFocus()
        self.update()

    def commit_text(self, text):
        if self.annotation and self.text_point is not None:
            if self.text_edit_index is not None:
                self.annotation.document.edit(self.text_edit_index, text=text)
                self.annotation.refresh()
            else:
                self.annotation.add_text(self.text_point, text)
        self.cancel_text()

    def commit_pending_text(self):
        if self.text_active():
            self.commit_text(self.text_entry.edit.toPlainText())

    def cancel_text(self):
        self.text_entry.hide()
        self.text_point = None
        self.text_edit_index = None
        self.setFocus()
        self.update()

    def update_text_font(self):
        if self.annotation:
            _, font = text_rect(self.annotation.new_mark("text", []))
            self.text_entry.edit.setFont(font)

    def update_drawing_cursor(self):
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        if self.recording: return
        target = self.annotation_target(point)
        cursor = editing_cursor(target)
        if cursor is not None:
            self.setCursor(cursor)
            return
        handle = self._handle_at(point)
        if handle:
            self.setCursor(editing_cursor(handle))
            return
        brush = self.annotation and self.annotation.brush_diameter() is not None
        if brush and self.selection.contains(point) and self.childAt(point) is None:
            self.setCursor(Qt.BlankCursor)
        else:
            self.setCursor(Qt.SizeAllCursor if self.tool == "select" and self.selection.contains(point) else Qt.CrossCursor)

    def annotation_target(self, point):
        if not self.annotation or self.tool == "select" or self.childAt(point) is not None: return ""
        if self.annotation.edit_origin is not None:
            return self.annotation.edit_handle if self.annotation.edit_mode != "move" else "move"
        scale = self.annotation.cached.width()/max(1, self.selection.width())
        return self.annotation.edit_target(self.image_point(point, clamp=False), 8*scale)

    def select_screen(self):
        if self.text_active() and self.text_entry.edit.hasFocus():
            self.text_entry.edit.selectAll()
            return
        self.commit_pending_text()
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        rect = self.screen_rect(point)
        self.selection = self.rect() if self.selection == rect else rect
        self.ensure_annotation()
        self.show_toolbar()
        self.update()

    def open_more(self):
        self.show_more_menu(self.toolbar.mapToGlobal(QPoint(0, self.toolbar.height())))

    def toggle_capture_cursor(self):
        self.desktop.include_cursor = not getattr(self.desktop, "include_cursor", False)
        if self.annotation and self.replay is None and self.board == "desktop":
            self.annotation.document.original = self.desktop.crop(self.global_rect())
            self.annotation.refresh()
        self.update()

    def escape(self):
        if self.text_active():
            self.cancel_text()
        elif self.annotation and self.annotation.draft:
            self.annotation.cancel_draft()
        else:
            self.cancel()

    def screen_rect(self, local_point):
        screen = QGuiApplication.screenAt(local_point+self.geometry().topLeft()) or QGuiApplication.primaryScreen()
        return screen.geometry().translated(-self.geometry().topLeft()).intersected(self.rect())

    def _handle_at(self, point):
        if self.selection.isEmpty():
            return ""
        r = self.selection
        positions = {"nw": r.topLeft(), "ne": r.topRight(), "sw": r.bottomLeft(), "se": r.bottomRight(),
                     "n": QPoint(r.center().x(), r.top()), "s": QPoint(r.center().x(), r.bottom()),
                     "w": QPoint(r.left(), r.center().y()), "e": QPoint(r.right(), r.center().y())}
        return next((name for name, pos in positions.items() if (point-pos).manhattanLength() <= 10), "")

    def mousePressEvent(self, event):
        if not self.recording and event.button() == Qt.LeftButton and self.caption_rect.contains(event.position().toPoint()):
            self.configure_region()
            return
        if not self.recording and event.button() == Qt.MiddleButton and self.selection.contains(event.position().toPoint()):
            self.finish("pin")
            return
        if event.button() == Qt.RightButton:
            if not self.recording and self.tool != "select":
                if self.annotation and self.annotation.draft:
                    self.annotation.finish_polyline() if self.tool == "polyline" else self.annotation.cancel_draft()
                if self.annotation: self.annotation.deselect()
            else:
                self.escape()
            return
        if event.button() != Qt.LeftButton:
            return
        point = event.position().toPoint()
        self.pointer = point
        self.update_drawing_cursor()
        self.setFocus()
        self.commit_pending_text()
        if not self.recording and self.annotation_target(point):
            self.annotation.begin_edit(self.image_point(point, clamp=False),
                                       bool(event.modifiers() & Qt.ControlModifier),
                                       8*self.annotation.cached.width()/self.selection.width())
            self.toolbar.sync_session(self.annotation)
            self.update_drawing_cursor()
            return
        self.handle = self._handle_at(point)
        if not self.selection.isEmpty() and not self.handle:
            if not self.selection.contains(point):
                if self.annotation: self.annotation.deselect()
                return
            if not self.recording and self.tool != "select":
                self.ensure_annotation()
                mapped = self.image_point(point)
                if self.annotation.begin_edit(mapped, bool(event.modifiers() & Qt.ControlModifier)):
                    self.toolbar.sync_session(self.annotation)
                    self.update()
                elif self.tool == "text":
                    index = self.annotation.hit_test(mapped)
                    self.start_text(point)
                    if index is not None:
                        self.text_edit_index = index
                        self.text_point = self.annotation.document.marks[index].points[0]
                        self.text_entry.edit.setPlainText(self.annotation.document.marks[index].text)
                else:
                    self.annotation.begin_stroke(mapped)
                return
        if self.annotation: self.annotation.deselect()
        self.region_preview_rect = QRect()
        self.region_preview_image = None
        self.origin = point
        self.previous = QRect(self.selection)
        self.handle = self._handle_at(point)
        self.operation = "resize" if self.handle else "move" if self.selection.contains(point) else "new"
        if self.operation == "new":
            self.replay = None
            self.annotation = None
        if self._toolbar is not None: self._toolbar.hide()

    def mouseMoveEvent(self, event):
        point = event.position().toPoint()
        self.pointer = point
        self.update_drawing_cursor()
        if self.annotation and self.annotation.edit_origin is not None:
            self.annotation.move_edit(self.image_point(point, clamp=False), event.modifiers())
            return
        if self.annotation and self.annotation.draft and self.origin is None:
            self.annotation.move_stroke(self.image_point(point), event.modifiers())
            self.update()
            return
        if self.origin is not None:
            delta = point-self.origin
            if self.space_origin is not None and not self.recording:
                rect = self.space_rect.translated(point-self.space_origin)
                rect.moveLeft(max(0, min(self.width()-rect.width(), rect.left())))
                rect.moveTop(max(0, min(self.height()-rect.height(), rect.top())))
                self.selection = rect
            elif self.operation == "new":
                if self.aspect_ratio:
                    dx, dy = point.x()-self.origin.x(), point.y()-self.origin.y()
                    width = max(abs(dx), abs(dy)*self.aspect_ratio)
                    point = self.origin+QPoint(round(width)*(1 if dx >= 0 else -1),
                                               round(width/self.aspect_ratio)*(1 if dy >= 0 else -1))
                self.selection = QRect(self.origin, point).normalized().intersected(self.rect())
            elif self.operation == "move":
                rect = self.previous.translated(delta)
                rect.moveLeft(max(0, min(self.width()-rect.width(), rect.left())))
                rect.moveTop(max(0, min(self.height()-rect.height(), rect.top())))
                self.selection = rect
            else:
                r = QRect(self.previous)
                if "w" in self.handle: r.setLeft(point.x())
                if "e" in self.handle: r.setRight(point.x())
                if "n" in self.handle: r.setTop(point.y())
                if "s" in self.handle: r.setBottom(point.y())
                self.selection = r.normalized().intersected(self.rect())
        else:
            global_point = event.globalPosition().toPoint()
            self.hover = next((r.intersected(self.geometry()).translated(-self.geometry().topLeft())
                               for r in self.desktop.windows if r.contains(global_point)), QRect())
            if not self.selection.isEmpty():
                handle = self._handle_at(point)
                if not self.recording:
                    self.update_drawing_cursor()
                    self.update()
                    return
                cursor = (Qt.SizeFDiagCursor if handle in ("nw", "se") else
                          Qt.SizeBDiagCursor if handle in ("ne", "sw") else
                          Qt.SizeVerCursor if handle in ("n", "s") else
                          Qt.SizeHorCursor if handle in ("e", "w") else
                          Qt.SizeAllCursor if self.tool == "select" and self.selection.contains(point) else Qt.CrossCursor)
                if not (self.annotation and self.annotation.brush_diameter() is not None and self.selection.contains(point) and not handle):
                    self.setCursor(cursor)
        self.update()

    def mouseReleaseEvent(self, event):
        if self.annotation and self.annotation.edit_origin is not None:
            self.annotation.end_edit()
            self.update_drawing_cursor()
            return
        if self.annotation and self.annotation.draft and self.origin is None:
            if event.button() == Qt.LeftButton:
                self.annotation.end_stroke(self.image_point(event.position()), event.modifiers())
                self.update_drawing_cursor()
            return
        if event.button() != Qt.LeftButton or self.origin is None:
            return
        self.mouseMoveEvent(event)
        if self.operation == "new" and (event.position().toPoint()-self.origin).manhattanLength() < 5:
            self.selection = self.hover if not self.hover.isEmpty() else QRect(self.origin, QSize(3, 3))
        self.origin = None
        self.space_origin = None
        if self.selection.width() >= 2 and self.selection.height() >= 2:
            self.ensure_annotation()
            self.show_toolbar()
        self.update()

    def mouseDoubleClickEvent(self, event):
        if not self.recording and self.annotation and self.tool in ("text", "objects"):
            index = self.annotation.hit_test(self.image_point(event.position()))
            if index is not None and self.annotation.document.marks[index].kind == "text":
                self.annotation.end_edit()
                self.start_text(event.position().toPoint())
                self.text_edit_index = index
                self.text_entry.edit.setPlainText(self.annotation.document.marks[index].text)
                return
        if self.tool == "polyline" and self.annotation and self.annotation.draft:
            self.annotation.finish_polyline()
        elif self.tool == "select" and self.selection.contains(event.position().toPoint()):
            self.finish("record" if self.recording else "copy")

    def show_toolbar(self):
        if self.selection.isEmpty():
            return
        available = self.screen_rect(self.selection.center()).adjusted(6, 6, -6, -6)
        self.toolbar.fit_width(available.width())
        x = max(available.left(), min(self.selection.right()-self.toolbar.width()+1,
                                      available.right()-self.toolbar.width()+1))
        y = self.selection.bottom()+12
        if y+self.toolbar.height() > available.bottom():
            y = self.selection.top()-self.toolbar.height()-12
        y = max(available.top(), min(y, available.bottom()-self.toolbar.height()+1))
        self.toolbar.move(x, y)
        self.toolbar.show()

    def copy_selection_or_text(self):
        if self.text_active() and self.text_entry.edit.hasFocus():
            self.text_entry.edit.copy()
        else:
            self.finish("copy")

    def keyPressEvent(self, event):
        key, modifiers = event.key(), event.modifiers()
        group = annotation_shortcut(key, modifiers) if not self.recording else None
        if group:
            self.toolbar.activate_group(group)
        elif not self.recording and key == Qt.Key_F3 and not modifiers:
            self.finish("pin")
        elif not self.recording and key == Qt.Key_A and modifiers == Qt.ControlModifier:
            self.select_screen()
        elif not self.recording and key == Qt.Key_F10:
            self.open_more()
        elif not self.recording and key in (Qt.Key_QuoteLeft, Qt.Key_Exclam):
            self.toggle_capture_cursor()
        elif key == Qt.Key_Escape:
            self.escape()
        elif not self.recording and key == Qt.Key_Alt:
            self.alt_magnifier = True
            self.update()
        elif not self.recording and key == Qt.Key_Space and self.origin is not None:
            self.space_origin = self.pointer
            self.space_rect = QRect(self.selection)
        elif not self.recording and key == Qt.Key_Tab and self.tool in ("line", "arrow", "polyline"):
            self.choose_tool("line" if self.tool == "arrow" else "arrow")
        elif not self.recording and not modifiers and key in (Qt.Key_1, Qt.Key_2) and self.tool != "select":
            self.adjust_pen(1 if key == Qt.Key_2 else -1)
        elif not self.recording and modifiers & Qt.ControlModifier and modifiers & Qt.ShiftModifier and key in (Qt.Key_C, Qt.Key_V):
            if self.annotation: getattr(self.annotation, "copy_selected" if key == Qt.Key_C else "paste_selected")()
        elif key == Qt.Key_Delete and self.annotation:
            self.annotation.delete_selected()
        elif not self.recording and key == Qt.Key_F5:
            self.refresh_desktop()
        elif modifiers & Qt.ControlModifier and key in (Qt.Key_Z, Qt.Key_Y):
            if key == Qt.Key_Y or modifiers & Qt.ShiftModifier:
                self.redo()
            else:
                self.undo()
        elif key == Qt.Key_C and not modifiers:
            _, sample_color = self.pixel_sample()
            color = (f"{sample_color.red()}, {sample_color.green()}, {sample_color.blue()}" if self.rgb_color
                     else sample_color.name().upper())
            QApplication.clipboard().setText(color)
            self.update()
        elif key == Qt.Key_Shift and self.origin is None and not (self.annotation and self.annotation.draft):
            self.rgb_color = not self.rgb_color
            self.update()
        elif key in (Qt.Key_Return, Qt.Key_Enter):
            if self.annotation and self.annotation.draft and self.tool == "polyline":
                self.annotation.finish_polyline()
            else:
                self.finish("record" if self.recording else "copy")
        elif modifiers & Qt.ControlModifier and key in (Qt.Key_C, Qt.Key_S, Qt.Key_T):
            self.finish("quick_save" if key == Qt.Key_S and modifiers & Qt.ShiftModifier else
                        {Qt.Key_C: "copy", Qt.Key_S: "save", Qt.Key_T: "pin"}[key])
        elif key == Qt.Key_R and self.last_rect:
            if not self.confirm_discard("确定要替换选区吗？当前标注将不会保存。"):
                return
            self.annotation = None
            self.replay = None
            self.selection = self.last_rect.translated(-self.geometry().topLeft()).intersected(self.rect())
            self.ensure_annotation()
            self.show_toolbar()
            self.update()
        elif key in (Qt.Key_Comma, Qt.Key_Period) and self.history and not self.recording:
            if not self.confirm_discard("确定要切换历史截图吗？当前标注将不会保存。"):
                return
            self.history_index = (self.history_index + (1 if key == Qt.Key_Comma else -1)) % len(self.history)
            image = QImage(self.history[self.history_index]["path"])
            if not image.isNull():
                self.annotation = None
                self.replay = image
                size = image.size().scaled(self.size()*.75, Qt.KeepAspectRatio)
                self.selection = QRect(QPoint((self.width()-size.width())//2, (self.height()-size.height())//2), size)
                self.ensure_annotation()
                self.show_toolbar()
                self.update()
        elif key in (Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down) and not self.selection.isEmpty():
            dx = -1 if key == Qt.Key_Left else 1 if key == Qt.Key_Right else 0
            dy = -1 if key == Qt.Key_Up else 1 if key == Qt.Key_Down else 0
            if modifiers & Qt.ControlModifier:
                self.selection.setSize(QSize(max(2, self.selection.width()+abs(dx)), max(2, self.selection.height()+abs(dy))))
            elif modifiers & Qt.ShiftModifier:
                self.selection.setSize(QSize(max(2, self.selection.width()-abs(dx)), max(2, self.selection.height()-abs(dy))))
            else:
                self.selection.translate(dx, dy)
            self.selection = self.selection.intersected(self.rect())
            self.ensure_annotation()
            self.show_toolbar()
            self.update()
        elif key in (Qt.Key_W, Qt.Key_A, Qt.Key_S, Qt.Key_D) and not modifiers:
            delta = {Qt.Key_W: QPoint(0, -1), Qt.Key_A: QPoint(-1, 0), Qt.Key_S: QPoint(0, 1), Qt.Key_D: QPoint(1, 0)}[key]
            if self.recording: QCursor.setPos(QCursor.pos()+delta)
            else: native.move_cursor_pixels(delta)

    def keyReleaseEvent(self, event):
        if event.key() == Qt.Key_Alt:
            self.alt_magnifier = False
            self.update()
        elif event.key() == Qt.Key_Space and self.space_origin is not None:
            self.space_origin = None
            self.origin = self.selection.topLeft() if self.operation == "new" else self.pointer
            self.previous = QRect(self.selection)
        else:
            super().keyReleaseEvent(event)

    def adjust_pen(self, steps):
        self.ensure_annotation()
        if self.annotation:
            self.annotation.adjust_width(steps)
            self.toolbar.sync_session(self.annotation)
            self.show_toolbar()

    def wheelEvent(self, event):
        if not self.recording and self.tool != "select":
            self.adjust_pen(1 if event.angleDelta().y() > 0 else -1)
        elif not self.recording and self.selection.isEmpty() and self.pointer is not None:
            point = self.pointer+self.geometry().topLeft()
            candidates = sorted((rect for rect in self.desktop.windows if rect.contains(point)),
                                key=lambda rect: rect.width()*rect.height())
            if candidates:
                current = self.hover.translated(self.geometry().topLeft())
                index = candidates.index(current) if current in candidates else 0
                index = max(0, min(len(candidates)-1, index+(1 if event.angleDelta().y() < 0 else -1)))
                self.hover = candidates[index].translated(-self.geometry().topLeft())
                self.update()
        else:
            super().wheelEvent(event)

    def focusNextPrevChild(self, next):
        if not self.recording and self.tool in ("line", "arrow", "polyline") and not self.text_entry.isVisible():
            self.choose_tool("line" if self.tool == "arrow" else "arrow")
            return True
        return super().focusNextPrevChild(next)

    def save_tool_options(self):
        if self.store and self.annotation:
            self.annotation.save_preferences(self.store.config)
            self.store.config["annotation_tool_groups"] = dict(self.toolbar.group_choice)
            self.store.save_config()

    def configure_region(self):
        if self.selection.isEmpty(): return
        dialog = QDialog(self)
        dialog.setWindowTitle("截图尺寸与形状")
        form = QFormLayout(dialog)
        width, height = QSpinBox(), QSpinBox()
        width.setRange(2, self.width())
        height.setRange(2, self.height())
        width.setValue(self.selection.width())
        height.setValue(self.selection.height())
        units = QComboBox()
        units.addItem("dip · 界面逻辑尺寸", 1.0)
        scale = self.desktop.crop(self.global_rect()).width()/self.selection.width()
        units.addItem("px · 按屏幕缩放换算", scale)
        last_unit = [1.0]
        def change_units():
            factor = units.currentData()
            w, h = round(width.value()/last_unit[0]*factor), round(height.value()/last_unit[0]*factor)
            width.setRange(2, round(self.width()*factor))
            height.setRange(2, round(self.height()*factor))
            width.setValue(w)
            height.setValue(h)
            last_unit[0] = factor
        units.currentIndexChanged.connect(change_units)
        ratio = QComboBox()
        for title, value in (("自由", None), ("1:1", 1), ("4:3", 4/3), ("16:9", 16/9), ("9:16", 9/16)):
            ratio.addItem(title, value)
        ratio.setCurrentIndex(max(0, ratio.findData(self.aspect_ratio)))
        radius = QSpinBox()
        radius.setRange(0, 200)
        radius.setValue(self.rounding)
        form.addRow("尺寸单位", units)
        form.addRow("宽度", width)
        form.addRow("高度", height)
        form.addRow("固定比例", ratio)
        form.addRow("圆角（输出像素）", radius)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)
        if dialog.exec() == QDialog.Accepted:
            self.aspect_ratio = ratio.currentData()
            w = round(width.value()/units.currentData())
            h = round(w/self.aspect_ratio) if self.aspect_ratio else round(height.value()/units.currentData())
            self.selection.setSize(QSize(w, max(2, min(self.height(), h))))
            self.selection.moveLeft(max(0, min(self.width()-self.selection.width(), self.selection.x())))
            self.selection.moveTop(max(0, min(self.height()-self.selection.height(), self.selection.y())))
            self.rounding = radius.value()
            self.ensure_annotation()
            self.show_toolbar()
            self.update()
        dialog.deleteLater()

    def round_image(self, image):
        result = QImage(image.size(), QImage.Format_ARGB32)
        result.fill(Qt.transparent)
        path = QPainterPath()
        radius = min(self.rounding, image.width()/2, image.height()/2)
        path.addRoundedRect(QRectF(image.rect()), radius, radius)
        painter = QPainter(result)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setClipPath(path)
        painter.drawImage(0, 0, image)
        painter.end()
        return result

    def set_board(self, mode):
        if not self.confirm_discard("切换画板将清除当前标注，继续吗？"): return
        self.board = mode
        self.annotation = None
        self.ensure_annotation()
        self.update()

    def refresh_desktop(self):
        if self.refresh_pending: return
        self.refresh_pending = True
        self.hide()
        def reload():
            try:
                desktop = Desktop(include_cursor=bool(self.store and self.store.config["capture_cursor"]))
                self.desktop = desktop
                if self.annotation:
                    self.annotation.document.original = desktop.crop(self.global_rect())
                    if self.board != "desktop":
                        self.annotation.document.original.fill(Qt.transparent if self.board == "transparent" else Qt.white)
                    self.annotation.refresh()
            except Exception as error:
                QMessageBox.warning(self, "刷新失败", str(error))
            finally:
                self.refresh_pending = False
                self.show()
                self.update()
        QTimer.singleShot(130, reload)

    def contextMenuEvent(self, event):
        if self.recording: return
        if self.tool != "select":
            event.accept()
            return
        self.show_more_menu(event.globalPos())

    def show_more_menu(self, position):
        menu = QMenu(self)
        tools = menu.addMenu("其他工具")
        for title, kind in (("移动 / 调整截图选区", "select"), ("自由选择标注", "objects"),
                            ("折线（Enter 完成）", "polyline"), ("序号", "number"), ("放大镜标注", "magnify")):
            tools.addAction(title, lambda checked=False, k=kind: self.choose_tool(k))
        menu.addAction("高级标注参数与色板…", self.toolbar.choose_style)
        menu.addAction("截图尺寸 / 固定比例 / 圆角…", self.configure_region)
        menu.addAction("刷新桌面  F5", self.refresh_desktop)
        menu.addAction("文字识别…", lambda: self.recognize(False))
        menu.addAction("二维码 / 条形码识别…", lambda: self.recognize(True))
        decoration = menu.addAction("输出图片带边框 / 阴影")
        decoration.setCheckable(True)
        decoration.setChecked(self.decoration)
        decoration.toggled.connect(lambda checked: setattr(self, "decoration", checked))
        if self.store and self.store.config.get("external_apps"):
            applications = menu.addMenu("发送到应用")
            for index, item in enumerate(self.store.config["external_apps"]):
                applications.addAction(item.get("name") or item["program"], lambda checked=False, i=index: self.finish(f"send:{i}"))
        board = menu.addMenu("画板")
        for text, mode in (("桌面", "desktop"), ("白板", "white"), ("透明画板", "transparent")):
            board.addAction(text, lambda checked=False, m=mode: self.set_board(m))
        menu.exec(position)

    def recognize(self, codes):
        self.commit_pending_text()
        self.ensure_annotation()
        if not self.annotation: return
        from .recognition import RecognitionDialog
        dialog = RecognitionDialog(self.annotation.document.render(), codes, self)
        dialog.exec()
        dialog.deleteLater()

    def finish(self, action):
        if self.selection.width() < 2 or self.selection.height() < 2:
            return
        if self.recording and action != "record":
            return
        self.commit_pending_text()
        self.ensure_annotation()
        if self.annotation:
            self.annotation.finish_polyline()
        rect = self.global_rect()
        image = self.annotation.document.render() if self.annotation else self.desktop.crop(rect)
        if self.rounding and not self.recording:
            image = self.round_image(image)
        if self.decoration and not self.recording:
            from .image_tools import decorate_image
            image = decorate_image(image)
        if image.isNull():
            return
        self.save_tool_options()
        self.hide()
        if self.output_handler and action != "record" and not self.output_handler(image, action):
            self.show()
            self.raise_()
            self.activateWindow()
            return
        self.completed.emit(image, rect, action)
        self.deleteLater()

    def has_annotations(self):
        return bool((self.annotation and (self.annotation.document.marks or self.annotation.draft))
                    or (self.text_active() and self.text_entry.edit.toPlainText().strip()))

    def confirm_discard(self, message="确定要退出截图吗？当前标注将不会保存。"):
        if not self.has_annotations() or (self.store and not self.store.config["confirm_capture_exit"]):
            return True
        prompt = RegionMessageBox(QMessageBox.Question, "浮光 · 退出截图", message,
                             QMessageBox.Yes | QMessageBox.Cancel, self)
        prompt.region = self.global_rect()
        prompt.setWindowFlag(Qt.WindowStaysOnTopHint)
        prompt.button(QMessageBox.Yes).setText("确定")
        prompt.button(QMessageBox.Cancel).setText("取消")
        prompt.setDefaultButton(QMessageBox.Cancel)
        checkbox = QCheckBox("以后不再显示此消息")
        prompt.setCheckBox(checkbox)
        accepted = prompt.exec() == QMessageBox.Yes
        if accepted and checkbox.isChecked() and self.store:
            self.store.config["confirm_capture_exit"] = False
            self.store.save_config()
        prompt.deleteLater()
        return accepted

    def cancel(self):
        if not self.confirm_discard():
            return
        if not self.recording and self.store and self.store.config["keep_all_capture_history"] and not self.selection.isEmpty():
            self.ensure_annotation()
            if self.annotation: self.store.add_image(self.annotation.preview())
        self.save_tool_options()
        self.hide()
        self.cancelled.emit()
        self.deleteLater()

    def pixel_sample(self, selected_image=None):
        count = self.store.config.get("magnifier_pixels", 11) if self.store else 11
        count = max(5, min(31, int(count)))
        if count % 2 == 0: count += 1
        radius = count//2
        columns = count if self.recording else round(count*15/11)
        if columns % 2 == 0: columns += 1
        sample_size = QSize(columns, count)
        sample_offset = QPoint(columns//2, radius)
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        global_point = point+self.geometry().topLeft()
        if self.annotation and self.selection.contains(point):
            image = selected_image if selected_image is not None else self.selection_preview()
            if image is None or image.isNull(): return QImage(), QColor("black")
            mapped = QPointF((point.x()-self.selection.x())*image.width()/self.selection.width(),
                             (point.y()-self.selection.y())*image.height()/self.selection.height())
            center = QPoint(int(mapped.x()), int(mapped.y()))
            center.setX(max(0, min(image.width()-1, center.x())))
            center.setY(max(0, min(image.height()-1, center.y())))
            return image.copy(QRect(center-sample_offset, sample_size)), image.pixelColor(center)
        for screen in self.desktop.screens:
            if screen.rect.contains(global_point):
                sx, sy = screen.image.width()/screen.rect.width(), screen.image.height()/screen.rect.height()
                center = QPoint(int((global_point.x()-screen.rect.x())*sx), int((global_point.y()-screen.rect.y())*sy))
                image = self.desktop.screen_image(screen)
                return image.copy(QRect(center-sample_offset, sample_size)), image.pixelColor(center)
        return QImage(), QColor("black")

    def magnifier_rect(self):
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        available = self.screen_rect(point).adjusted(8, 8, -8, -8)
        size = QSize(180, 238) if self.recording else QSize(210, 254)
        x, y = point.x()+24, point.y()+24
        if x+size.width() > available.right():
            x = point.x()-size.width()-24
        if y+size.height() > available.bottom():
            y = point.y()-size.height()-24
        box = QRect(QPoint(max(available.left(), x), max(available.top(), y)), size)
        if self._toolbar is not None and self._toolbar.isVisible() and box.intersects(self._toolbar.geometry()):
            above = self.toolbar.y()-size.height()-8
            below = self.toolbar.geometry().bottom()+8
            box.moveTop(above if above >= available.top() else min(below, available.bottom()-size.height()+1))
        return box

    def paint_magnifier(self, p, selected_image):
        if self.store and not self.store.config["magnifier_enabled"] and not self.alt_magnifier:
            return
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        if not self.rect().contains(point) or self.childAt(point) is not None or self.text_active():
            return
        if not self.recording and not self.alt_magnifier and (self.annotation and
                                      (self.annotation.draft or self.annotation.edit_origin is not None or
                                       self.annotation.brush_diameter() is not None and self.selection.contains(point))):
            return
        sample, color = self.pixel_sample(selected_image)
        if sample.isNull():
            return
        if not self.recording:
            self.paint_screenshot_magnifier(p, sample, color, point)
            return
        p.setBrush(Qt.NoBrush)
        box = self.magnifier_rect()
        p.fillRect(box, QColor("#14252d"))
        p.setPen(QPen(QColor("#567581"), 1))
        p.drawRect(box.adjusted(0, 0, -1, -1))
        image_box = QRect(box.x()+12, box.y()+10, 156, 156)
        p.setRenderHint(QPainter.SmoothPixmapTransform, False)
        p.drawImage(image_box, sample)
        p.setPen(QPen(QColor(0, 0, 0, 45), 1))
        if not self.store or self.store.config.get("magnifier_grid", True):
            for i in range(1, sample.width()):
                x = image_box.left()+round(i*image_box.width()/sample.width())
                y = image_box.top()+round(i*image_box.height()/sample.height())
                p.drawLine(x, image_box.top(), x, image_box.bottom())
                p.drawLine(image_box.left(), y, image_box.right(), y)
        center = image_box.center()
        p.setPen(QPen(QColor("#48d6be"), 1))
        p.drawLine(image_box.left(), center.y(), image_box.right(), center.y())
        p.drawLine(center.x(), image_box.top(), center.x(), image_box.bottom())
        p.setPen(QPen(Qt.black, 2))
        p.drawRect(QRect(center-QPoint(7, 7), QSize(15, 15)))
        p.setPen(QPen(Qt.white, 1))
        p.drawRect(QRect(center-QPoint(6, 6), QSize(13, 13)))
        global_point = point+self.geometry().topLeft()
        value = f"{color.red()}, {color.green()}, {color.blue()}" if self.rgb_color else color.name().upper()
        p.fillRect(QRect(box.left()+12, box.top()+176, 13, 13), color)
        p.setPen(Qt.white)
        p.drawText(QRect(box.left()+30, box.top()+171, 140, 24), Qt.AlignVCenter, value)
        p.drawText(QRect(box.left()+8, box.top()+195, 164, 18), Qt.AlignCenter,
                   f"({global_point.x()}, {global_point.y()})")
        font = p.font()
        font.setPixelSize(11)
        p.setFont(font)
        p.drawText(QRect(box.left()+4, box.top()+217, 172, 16), Qt.AlignCenter, "C 复制颜色 · Shift 切换 RGB/HEX")

    def paint_screenshot_magnifier(self, p, sample, color, point):
        p.save()
        p.setRenderHint(QPainter.Antialiasing, False)
        p.setRenderHint(QPainter.SmoothPixmapTransform, False)
        box = self.magnifier_rect()
        image_box = QRect(box.x(), box.y(), box.width(), 154)
        p.drawImage(image_box, sample)
        cell_w = image_box.width()/sample.width()
        cell_h = image_box.height()/sample.height()
        pixel = QRectF(image_box.x()+(sample.width()//2)*cell_w,
                       image_box.y()+(sample.height()//2)*cell_h, cell_w, cell_h)
        cross = QColor(80, 165, 218, 115)
        p.fillRect(QRectF(image_box.x(), pixel.y(), image_box.width(), cell_h), cross)
        p.fillRect(QRectF(pixel.x(), image_box.y(), cell_w, image_box.height()), cross)
        p.fillRect(pixel, color)
        p.setPen(QPen(Qt.black, 1))
        p.setBrush(Qt.NoBrush)
        p.drawRect(pixel.adjusted(.5, .5, -.5, -.5))
        info = QRect(box.x(), image_box.bottom()+1, box.width(), box.height()-image_box.height())
        p.fillRect(info, QColor(0, 0, 0, 185))
        font = p.font()
        font.setFamily("Consolas")
        font.setPixelSize(16)
        p.setFont(font)
        p.setPen(Qt.white)
        global_point = point+self.geometry().topLeft()
        p.drawText(QRect(info.x()+4, info.y()+3, info.width()-8, 23), Qt.AlignCenter,
                   f"({global_point.x()}, {global_point.y()})")
        value = f"{color.red()}, {color.green()}, {color.blue()}" if self.rgb_color else color.name().upper()
        text_width = p.fontMetrics().horizontalAdvance(value)
        x = info.x()+(info.width()-text_width-24)//2
        p.fillRect(QRect(x, info.y()+30, 16, 16), color)
        p.drawText(QRect(x+24, info.y()+26, text_width+2, 24), Qt.AlignVCenter, value)
        font.setFamily("Microsoft YaHei UI")
        font.setPixelSize(13)
        p.setFont(font)
        bold = p.font()
        bold.setBold(True)
        for y, key, suffix in ((53, "C", " 复制颜色值"), (76, "Shift", " 切换 RGB/HEX")):
            segments = [("按 ", font), (key, bold), (suffix, font)]
            widths = []
            for text, face in segments:
                p.setFont(face)
                widths.append(p.fontMetrics().horizontalAdvance(text))
            x = info.x()+(info.width()-sum(widths))//2
            for (text, face), width in zip(segments, widths):
                p.setFont(face)
                p.drawText(QRect(x, info.y()+y, width+1, 20), Qt.AlignVCenter, text)
                x += width
        p.setPen(QPen(QColor(0, 0, 0, 200), 1))
        p.drawRect(box.adjusted(0, 0, -1, -1))
        p.restore()

    def paintEvent(self, event):
        p = QPainter(self)
        for screen in self.desktop.screens:
            p.drawImage(screen.rect.translated(-self.geometry().topLeft()), self.desktop.screen_image(screen))
        p.fillRect(self.rect(), QColor(0, 0, 0, 120))
        rect = self.selection if not self.selection.isEmpty() else self.hover
        selected_image = self.selection_preview()
        if not rect.isEmpty():
            p.save()
            p.setClipRect(rect)
            if selected_image is not None:
                p.drawImage(rect, selected_image)
            else:
                for screen in self.desktop.screens:
                    p.drawImage(screen.rect.translated(-self.geometry().topLeft()), self.desktop.screen_image(screen))
            p.restore()
            if self.annotation and self.annotation.selected_index is not None and self.origin is None:
                p.save()
                p.translate(rect.topLeft())
                p.scale(rect.width()/self.annotation.cached.width(), rect.height()/self.annotation.cached.height())
                self.annotation.paint_selection(p)
                p.restore()
            accent = QColor("#48d6be" if self.recording else "#1688ef")
            p.setRenderHint(QPainter.Antialiasing, False)
            p.setPen(QPen(accent, 2 if self.recording else 1))
            p.drawRect(rect.adjusted(0, 0, -1, -1))
            if not self.selection.isEmpty():
                p.setBrush(accent)
                for pos in (rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight(),
                            QPoint(rect.center().x(), rect.top()), QPoint(rect.center().x(), rect.bottom()),
                            QPoint(rect.left(), rect.center().y()), QPoint(rect.right(), rect.center().y())):
                    if self.recording: p.drawRect(QRect(pos-QPoint(3, 3), QSize(7, 7)))
                    else:
                        p.setRenderHint(QPainter.Antialiasing, True)
                        p.setPen(QPen(Qt.white, 1))
                        p.drawEllipse(QPointF(pos), 4, 4)
            scale = max((s.image.width()/s.rect.width() for s in self.desktop.screens
                         if s.rect.intersects(rect.translated(self.geometry().topLeft()))), default=1)
            size = (selected_image.size() if self.replay is not None and selected_image is not None
                    else QSize(round(rect.width()*scale), round(rect.height()*scale)))
            caption = f"{size.width()} × {size.height()} px"+("  ·  Enter 录制" if self.recording else "")
            caption_width = p.fontMetrics().horizontalAdvance(caption)+20
            box = QRect(rect.left(), max(0, rect.top()-31), max(235, caption_width) if self.recording else caption_width, 27)
            self.caption_rect = box
            p.fillRect(box, QColor("#142b32"))
            p.setPen(Qt.white)
            p.drawText(box, Qt.AlignCenter, caption)
        point = self.pointer if self.pointer is not None else self.mapFromGlobal(QCursor.pos())
        help_rect = self.screen_rect(point).adjusted(20, 20, -20, -20)
        if self.recording:
            p.setPen(Qt.white)
            p.drawText(help_rect, Qt.AlignTop | Qt.AlignHCenter,
                       "拖拽框选 · 底部工具栏直接标注 · Ctrl+Z 撤销 · C 取色 · Shift 切换 RGB/HEX · Esc 取消")
        else:
            self.paint_help(p, help_rect)
            if (self.annotation and self.selection.contains(point) and self.childAt(point) is None
                    and not self.text_active() and self.origin is None and not self.annotation_target(point)):
                p.save()
                p.setClipRect(self.selection)
                scale = self.selection.width()/self.annotation.cached.width()
                self.annotation.paint_brush(p, point, scale)
                p.restore()
        self.paint_magnifier(p, selected_image)

    def paint_help(self, p, available):
        lines = ["W A S D    移动鼠标指针 1 像素", "↑ ↓ ← →    移动截图区域", "Ctrl+1～7    选择标注工具"]
        if self.tool == "select":
            lines += ["Ctrl+A    当前屏幕 / 全屏", ", / .    回溯截图历史"]
        else:
            lines += ["1 / 2 / 滚轮    调整字号" if self.tool == "text" else "1 / 2 / 滚轮    调整画笔粗细",
                      "鼠标右键    取消选中当前标注"]
        lines += ["F3    将当前截图贴到桌面", "` / !    显示 / 隐藏捕获的鼠标指针", "Esc    退出截图"]
        font = p.font()
        font.setPixelSize(12)
        p.setFont(font)
        width = max(p.fontMetrics().horizontalAdvance(line) for line in lines)+24
        height = len(lines)*24+16
        box = QRect(available.left(), available.bottom()-height+1, width, height)
        if box.intersects(self.selection) or (self._toolbar is not None and self._toolbar.isVisible()
                                             and box.intersects(self._toolbar.geometry())):
            lines = ["Ctrl+1～7 标注 · 滚轮调粗细", "F3 贴图 · Ctrl+A 全屏 · Esc 退出",
                     "` / ! 显示 / 隐藏捕获的鼠标"]
            height = len(lines)*24+16
            width = max(p.fontMetrics().horizontalAdvance(line) for line in lines)+24
            box = QRect(available.left(), available.bottom()-height+1, width, height)
        p.fillRect(box, QColor(20, 20, 20, 210))
        p.setPen(Qt.white)
        for i, line in enumerate(lines):
            p.drawText(box.adjusted(12, 8+i*24, -8, 0), Qt.AlignTop | Qt.AlignLeft, line)
