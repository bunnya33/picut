import threading
import time

import pytest
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage, QPainter
from PySide6.QtTest import QTest

from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.editor import AnnotationSession, ImageDocument, Mark
from test_inline_capture import make_overlay, dispose
from test_capture_toolbar_v04 import stroke


@pytest.mark.parametrize("scale", [1, 2])
@pytest.mark.parametrize("operation", ["move", "resize"])
def test_region_preview_updates_before_release_without_mutating_history(app, scale, operation):
    overlay = make_overlay(app, scale=scale)
    try:
        source = overlay.desktop.screens[0].image
        painter = QPainter(source)
        for x in range(0, 800, 20):
            for y in range(0, 600, 20):
                painter.fillRect(QRect(x*scale, y*scale, 20*scale, 20*scale),
                                 QColor(x//4 % 256, y//3 % 256, (x+y)//5 % 256))
        painter.end()
        session = overlay.annotation
        session.document.original = overlay.desktop.crop(overlay.global_rect())
        session.select_tool("line")
        session.begin_stroke(QPointF(60*scale, 100*scale))
        session.end_stroke(QPointF(230*scale, 100*scale))
        overlay.choose_tool("select")
        old_points = [QPointF(p) for p in session.document.marks[0].points]
        history = len(session.document.undo_states)
        start = QPoint(360, 250) if operation == "move" else QPoint(120, 100)
        end = start+QPoint(35, 25) if operation == "move" else QPoint(80, 80)
        QTest.mousePress(overlay, Qt.LeftButton, pos=start)
        QTest.mouseMove(overlay, end)
        assert overlay.origin is not None
        assert session.document.marks[0].points == old_points
        assert len(session.document.undo_states) == history
        sample_point = overlay.selection.topLeft()+QPoint(25, 30)
        expected = source.pixelColor(sample_point.x()*scale, sample_point.y()*scale)
        preview = overlay.selection_preview()
        assert preview.pixelColor(25*scale, 30*scale) == expected
        assert overlay.grab().toImage().pixelColor(sample_point) == expected
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=end)
        assert session.cached == preview
        assert len(session.document.undo_states) == history
        delta = QPointF(0, 0) if operation == "move" else QPointF(40*scale, 20*scale)
        assert session.document.marks[0].points == [p+delta for p in old_points]
        overlay.undo()
        assert session.document.marks == []
        overlay.redo()
        assert session.cached == preview
    finally:
        dispose(app, overlay)


@pytest.mark.parametrize("scale", [1, 2])
@pytest.mark.parametrize("tool", ["rect", "ellipse"])
def test_hollow_shape_interior_confirms_and_cannot_reselect(app, scale, tool):
    overlay = make_overlay(app, scale=scale)
    try:
        overlay.choose_tool(tool)
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        session = overlay.annotation
        before = session.document.clone(session.document.marks)
        edge = QPoint(230, 180) if tool == "rect" else QPoint(290, 300)
        # Avoid the ellipse's midpoint handle for the move hit.
        if tool == "ellipse": edge = QPoint(245, 292)
        QTest.mouseMove(overlay, edge+QPoint(1, 1))
        QTest.mouseMove(overlay, edge)
        assert overlay.cursor().shape() == Qt.SizeAllCursor
        blank = QPoint(260, 240)
        QTest.mouseMove(overlay, blank)
        assert overlay.cursor().shape() == Qt.CrossCursor
        QTest.mouseClick(overlay, Qt.LeftButton, pos=blank)
        assert session.selected_index is None and session.draft is None
        assert len(session.document.marks) == 1
        assert session.document.marks[0].points == before[0].points
        assert not session.begin_edit(overlay.image_point(edge))
        QTest.mouseMove(overlay, edge)
        assert overlay.cursor().shape() == Qt.CrossCursor
        overlay.adjust_pen(1)
        assert session.document.marks[0].width == 4
        stroke(overlay, QPoint(420, 160), QPoint(550, 240))
        assert session.selected_index == 1
        assert len(session.document.marks) == 2
        assert session.document.marks[0].points == before[0].points
        assert not session.begin_edit(overlay.image_point(edge))
        assert session.selected_index is None
    finally:
        dispose(app, overlay)


@pytest.mark.parametrize("scale", [1, 2])
def test_hover_cursors_match_handles_and_outline_drag(app, scale):
    overlay = make_overlay(app, scale=scale)
    try:
        overlay.choose_tool("rect")
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        session = overlay.annotation
        handles = session.control_points(session.document.marks[0])
        shapes = {"nw": Qt.SizeFDiagCursor, "se": Qt.SizeFDiagCursor,
                  "ne": Qt.SizeBDiagCursor, "sw": Qt.SizeBDiagCursor,
                  "n": Qt.SizeVerCursor, "s": Qt.SizeVerCursor,
                  "e": Qt.SizeHorCursor, "w": Qt.SizeHorCursor,
                  "rotate": Qt.BitmapCursor}
        for name, pos in handles.items():
            point = (pos/scale+QPointF(120, 100)).toPoint()
            QTest.mouseMove(overlay, point+QPoint(1, 1))
            QTest.mouseMove(overlay, point)
            assert overlay.cursor().shape() == shapes[name], name
            assert overlay.annotation_target(point) == name
        edge = QPoint(230, 180)
        QTest.mouseMove(overlay, edge)
        assert overlay.cursor().shape() == Qt.SizeAllCursor
        before = session.document.clone(session.document.marks)[0]
        stroke(overlay, edge, edge+QPoint(25, 15))
        assert session.document.marks[0].points == [p+QPointF(25*scale, 15*scale) for p in before.points]
        overlay.undo()
        assert session.selected_index is None
        assert overlay.cursor().shape() == Qt.CrossCursor
    finally:
        dispose(app, overlay)


def test_blank_outside_selection_confirms_last_shape(app):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool("rect")
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(720, 150))
        assert overlay.annotation.selected_index is None
        assert len(overlay.annotation.document.marks) == 1
    finally:
        dispose(app, overlay)


def test_freehand_empty_bounds_are_not_movable(app):
    image = QImage(400, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image), recent_only=True)
    session.select_tool("pen")
    session.begin_stroke(QPointF(50, 50))
    session.move_stroke(QPointF(50, 200))
    session.end_stroke(QPointF(200, 200))
    assert session.edit_target(QPointF(50, 100)) == "move"
    assert session.edit_target(QPointF(125, 100)) == ""
    assert not session.begin_edit(QPointF(125, 100))
    assert not session.begin_edit(QPointF(50, 100))


def test_dashed_outline_gaps_confirm_and_object_mode_only_edits_pending(app):
    image = QImage(400, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image), recent_only=True)
    session.select_tool("line")
    session.dash = "dash"
    session.begin_stroke(QPointF(30, 150))
    session.end_stroke(QPointF(350, 150))
    session.select_tool("objects")
    assert session.selected_index == 0
    assert session.cached.pixelColor(40, 150) == QColor("#ff0004")
    assert session.cached.pixelColor(50, 150) == QColor("white")
    assert session.edit_target(QPointF(40, 150)) == "move"
    assert session.edit_target(QPointF(50, 150)) == ""
    assert not session.begin_edit(QPointF(50, 150))
    assert session.selected_index is None
    assert not session.begin_edit(QPointF(40, 150))


def test_magnifier_has_no_grid_one_pixel_cross_and_translucent_panel(app, store):
    store.config["magnifier_grid"] = True
    overlay = make_overlay(app, store=store)
    try:
        overlay.pointer = QPoint(250, 200)
        sample = QImage(15, 11, QImage.Format_ARGB32)
        sample.fill(QColor(240, 240, 240))
        canvas = QImage(800, 600, QImage.Format_ARGB32)
        canvas.fill(QColor(120, 160, 200))
        painter = QPainter(canvas)
        overlay.paint_screenshot_magnifier(painter, sample, QColor(240, 240, 240), overlay.pointer)
        painter.end()
        box = overlay.magnifier_rect()
        color = lambda x, y: canvas.pixelColor(box.x()+x, box.y()+y)
        # Adjacent cells have identical edges: no grid or inset border.
        assert color(13, 13) == color(14, 14) == QColor(240, 240, 240)
        assert color(1, 1) == QColor(240, 240, 240)
        assert color(96, 25) == color(112, 25) == QColor(240, 240, 240)
        assert color(98, 25) == color(111, 25) != QColor(240, 240, 240)
        assert color(25, 69) == color(25, 84) == QColor(240, 240, 240)
        assert color(25, 70) == color(25, 83) != QColor(240, 240, 240)
        assert color(105, 77) == QColor(240, 240, 240)
        assert color(98, 70) == QColor("black")
        panel = color(4, 160)
        assert 0 < panel.red() < 120 and 0 < panel.green() < 160 and 0 < panel.blue() < 200
        assert panel.red() < panel.green() < panel.blue()
        # At an image edge, the center pixel stays at the center of the sample.
        overlay.pointer = overlay.selection.topLeft()
        sampled, selected = overlay.pixel_sample()
        assert sampled.pixelColor(sampled.width()//2, sampled.height()//2) == selected
    finally:
        dispose(app, overlay)


def test_slow_element_provider_does_not_block_capture_or_apply_stale_results(app, monkeypatch):
    from floatshot import accessibility
    entered, release = threading.Event(), threading.Event()
    def slow_provider(handle):
        entered.set()
        release.wait(2)
        return [(30, 40, 100, 80)]
    monkeypatch.setattr(accessibility, "element_rectangles", slow_provider)
    detector = accessibility.ElementDetector(app)
    monkeypatch.setattr(accessibility, "element_detector", lambda: detector)
    image = QImage(800, 600, QImage.Format_ARGB32)
    image.fill(Qt.white)
    def desktop():
        result = Desktop.__new__(Desktop)
        result.bounds, result.windows = image.rect(), []
        result.screens = [ScreenImage(image.rect(), image)]
        result.detect_elements, result.foreground_handle = True, 1
        return result
    first, second = SelectionOverlay(desktop()), SelectionOverlay(desktop())
    done = []
    detector.ready.connect(lambda token, rects: done.append(token))
    try:
        start = time.perf_counter()
        first.show()
        app.processEvents()
        assert time.perf_counter()-start < .8
        assert first.isVisible() and first._toolbar is None and first._text_entry is None
        assert entered.wait(.5)
        first.hide()
        second.show()
        assert second.detection_token != first.detection_token
        release.set()
        for _ in range(50):
            if done: break
            QTest.qWait(10)
        assert done == [first.detection_token]
        assert first.desktop.windows == second.desktop.windows == []
    finally:
        release.set()
        dispose(app, first)
        dispose(app, second)


def test_hidden_workbench_capture_has_no_compositor_delay(app, store, monkeypatch):
    from floatshot.app import MainWindow
    window = MainWindow(store, testing=True)
    calls = []
    monkeypatch.setattr("floatshot.app.QTimer.singleShot", lambda delay, callback: calls.append(delay))
    try:
        window.begin_selection(False)
        assert calls == [0]
        window.selection_pending = False
        store.config["capture_delay"] = 2
        window.begin_selection(False)
        assert calls[-1] == 2000
        window.selection_pending = False
        window.show()
        window.begin_selection(False)
        assert calls[-1] == 2130
        window.selection_pending = False
        window.begin_selection(True)
        assert calls[-1] == 130
    finally:
        window.hotkeys.clear()
        app.removeNativeEventFilter(window.hotkeys)
        window.close()
        window.deleteLater()
        app.processEvents()
