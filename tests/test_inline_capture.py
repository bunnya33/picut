import pytest
from shiboken6 import isValid
from PySide6.QtCore import QPoint, QRect, QTimer, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QMessageBox

from floatshot.app import MainWindow
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.storage import Store


def make_overlay(app, store=None, scale=1, output_handler=None):
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(0, 0, 800, 600)
    image = QImage(800*scale, 600*scale, QImage.Format_ARGB32)
    image.fill(QColor("white"))
    desktop.screens = [ScreenImage(desktop.bounds, image)]
    desktop.windows = []
    overlay = SelectionOverlay(desktop, store=store, output_handler=output_handler)
    overlay.show()
    QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(120, 100))
    QTest.mouseMove(overlay, QPoint(599, 399))
    QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(599, 399))
    return overlay


def dispose(app, overlay):
    overlay.close()
    overlay.deleteLater()
    app.processEvents()


def draw_line(overlay, kind="line"):
    QTest.mouseClick(overlay.toolbar.tool_buttons[kind], Qt.LeftButton)
    if kind in ("line", "arrow", "polyline"):
        combo = overlay.toolbar.line_type
        if kind == "polyline": overlay.choose_tool(kind)
        else: combo.setCurrentIndex(combo.findData(kind+":"+("filled" if kind == "arrow" else "")))
    elif kind in ("rect", "ellipse"):
        overlay.toolbar.shape_type.setCurrentIndex(overlay.toolbar.shape_type.findData(kind))
    QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(180, 200))
    end = QPoint(350, 280 if kind in ("rect", "ellipse") else 200)
    QTest.mouseMove(overlay, end)
    QTest.mouseRelease(overlay, Qt.LeftButton, pos=end)


@pytest.mark.parametrize("scale", [1, 2])
def test_inline_annotation_export_and_undo_without_editor_window(app, scale):
    overlay = make_overlay(app, scale=scale)
    try:
        draw_line(overlay)
        assert overlay.toolbar.parent() is overlay
        assert len(overlay.annotation.document.marks) == 1
        assert overlay.annotation.cached.pixelColor(120*scale, 100*scale) == QColor("#ff0004")
        QTest.keyClick(overlay, Qt.Key_Z, Qt.ControlModifier)
        assert overlay.annotation.cached.pixelColor(120*scale, 100*scale) == QColor("white")
        QTest.keyClick(overlay, Qt.Key_Y, Qt.ControlModifier)
        assert overlay.annotation.cached.pixelColor(120*scale, 100*scale) == QColor("#ff0004")
        received = []
        overlay.completed.connect(lambda image, rect, action: received.append((image, rect, action)))
        QTest.mouseClick(overlay.toolbar.actions["copy"], Qt.LeftButton)
        image, rect, action = received[0]
        assert action == "copy"
        assert rect == QRect(120, 100, 480, 300)
        assert image.size().width() == 480*scale
        assert image.pixelColor(120*scale, 100*scale) == QColor("#ff0004")
        assert image.pixelColor(0, 0) == QColor("white")
    finally:
        if isValid(overlay) and overlay.isVisible():
            overlay.close()
            overlay.deleteLater()
        app.processEvents()


def test_selected_magnifier_reads_annotations_and_color_copy_keeps_capture_open(app):
    overlay = make_overlay(app, scale=2)
    try:
        draw_line(overlay)
        QTest.mouseMove(overlay, QPoint(250, 200))
        QTest.keyPress(overlay, Qt.Key_Alt)
        sample, color = overlay.pixel_sample()
        assert (sample.width(), sample.height()) == (15, 11)
        assert color == QColor("#ff0004")
        rendered = overlay.grab().toImage()
        loupe = overlay.magnifier_rect()
        assert rendered.pixelColor(loupe.left()+105, loupe.top()+77) == color
        # Information panel is translucent over the white selection.
        panel = rendered.pixelColor(loupe.left()+4, loupe.top()+160)
        assert 0 < panel.red() < 100 and panel.red() == panel.green() == panel.blue()
        QTest.keyClick(overlay, Qt.Key_C)
        assert app.clipboard().text() == "255, 0, 4"
        assert overlay.isVisible()
        QTest.keyClick(overlay, Qt.Key_Shift)
        QTest.keyClick(overlay, Qt.Key_C)
        assert app.clipboard().text() == "#FF0004"
    finally:
        dispose(app, overlay)


def test_polyline_finishes_in_selection_and_enter_then_exports(app):
    overlay = make_overlay(app)
    QTest.mouseClick(overlay.toolbar.tool_buttons["polyline"], Qt.LeftButton)
    overlay.choose_tool("polyline")
    QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(200, 200))
    QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(300, 250))
    QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(400, 200))
    QTest.keyClick(overlay, Qt.Key_Return)
    assert overlay.isVisible()
    assert len(overlay.annotation.document.marks) == 1
    assert len(overlay.annotation.document.marks[0].points) == 3
    received = []
    overlay.completed.connect(lambda image, rect, action: received.append(action))
    QTest.keyClick(overlay, Qt.Key_Return)
    assert received == ["copy"]
    app.processEvents()


def test_text_is_entered_inside_capture_and_committed_before_export(app):
    overlay = make_overlay(app)
    try:
        QTest.mouseClick(overlay.toolbar.tool_buttons["text"], Qt.LeftButton)
        QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(200, 180))
        assert overlay.text_entry.isVisible()
        assert not overlay.text_entry.isWindow()
        overlay.text_entry.edit.setPlainText("Screenshot note")
        QTest.keyClick(overlay.text_entry.edit, Qt.Key_Return, Qt.ControlModifier)
        assert not overlay.text_entry.isVisible()
        assert overlay.annotation.document.marks[0].text == "Screenshot note"
        QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(220, 260))
        overlay.text_entry.edit.setPlainText("Discard this text")
        QTest.keyClick(overlay.text_entry.edit, Qt.Key_Escape)
        assert overlay.isVisible()
        assert len(overlay.annotation.document.marks) == 1
    finally:
        dispose(app, overlay)


def answer_prompt(app, answer, disable=False, seen=None):
    def reply():
        prompt = app.activeModalWidget()
        assert isinstance(prompt, QMessageBox)
        if seen is not None:
            seen.append(prompt.text())
        prompt.checkBox().setChecked(disable)
        prompt.button(answer).click()
    QTimer.singleShot(0, reply)


def test_escape_confirmation_cancel_accept_and_preference_persistence(app, store):
    overlay = make_overlay(app, store)
    draw_line(overlay)
    seen = []
    answer_prompt(app, QMessageBox.Cancel, disable=True, seen=seen)
    QTest.keyClick(overlay, Qt.Key_Escape)
    assert seen and overlay.isVisible()
    assert store.config["confirm_capture_exit"]
    assert len(overlay.annotation.document.marks) == 1
    cancelled = []
    overlay.cancelled.connect(lambda: cancelled.append(True))
    answer_prompt(app, QMessageBox.Yes, disable=True)
    QTest.keyClick(overlay, Qt.Key_Escape)
    assert cancelled == [True]
    assert not Store(store.root).config["confirm_capture_exit"]
    app.processEvents()
    next_overlay = make_overlay(app, store)
    draw_line(next_overlay)
    next_overlay.cancelled.connect(lambda: cancelled.append(True))
    QTest.keyClick(next_overlay, Qt.Key_Escape)
    assert cancelled == [True, True]
    app.processEvents()


def test_escape_without_annotations_exits_directly(app, store):
    overlay = make_overlay(app, store)
    cancelled = []
    overlay.cancelled.connect(lambda: cancelled.append(True))
    QTest.keyClick(overlay, Qt.Key_Escape)
    assert cancelled == [True]
    app.processEvents()


def test_cancelled_save_restores_selection_and_preserves_marks(app, store):
    attempts = []
    def reject_save(image, action):
        attempts.append((image, action))
        return False
    overlay = make_overlay(app, store, output_handler=reject_save)
    try:
        draw_line(overlay)
        QTest.mouseClick(overlay.toolbar.actions["save"], Qt.LeftButton)
        assert attempts[0][1] == "save"
        assert overlay.isVisible()
        assert overlay.toolbar.isVisible()
        assert len(overlay.annotation.document.marks) == 1
        assert attempts[0][0].pixelColor(120, 100) == QColor("#ff0004")
    finally:
        dispose(app, overlay)


def test_capture_output_uses_workbench_storage_without_opening_editor(app, store):
    window = MainWindow(store, testing=True)
    overlay = make_overlay(app, store, output_handler=window.image_output)
    window.overlay = overlay
    overlay.completed.connect(window.selection_done)
    try:
        draw_line(overlay)
        QTest.mouseClick(overlay.toolbar.actions["copy"], Qt.LeftButton)
        assert window.overlay is None
        assert window.editor is None
        assert len(store.history) == 1
        assert app.clipboard().image().pixelColor(120, 100) == QColor("#ff0004")
    finally:
        window.hotkeys.clear()
        app.removeNativeEventFilter(window.hotkeys)
        window.close()
        window.deleteLater()
        app.processEvents()


def test_resize_preserves_annotation_position_and_toolbar_stays_on_screen(app):
    overlay = make_overlay(app)
    try:
        draw_line(overlay)
        QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(120, 100))
        QTest.mouseMove(overlay, QPoint(80, 80))
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(80, 80))
        assert overlay.annotation.cached.pixelColor(160, 120) == QColor("#ff0004")
        assert overlay.screen_rect(overlay.selection.center()).contains(overlay.toolbar.geometry())
        overlay.selection.moveBottom(overlay.height()-2)
        overlay.ensure_annotation()
        overlay.show_toolbar()
        assert overlay.screen_rect(overlay.selection.center()).contains(overlay.toolbar.geometry())
        assert overlay.toolbar.geometry().bottom() < overlay.selection.top()
    finally:
        dispose(app, overlay)
