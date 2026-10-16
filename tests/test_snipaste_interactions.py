from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage, QWheelEvent
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from floatshot.app import MainWindow
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.image_tools import parse_color
from floatshot.pins import PinManager
from test_inline_capture import make_overlay, draw_line, dispose


def wheel(widget, amount=120, modifiers=Qt.NoModifier):
    event = QWheelEvent(QPointF(50, 50), QPointF(50, 50), QPoint(), QPoint(0, amount),
                        Qt.NoButton, modifiers, Qt.NoScrollPhase, False)
    QApplication.sendEvent(widget, event)


def test_annotation_wheel_keys_tab_and_independent_tool_parameters(app, store):
    overlay = make_overlay(app, store)
    try:
        overlay.choose_tool("arrow")
        wheel(overlay)
        QTest.keyClick(overlay, Qt.Key_2)
        assert overlay.annotation.pen_width == 6
        overlay.choose_color(QColor("#123456"))
        overlay.choose_tool("rect")
        overlay.set_option("pen_width", 11)
        overlay.choose_color(QColor("#aabbcc"))
        overlay.choose_tool("arrow")
        assert overlay.annotation.pen_width == 6
        assert overlay.annotation.color == QColor("#aabbcc")
        QTest.keyClick(overlay, Qt.Key_Tab)
        assert overlay.tool == "line"
        QTest.keyClick(overlay, Qt.Key_Tab)
        assert overlay.tool == "arrow"
        overlay.choose_tool("text")
        wheel(overlay)
        assert overlay.annotation.font_size == 23
        overlay.save_tool_options()
        assert store.config["annotation_options"]["arrow"]["pen_width"] == 6
    finally: dispose(app, overlay)


def test_middle_click_pins_selection_and_shift_save_uses_quick_output(app):
    overlay = make_overlay(app)
    received = []
    overlay.completed.connect(lambda image, rect, action: received.append(action))
    QTest.mouseClick(overlay, Qt.MiddleButton, pos=QPoint(250, 200))
    assert received == ["pin"]
    app.processEvents()
    overlay = make_overlay(app)
    overlay.completed.connect(lambda image, rect, action: received.append(action))
    QTest.mouseClick(overlay.toolbar.actions["save"], Qt.LeftButton, Qt.ShiftModifier)
    assert received == ["pin", "quick_save"]
    app.processEvents()


def test_space_moves_in_progress_selection_and_release_resumes_resizing(app):
    overlay = make_overlay(app)
    try:
        overlay.selection = QRect()
        overlay.annotation = None
        QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(100, 100))
        QTest.mouseMove(overlay, QPoint(250, 200))
        size = overlay.selection.size()
        QTest.keyPress(overlay, Qt.Key_Space)
        QTest.mouseMove(overlay, QPoint(280, 230))
        assert overlay.selection.topLeft() == QPoint(130, 130)
        assert overlay.selection.size() == size
        QTest.keyRelease(overlay, Qt.Key_Space)
        QTest.mouseMove(overlay, QPoint(300, 250))
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(300, 250))
        assert overlay.selection.topLeft() == QPoint(130, 130)
        assert overlay.selection.bottomRight() == QPoint(300, 250)
    finally: dispose(app, overlay)


def test_alt_summons_disabled_magnifier_and_release_hides_it(app, store):
    store.config["magnifier_enabled"] = False
    overlay = make_overlay(app, store)
    try:
        QTest.mouseMove(overlay, QPoint(250, 200))
        box = overlay.magnifier_rect()
        panel_point = box.topLeft()+QPoint(4, 160)
        color_before = overlay.grab().toImage().pixelColor(panel_point)
        QTest.keyPress(overlay, Qt.Key_Alt)
        panel = overlay.grab().toImage().pixelColor(panel_point)
        assert 0 < panel.red() < color_before.red()
        QTest.keyRelease(overlay, Qt.Key_Alt)
        assert overlay.grab().toImage().pixelColor(panel_point) == color_before
    finally: dispose(app, overlay)


def test_recording_keeps_original_middle_tab_and_wheel_behavior(app):
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(0, 0, 800, 600)
    image = QImage(800, 600, QImage.Format_ARGB32)
    image.fill(Qt.white)
    desktop.screens = [ScreenImage(desktop.bounds, image)]
    desktop.windows = []
    overlay = SelectionOverlay(desktop, recording=True)
    overlay.selection = QRect(100, 100, 400, 300)
    overlay.show()
    received = []
    overlay.completed.connect(lambda image, rect, action: received.append(action))
    QTest.mouseClick(overlay, Qt.MiddleButton, pos=QPoint(200, 200))
    wheel(overlay)
    QTest.keyClick(overlay, Qt.Key_Tab)
    assert received == []
    assert overlay.annotation is None
    QTest.keyClick(overlay, Qt.Key_Return)
    assert received == ["record"]
    app.processEvents()


def test_selected_annotation_moves_changes_and_undo_restores_it(app):
    overlay = make_overlay(app)
    try:
        draw_line(overlay, "rect")
        original = overlay.annotation.document.clone(overlay.annotation.document.marks)[0]
        QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(250, 200))
        QTest.mouseMove(overlay, QPoint(290, 230))
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(290, 230))
        moved = overlay.annotation.document.marks[0]
        assert moved.points[0] == original.points[0]+QPointF(40, 30)
        overlay.set_option("color", QColor("blue"))
        assert overlay.annotation.document.marks[0].color == QColor("blue")
        overlay.undo()
        assert overlay.annotation.document.marks[0].color == original.color
        overlay.undo()
        assert overlay.annotation.document.marks[0].points == original.points
    finally: dispose(app, overlay)


def create_pin(store):
    manager = PinManager(store, lambda pin, action: None)
    image = QImage(480, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    pin = manager.create(image)
    return manager, pin


def test_pin_space_edits_in_place_and_export_keeps_marks(app, store):
    manager, pin = create_pin(store)
    try:
        position = pin.pos()
        QTest.keyClick(pin, Qt.Key_Space)
        assert pin.editing and pin.toolbar.isVisible()
        assert pin.toolbar.parent() is pin
        pin.choose_tool("line")
        QTest.mousePress(pin, Qt.LeftButton, pos=QPoint(80, 100))
        QTest.mouseMove(pin, QPoint(200, 100))
        QTest.mouseRelease(pin, Qt.LeftButton, pos=QPoint(200, 100))
        sampled = pin.image_point(QPoint(120, 100)).toPoint()
        assert pin.export_image().pixelColor(sampled) == QColor("#ff0004")
        QTest.keyClick(pin, Qt.Key_Space)
        assert not pin.editing and not pin.toolbar.isVisible()
        assert pin.pos() == position
        QTest.keyClick(pin, Qt.Key_Space)
        assert len(pin.annotation.document.marks) == 1
        QTest.keyClick(pin, Qt.Key_Z, Qt.ControlModifier)
        assert pin.export_image().pixelColor(sampled) == QColor("white")
    finally: manager.handle(pin, "destroy")


def test_pin_middle_reset_transparency_keys_and_ctrl_w_restore(app, store):
    manager, pin = create_pin(store)
    pin.set_alpha(.4)
    QTest.keyClick(pin, Qt.Key_Plus, Qt.ControlModifier)
    assert pin.alpha > .4
    pin.set_scale(.6)
    QTest.mouseClick(pin, Qt.MiddleButton, pos=QPoint(100, 100))
    assert pin.scale == pin.alpha == 1
    QTest.keyClick(pin, Qt.Key_W, Qt.ControlModifier)
    assert pin.state == "closed"
    assert manager.restore_closed()
    assert pin.isVisible()
    manager.handle(pin, "destroy")


def test_pin_alt_color_groups_selection_and_solo(app, store):
    manager, pin = create_pin(store)
    other = manager.create(pin.image)
    QTest.mouseMove(pin, QPoint(100, 100))
    QTest.keyPress(pin, Qt.Key_Alt)
    assert pin.loupe.isVisible()
    QTest.keyClick(pin, Qt.Key_C)
    assert app.clipboard().text() == "#FFFFFF"
    QTest.keyRelease(pin, Qt.Key_Alt)
    assert not pin.loupe.isVisible()
    manager.handle(pin, "select")
    manager.handle(other, "select")
    position = other.pos()
    manager.handle(pin, "move:20:10")
    assert other.pos() == position+QPoint(20, 10)
    other.selected = False
    manager.handle(pin, "solo")
    assert pin.isVisible() and not other.isVisible()
    manager.handle(pin, "solo")
    assert other.isVisible()
    manager.handle(other, "group:工作")
    assert not other.isVisible()
    manager.set_group("工作")
    assert other.isVisible() and not pin.isVisible()
    manager.handle(pin, "destroy")
    manager.handle(other, "destroy")


def test_rgb_parser_accepts_integers_fractions_and_rejects_invalid():
    assert parse_color("244, 91, 105") == QColor("#f45b69")
    assert parse_color("rgb(244,91,105)") == QColor("#f45b69")
    assert parse_color("1.0 0.0 0.0") == QColor("red")
    assert not parse_color("256, 0, 0").isValid()
    assert not parse_color("0.5, 2.0, 0.2").isValid()


def test_f3_restores_closed_pin_for_same_clipboard_and_rgb_card(app, store):
    window = MainWindow(store, testing=True)
    try:
        app.clipboard().setText("244, 91, 105")
        window.pin_clipboard()
        pin = window.pins.windows[0]
        assert pin.image.pixelColor(5, 5) == QColor("#f45b69")
        window.pins.handle(pin, "close")
        window.pin_clipboard()
        assert len(window.pins.windows) == 1 and pin.state == "visible"
    finally:
        for pin in list(window.pins.windows): window.pins.handle(pin, "destroy")
        window.hotkeys.clear()
        app.removeNativeEventFilter(window.hotkeys)
        window.close()
        window.deleteLater()
        app.processEvents()
