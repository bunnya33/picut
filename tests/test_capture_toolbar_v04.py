import pytest
from PySide6.QtCore import QPoint, QPointF, Qt
from PySide6.QtGui import QColor, QContextMenuEvent, QFont, QImage
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from floatshot.editor import AnnotationSession, ImageDocument, Mark, text_rect
from test_inline_capture import make_overlay, dispose
from test_snipaste_interactions import wheel


def stroke(overlay, start, end):
    QTest.mousePress(overlay, Qt.LeftButton, pos=start)
    QTest.mouseMove(overlay, end)
    QTest.mouseRelease(overlay, Qt.LeftButton, pos=end)


def test_group_shortcuts_and_inline_subtypes(app):
    overlay = make_overlay(app)
    try:
        for key, expected in zip(range(Qt.Key_1, Qt.Key_7+1),
                                 ("rect", "arrow", "pen", "marker", "mosaic", "text", "eraser")):
            QTest.keyClick(overlay, key, Qt.ControlModifier)
            assert overlay.tool == expected
        QTest.keyClick(overlay, Qt.Key_1, Qt.ControlModifier)
        overlay.toolbar.shape_type.setCurrentIndex(overlay.toolbar.shape_type.findData("ellipse"))
        assert overlay.tool == "ellipse"
        QTest.keyClick(overlay, Qt.Key_2, Qt.ControlModifier)
        overlay.toolbar.line_type.setCurrentIndex(overlay.toolbar.line_type.findData("arrow:hollow"))
        assert overlay.annotation.arrow_head == "hollow"
        assert QApplication.activeModalWidget() is None
        QTest.keyClick(overlay, Qt.Key_B)
        assert overlay.tool == "pen"
        QTest.keyClick(overlay, Qt.Key_T)
        assert overlay.tool == "text"
    finally: dispose(app, overlay)


@pytest.mark.parametrize("tool", ["line", "rect", "ellipse"])
def test_wheel_changes_latest_mark_export_and_undo(app, tool):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool(tool)
        stroke(overlay, QPoint(160, 140), QPoint(240, 190))
        stroke(overlay, QPoint(300, 240), QPoint(450, 320))
        first, last = overlay.annotation.document.marks
        assert first.width == last.width == 4
        wheel(overlay, 120)
        assert overlay.annotation.document.marks[0].width == 4
        assert overlay.annotation.document.marks[1].width == 5
        assert overlay.toolbar.pen_width.value() == 5
        after = overlay.annotation.document.render()
        overlay.undo()
        assert overlay.annotation.document.marks[1].width == 4
        assert overlay.annotation.document.render() != after
        overlay.redo()
        assert overlay.annotation.document.render() == after
        overlay.toolbar.pen_width.setValue(12)
        # Undo clears selection; the next stroke uses the current size.
        stroke(overlay, QPoint(460, 140), QPoint(540, 190))
        assert overlay.annotation.document.marks[-1].width == 12
    finally: dispose(app, overlay)


@pytest.mark.parametrize("scale", [1, 2])
def test_brush_preview_hides_during_drawing_and_never_enters_export(app, scale):
    overlay = make_overlay(app, scale=scale)
    try:
        QTest.keyClick(overlay, Qt.Key_B)
        overlay.toolbar.pen_width.setValue(20)
        point = QPoint(260, 220)
        QTest.mouseMove(overlay, point-QPoint(20, 20))
        QTest.mouseMove(overlay, point)
        arm = point+QPoint(round(10/scale)+5, 0)
        assert overlay.grab().toImage().pixelColor(arm).red() > 200
        assert overlay.grab().toImage().pixelColor(arm).green() < 220
        assert overlay.annotation.document.render().pixelColor(280*scale, 120*scale) == QColor("white")
        QTest.mousePress(overlay, Qt.LeftButton, pos=point)
        assert overlay.annotation.draft is not None
        assert overlay.grab().toImage().pixelColor(arm) == QColor("white")
        QTest.mouseMove(overlay, point+QPoint(0, 40))
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=point+QPoint(0, 40))
        # The selected stroke has editing cursors; the tip returns over blank pixels.
        blank = point+QPoint(90, 40)
        QTest.mouseMove(overlay, blank)
        arm = blank+QPoint(round(10/scale)+5, 0)
        assert overlay.grab().toImage().pixelColor(arm).green() < 220
        exported = overlay.annotation.document.render()
        assert exported.pixelColor(round((arm.x()-120)*scale), round((arm.y()-100)*scale)) == QColor("white")
    finally: dispose(app, overlay)


@pytest.mark.parametrize("tool", ["rect", "ellipse"])
def test_nine_handles_resize_one_edge_with_opposite_edge_anchored(app, tool):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool(tool)
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        session = overlay.annotation
        original = session.document.clone(session.document.marks)[0]
        handles = session.control_points(original)
        assert len(handles) == 9
        handle = (handles["e"]+QPointF(120, 100)).toPoint()
        stroke(overlay, handle, handle+QPoint(45, 0))
        resized = session.document.marks[0]
        assert resized.points[0] == original.points[0]
        assert resized.points[-1] == original.points[-1]+QPointF(45, 0)
        assert overlay.selection.width() == 480
        overlay.undo()
        assert session.document.marks[0].points == original.points
        assert overlay.cursor().shape() == Qt.CrossCursor
    finally: dispose(app, overlay)


def test_rotated_shape_resize_anchors_opposite_world_edge(app):
    image = QImage(400, 300, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image))
    session.select_tool("rect")
    session.document.add(Mark("rect", [QPointF(80, 80), QPointF(180, 150)], rotation=30))
    session.selected_index = 0
    before = session.control_points(session.document.marks[0])
    assert session.begin_edit(before["e"])
    session.move_edit(before["e"]+QPointF(30, 15))
    session.end_edit()
    after = session.control_points(session.document.marks[0])
    assert (after["w"]-before["w"]).manhattanLength() < .001
    assert (after["e"]-before["e"]).manhattanLength() > 30


def test_right_click_deselects_and_keeps_drawing_tool(app):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool("rect")
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        assert overlay.annotation.selected_index == 0
        QTest.mouseClick(overlay, Qt.RightButton, pos=QPoint(450, 220))
        assert overlay.tool == "rect"
        assert overlay.annotation.selected_index is None
        assert len(overlay.annotation.document.marks) == 1
        opened = []
        overlay.show_more_menu = lambda _: opened.append(True)
        event = QContextMenuEvent(QContextMenuEvent.Mouse, QPoint(450, 220), overlay.mapToGlobal(QPoint(450, 220)))
        QApplication.sendEvent(overlay, event)
        assert not opened
    finally: dispose(app, overlay)


def test_size_swatch_popup_changes_recent_annotation_without_modal_dialog(app):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool("line")
        stroke(overlay, QPoint(200, 180), QPoint(380, 180))
        overlay.toolbar.size_button.click()
        popup = overlay.toolbar.size_popup
        from PySide6.QtWidgets import QSlider
        popup.findChild(QSlider).setValue(15)
        assert overlay.annotation.document.marks[0].width == 15
        assert overlay.annotation.cached.pixelColor(160, 86) == QColor("#ff0004")
        assert QApplication.activeModalWidget() is None
        popup.close()
        overlay.choose_tool("select")
        assert not any(control.isChecked() for control in overlay.toolbar.group_buttons.values())
    finally: dispose(app, overlay)


def test_pin_uses_group_shortcuts_and_recent_width_while_editing(app, store):
    from test_snipaste_interactions import create_pin
    manager, pin = create_pin(store)
    try:
        pin.start_editing()
        QTest.keyClick(pin, Qt.Key_1, Qt.ControlModifier)
        assert pin.tool == "rect"
        stroke(pin, QPoint(80, 80), QPoint(200, 180))
        assert len(pin.annotation.control_points(pin.annotation.document.marks[0])) == 9
        wheel(pin)
        assert pin.annotation.document.marks[0].width == 5
        QTest.mouseClick(pin, Qt.RightButton, pos=QPoint(250, 150))
        assert pin.editing and pin.tool == "rect" and pin.annotation.selected_index is None
    finally: manager.handle(pin, "destroy")


def test_font_controls_affect_export_metrics_and_survive_serialization(app):
    overlay = make_overlay(app)
    try:
        QTest.keyClick(overlay, Qt.Key_T)
        overlay.toolbar.bold.click()
        overlay.toolbar.italic.click()
        overlay.toolbar.font_family.setCurrentFont(QFont("Segoe UI"))
        QTest.mouseClick(overlay, Qt.LeftButton, pos=QPoint(200, 180))
        QTest.keyClicks(overlay.text_entry.edit, "Bold text")
        # Tool letters must remain ordinary characters inside text input.
        QTest.keyClicks(overlay.text_entry.edit, " BT")
        QTest.keyClick(overlay.text_entry.edit, Qt.Key_A, Qt.ControlModifier)
        assert overlay.text_entry.edit.textCursor().hasSelection()
        assert overlay.selection.width() == 480
        QTest.keyClick(overlay.text_entry.edit, Qt.Key_Return, Qt.ControlModifier)
        mark = overlay.annotation.document.marks[0]
        assert mark.text == "Bold text BT"
        restored = Mark.from_dict(mark.to_dict())
        _, font = text_rect(restored)
        assert font.bold() and font.italic() and font.family() == "Segoe UI"
        assert overlay.annotation.document.render() != overlay.annotation.document.original
    finally: dispose(app, overlay)


def test_pixel_strength_independent_from_width_and_colors_hidden(app):
    overlay = make_overlay(app)
    try:
        image = overlay.annotation.document.original
        for x in range(image.width()):
            for y in range(image.height()):
                image.setPixelColor(x, y, QColor.fromHsv((x*3+y*5) % 360, 220, 200))
        QTest.keyClick(overlay, Qt.Key_5, Qt.ControlModifier)
        overlay.toolbar.intensity.setValue(4)
        stroke(overlay, QPoint(200, 180), QPoint(380, 300))
        original = overlay.annotation.document.render()
        assert not overlay.toolbar.custom_color.isVisible()
        assert not overlay.toolbar.palette_widget.isVisible()
        wheel(overlay)
        assert overlay.annotation.document.marks[0].intensity == 4
        assert overlay.annotation.document.render() == original
        overlay.toolbar.intensity.setValue(20)
        assert overlay.annotation.document.render() != original
        overlay.toolbar.pixel_type.setCurrentIndex(overlay.toolbar.pixel_type.findData("blur"))
        assert overlay.tool == "blur"
        assert QApplication.activeModalWidget() is None
    finally: dispose(app, overlay)


def test_cursor_toggle_reuses_snapshot_and_retains_annotations(app, store):
    overlay = make_overlay(app, store)
    try:
        screen = overlay.desktop.screens[0]
        screen.cursor_image = screen.image.copy()
        screen.cursor_image.setPixelColor(250, 220, QColor("black"))
        overlay.choose_tool("line")
        stroke(overlay, QPoint(200, 180), QPoint(380, 180))
        original_points = list(overlay.annotation.document.marks[0].points)
        QTest.keyClick(overlay, Qt.Key_QuoteLeft)
        assert overlay.annotation.document.render().pixelColor(130, 120) == QColor("black")
        QTest.keyClick(overlay, Qt.Key_Exclam)
        assert overlay.annotation.document.render().pixelColor(130, 120) == QColor("white")
        assert overlay.annotation.document.marks[0].points == original_points
        assert store.config["capture_cursor"] is False
        QTest.keyClick(overlay, Qt.Key_A, Qt.ControlModifier)
        assert overlay.selection == overlay.rect()
        assert len(overlay.annotation.document.marks) == 1
    finally: dispose(app, overlay)
