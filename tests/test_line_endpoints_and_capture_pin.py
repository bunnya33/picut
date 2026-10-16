import ctypes
from types import SimpleNamespace

import pytest
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage, QPainter
from PySide6.QtTest import QTest
from shiboken6 import isValid

from floatshot import native
from floatshot.app import MainWindow
from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.editor import AnnotationSession, ImageDocument
from test_capture_toolbar_v04 import stroke
from test_inline_capture import make_overlay, dispose
from test_snipaste_interactions import create_pin


@pytest.mark.parametrize("scale", [1, 2])
@pytest.mark.parametrize("tool", ["line", "arrow"])
@pytest.mark.parametrize("endpoint", ["start", "end"])
def test_endpoint_drag_changes_only_grabbed_point_and_can_undo(app, scale, tool, endpoint):
    overlay = make_overlay(app, scale=scale)
    try:
        overlay.choose_tool(tool)
        # A descending line ensures endpoints are not confused with bounding corners.
        start, end = QPoint(380, 180), QPoint(200, 300)
        stroke(overlay, start, end)
        session = overlay.annotation
        before = session.document.clone(session.document.marks)[0]
        assert session.control_points(before) == {"start": before.points[0], "end": before.points[-1]}
        grab = start if endpoint == "start" else end
        QTest.mouseMove(overlay, grab+QPoint(1, 1))
        QTest.mouseMove(overlay, grab)
        assert overlay.annotation_target(grab) == endpoint
        assert overlay.cursor().shape() == Qt.SizeAllCursor
        QTest.mousePress(overlay, Qt.LeftButton, pos=grab)
        assert session.edit_mode == "endpoint"
        moved = grab+QPoint(35, 20)
        QTest.mouseMove(overlay, moved)
        index = 0 if endpoint == "start" else 1
        other = 1-index
        assert session.document.marks[0].points[index] == before.points[index]+QPointF(35*scale, 20*scale)
        assert session.document.marks[0].points[other] == before.points[other]
        # Further mouse moves must share a single undo checkpoint.
        moved += QPoint(10, 5)
        QTest.mouseMove(overlay, moved)
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=moved)
        changed = session.document.clone(session.document.marks)[0]
        assert changed.points[index] == before.points[index]+QPointF(45*scale, 25*scale)
        assert changed.points[other] == before.points[other]
        assert changed.color == before.color and changed.width == before.width
        assert len(session.document.undo_states) == 2
        overlay.undo()
        assert session.document.marks[0].points == before.points
        overlay.redo()
        assert session.document.marks[0].points == changed.points
        # Confirmed lines must still obey the recent-object editing rule.
        assert session.selected_index is None
        assert not session.begin_edit(changed.points[index])
    finally:
        dispose(app, overlay)


@pytest.mark.parametrize("tool", ["line", "arrow"])
def test_line_selection_paints_only_two_endpoint_handles(app, tool):
    image = QImage(400, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image), recent_only=True)
    session.select_tool(tool)
    session.begin_stroke(QPointF(80, 80))
    session.end_stroke(QPointF(300, 210))
    selection = image.copy()
    painter = QPainter(selection)
    session.paint_selection(painter)
    painter.end()
    for name, point in session.control_points(session.document.marks[0]).items():
        assert selection.pixelColor(point.toPoint()+QPoint(3, 0)) == QColor("#888888"), name
    # All selection pixels must be within the two handle circles.
    for x in range(selection.width()):
        for y in range(selection.height()):
            if selection.pixelColor(x, y) != QColor("white"):
                assert min((QPointF(x, y)-p).manhattanLength() for p in session.document.marks[0].points) <= 8
    assert session.document.render().pixelColor(80, 76) == QColor("white")


@pytest.mark.parametrize("endpoint", ["start", "end"])
def test_endpoint_can_return_to_origin_and_shift_snaps_about_other_endpoint(app, endpoint):
    image = QImage(400, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image), recent_only=True)
    session.select_tool("arrow")
    session.begin_stroke(QPointF(100, 100))
    session.end_stroke(QPointF(250, 150))
    before = list(session.document.marks[0].points)
    index = 0 if endpoint == "start" else 1
    assert session.begin_edit(before[index])
    session.move_edit(before[index]+QPointF(30, 40))
    session.move_edit(before[index])
    assert session.document.marks[0].points == before
    session.move_edit(QPointF(180, 190), Qt.ShiftModifier)
    edited = session.document.marks[0].points
    assert edited[1-index] == before[1-index]
    delta = edited[index]-edited[1-index]
    assert abs(delta.x()) < .001 or abs(delta.y()) < .001 or abs(abs(delta.x())-abs(delta.y())) < .001
    session.end_edit()


@pytest.mark.parametrize("tool", ["line", "arrow"])
def test_pin_annotations_share_independent_endpoint_editing(app, store, tool):
    manager, pin = create_pin(store)
    try:
        pin.start_editing()
        pin.choose_tool(tool)
        start, end = QPoint(80, 100), QPoint(260, 200)
        stroke(pin, start, end)
        session = pin.annotation
        before = list(session.document.marks[0].points)
        assert set(session.control_points(session.document.marks[0])) == {"start", "end"}
        QTest.mouseMove(pin, end+QPoint(1, 1))
        QTest.mouseMove(pin, end)
        assert pin.cursor().shape() == Qt.SizeAllCursor
        stroke(pin, end, end+QPoint(30, -40))
        assert session.document.marks[0].points[0] == before[0]
        assert session.document.marks[0].points[-1] == pin.image_point(end+QPoint(30, -40))
    finally:
        manager.handle(pin, "destroy")


def close_window(app, window):
    for pin in list(window.pins.windows): window.pins.handle(pin, "destroy")
    window.pins.timer.stop()
    window.hotkeys.clear()
    app.removeNativeEventFilter(window.hotkeys)
    window.tray.hide()
    window.close()
    window.deleteLater()
    app.processEvents()


@pytest.mark.parametrize("trigger", ["local", "text_input", "global"])
def test_capture_f3_pins_current_image_with_annotations_instead_of_clipboard(app, store, trigger):
    window = MainWindow(store, testing=True)
    window.notify = lambda text: None
    overlay = make_overlay(app, store, output_handler=window.capture_output)
    window.overlay = overlay
    overlay.completed.connect(window.selection_done)
    clipboard = QImage(40, 30, QImage.Format_ARGB32)
    clipboard.fill(QColor("blue"))
    app.clipboard().setImage(clipboard)
    try:
        overlay.choose_tool("line")
        stroke(overlay, QPoint(200, 180), QPoint(380, 250))
        if trigger == "text_input":
            overlay.choose_tool("text")
            QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(220, 300))
            overlay.text_entry.edit.setPlainText("Pinned with F3")
            QTest.keyClick(overlay.text_entry.edit, Qt.Key_F3)
        elif trigger == "global":
            # Deliver the same WM_HOTKEY callback that RegisterHotKey uses,
            # without reserving a real system key or operating other apps.
            message = native.wt.MSG()
            message.message, message.wParam = 0x0312, 50000
            window.hotkeys.registered[50000] = window.hotkeys.actions["pin"]
            window.hotkeys.names[50000] = "pin"
            assert window.hotkeys.nativeEventFilter(b"windows_generic_MSG", ctypes.addressof(message)) == (True, 0)
            window.hotkeys.registered.clear()
        else:
            QTest.keyClick(overlay, Qt.Key_F3)
        assert window.overlay is None
        assert len(window.pins.windows) == 1 and len(store.history) == 1
        pin = window.pins.windows[0]
        assert pin.isVisible() and pin.topmost and pin.windowFlags() & Qt.WindowStaysOnTopHint
        assert pin.image.size().width() == 480 and pin.image.size().height() == 300
        assert pin.image.pixelColor(0, 0) == QColor("white")
        assert pin.image == QImage(store.history[0]["path"])
        assert pin.image != overlay.annotation.document.original
        if trigger == "text_input": assert overlay.annotation.document.marks[-1].text == "Pinned with F3"
        assert app.clipboard().image() == clipboard
        assert window.last_rect == overlay.global_rect()
        assert pin.content_geometry() == window.last_rect
    finally:
        if isValid(overlay):
            overlay.hide()
            overlay.deleteLater()
        close_window(app, window)


def test_global_pin_outside_capture_keeps_clipboard_behavior_and_empty_capture_waits(app, store):
    window = MainWindow(store, testing=True)
    window.notify = lambda text: None
    image = QImage(60, 40, QImage.Format_ARGB32)
    image.fill(QColor("blue"))
    app.clipboard().setImage(image)
    selected = make_overlay(app, store)
    overlay = SelectionOverlay(selected.desktop, store=store, output_handler=window.capture_output)
    dispose(app, selected)
    window.overlay = overlay
    try:
        overlay.show()
        window.pin_clipboard()
        QTest.keyClick(overlay, Qt.Key_F3)
        assert overlay.isVisible() and window.overlay is overlay
        assert not window.pins.windows and not store.history
        overlay.hide()
        window.overlay = None
        window.pin_clipboard()
        assert len(window.pins.windows) == 1
        assert window.pins.windows[0].image == image
    finally:
        dispose(app, overlay)
        close_window(app, window)


def test_recording_selection_does_not_use_screenshot_f3_action(app):
    selected = make_overlay(app)
    recording = SelectionOverlay(selected.desktop, recording=True)
    dispose(app, selected)
    recording.selection = recording.rect().adjusted(120, 100, -200, -200)
    actions = []
    recording.completed.connect(lambda image, rect, action: actions.append(action))
    try:
        recording.show()
        QTest.keyClick(recording, Qt.Key_F3)
        assert recording.isVisible() and not actions
    finally:
        dispose(app, recording)


@pytest.mark.parametrize("scale", [1, 1.5, 2])
@pytest.mark.parametrize("origin", [QPoint(0, 0), QPoint(-800, -100), QPoint(1920, 0)])
def test_f3_preserves_image_global_position_size_and_restart_layout(app, store, monkeypatch, scale, origin):
    window = MainWindow(store, testing=True)
    window.notify = lambda text: None
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(origin.x(), origin.y(), 800, 600)
    desktop.windows = []
    source = QImage(round(800*scale), round(600*scale), QImage.Format_ARGB32)
    source.fill(QColor("white"))
    desktop.screens = [ScreenImage(desktop.bounds, source)]
    selector = SelectionOverlay(desktop, store=store, output_handler=window.capture_output)
    selector.selection = QRect(120, 100, 480, 300)
    selector.show()
    selector.ensure_annotation()
    selector.completed.connect(window.selection_done)
    window.overlay = selector
    rect = selector.global_rect()
    restored = None
    try:
        window.pin_clipboard()
        pin = window.pins.windows[0]
        assert pin.content_geometry() == rect
        assert pin.image.width() == round(rect.width()*scale)
        assert pin.scale == pytest.approx(1/scale)
        assert pin.isVisible() and pin.topmost
        window.pins.persist()
        from floatshot.pins import PinManager
        restored = PinManager(store, lambda *args: None)
        # The synthetic monitor must exist for the usual lost-window recovery.
        monkeypatch.setattr("floatshot.pins.QGuiApplication.screens",
                            lambda: [SimpleNamespace(availableGeometry=lambda: desktop.bounds)])
        restored.restore()
        same = restored.windows[0]
        assert same.content_geometry() == rect
    finally:
        if isValid(selector):
            selector.hide()
            selector.deleteLater()
        if restored:
            for pin in list(restored.windows): restored.handle(pin, "destroy")
            restored.timer.stop()
        close_window(app, window)


@pytest.mark.parametrize("scale", [1, 2])
def test_capture_pin_shadow_padding_does_not_shift_original_image(app, store, scale):
    window = MainWindow(store, testing=True)
    window.notify = lambda text: None
    overlay = make_overlay(app, store, scale=scale, output_handler=window.capture_output)
    window.overlay = overlay
    overlay.completed.connect(window.selection_done)
    overlay.decoration = True
    region = overlay.global_rect()
    try:
        QTest.keyClick(overlay, Qt.Key_F3)
        pin = window.pins.windows[0]
        # decorate_image adds 18 image pixels on each edge.
        padding = round(18/scale)
        assert pin.content_geometry().topLeft()+QPoint(padding, padding) == region.topLeft()
        assert pin.image.width() == region.width()*scale+36
        assert pin.content_rect().adjusted(padding, padding, -padding, -padding).size() == region.size()
        assert pin.scale == pytest.approx(1/scale)
    finally:
        if isValid(overlay):
            overlay.hide()
            overlay.deleteLater()
        close_window(app, window)


def test_large_capture_pin_keeps_full_selected_size_while_clipboard_stays_centered(app, store):
    window = MainWindow(store, testing=True)
    window.notify = lambda text: None
    overlay = make_overlay(app, store, output_handler=window.capture_output)
    window.overlay = overlay
    overlay.completed.connect(window.selection_done)
    # A full-screen selection must bypass the clipboard's 70% fit rule.
    overlay.selection = overlay.rect()
    overlay.ensure_annotation()
    region = overlay.global_rect()
    try:
        window.pin_clipboard()
        pinned = window.pins.windows[0]
        assert pinned.content_geometry() == region
        from PySide6.QtGui import QCursor, QGuiApplication
        screen = QGuiApplication.screenAt(QCursor.pos()) or QGuiApplication.primaryScreen()
        clipboard = QImage(60, 40, QImage.Format_ARGB32)
        clipboard.fill(Qt.blue)
        app.clipboard().setImage(clipboard)
        window.pin_clipboard()
        default = window.pins.windows[-1]
        assert default.image == clipboard
        assert default.pos() == screen.availableGeometry().center()-default.rect().center()
    finally:
        if isValid(overlay):
            overlay.hide()
            overlay.deleteLater()
        close_window(app, window)
