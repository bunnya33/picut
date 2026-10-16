import pytest
from PySide6.QtCore import QPoint, QPointF, QRect, QTimer, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QMessageBox

from floatshot.editor import AnnotationSession, ImageDocument, Mark
from floatshot.pins import PinManager
from floatshot.storage import Store
from test_inline_capture import dispose, make_overlay


def render(head, color=QColor(255, 0, 4), width=9):
    image = QImage(360, 160, QImage.Format_ARGB32)
    image.fill(Qt.transparent)
    document = ImageDocument(image)
    document.add(Mark("arrow", [QPointF(40, 80), QPointF(300, 80)], color, width, arrow_head=head))
    return document.render(), document.marks[0]


def thickness(image, x):
    return sum(image.pixelColor(x, y).alpha() > 200 for y in range(image.height()))


def test_reference_arrows_have_tapered_constant_and_completely_hollow_bodies(app):
    tapered, _ = render("tapered")
    filled, _ = render("filled")
    hollow, mark = render("hollow")
    assert thickness(tapered, 90) < thickness(tapered, 200)
    assert thickness(filled, 90) == thickness(filled, 200)
    assert filled.pixelColor(200, 80) == QColor(255, 0, 4)
    assert hollow.pixelColor(200, 80).alpha() == 0
    assert hollow.pixelColor(280, 80).alpha() == 0
    assert hollow.pixelColor(200, 74).alpha() > 200
    assert not AnnotationSession.painted_hit(mark, QPointF(200, 80))
    assert AnnotationSession.painted_hit(mark, QPointF(200, 74), tolerance=0)
    assert not AnnotationSession.painted_hit(mark, QPointF(150, 55))


@pytest.mark.parametrize("head", ["tapered", "filled"])
def test_translucent_arrow_body_and_head_have_no_overlap_seams(app, head):
    image, _ = render(head, QColor(255, 0, 4, 128))
    assert image.pixelColor(200, 80).alpha() == 128
    assert image.pixelColor(280, 80).alpha() == 128
    assert max(image.pixelColor(x, y).alpha() for x in range(image.width()) for y in range(image.height())) == 128


@pytest.mark.parametrize("head", ["tapered", "filled", "hollow"])
def test_dashed_arrow_gaps_are_not_draggable_and_direction_keeps_style(app, head):
    _, mark = render(head)
    mark.dash = "dash"
    image = QImage(360, 160, QImage.Format_ARGB32)
    image.fill(Qt.white)
    doc = ImageDocument(image)
    doc.add(mark)
    dashed = doc.render()
    gaps = [QPointF(x+.5, 80.5) for x in range(60, 220) if dashed.pixelColor(x, 80) == Qt.white]
    assert gaps
    assert all(not AnnotationSession.painted_hit(mark, point) for point in gaps)
    mark.arrow_mode = "reverse"
    assert doc.render() != dashed
    assert Mark.from_dict(mark.to_dict()).arrow_head == head


@pytest.mark.parametrize("head", ["tapered", "filled", "hollow"])
def test_arrow_style_roundtrip_endpoints_and_recent_object_confirmation(app, head):
    _, mark = render(head)
    image = QImage(360, 160, QImage.Format_ARGB32)
    image.fill(Qt.white)
    session = AnnotationSession(ImageDocument(image), recent_only=True)
    session.document.add(Mark.from_dict(mark.to_dict()))
    session.selected_index = 0
    session.refresh()
    original = session.cached.copy()
    assert session.begin_edit(mark.points[-1])
    session.move_edit(QPointF(300, 120))
    session.end_edit()
    assert session.document.marks[0].points[0] == QPointF(40, 80)
    assert session.document.marks[0].arrow_head == head
    assert session.cached != original
    session.document.undo()
    session.refresh()
    assert session.cached == original
    assert not session.begin_edit(QPointF(120, 20))
    assert session.selected_index is None
    assert not session.begin_edit(mark.points[-1])


def test_toolbar_four_icon_choices_update_latest_arrow_without_resetting_color(app):
    overlay = make_overlay(app)
    try:
        overlay.choose_tool("arrow")
        overlay.choose_color(QColor("#168844"))
        session = overlay.annotation
        session.begin_stroke(QPointF(40, 100))
        session.end_stroke(QPointF(300, 100))
        toolbar = overlay.toolbar
        assert [toolbar.line_type.itemData(i) for i in range(toolbar.line_type.count())] == [
            "line:", "arrow:tapered", "arrow:filled", "arrow:hollow"]
        assert all(not toolbar.line_type.itemText(i) for i in range(4))
        for head in ("tapered", "filled", "hollow"):
            toolbar.line_type.setCurrentIndex(toolbar.line_type.findData("arrow:"+head))
            assert session.selected_index == 0
            assert session.document.marks[0].arrow_head == head
            assert session.color == session.document.marks[0].color == QColor("#168844")
        toolbar.direction.click()
        assert session.document.marks[0].arrow_mode == "reverse"
        toolbar.direction.click()
        assert session.document.marks[0].arrow_mode == "both"
        overlay.choose_tool("line")
        assert not toolbar.direction.isEnabled()
        assert not toolbar.dash.isVisible() and not toolbar.actions["more"].isVisible()
        assert toolbar.main_frame.height() < 40
        assert toolbar.options_scroll.width() < toolbar.main_frame.width()
        # Legacy arrow types remain available from the local advanced parameters.
        overlay.choose_tool("arrow")
        toolbar.choose_style()
        from PySide6.QtWidgets import QComboBox
        heads = next(c for c in toolbar.style_popup.findChildren(QComboBox) if c.findData("diamond") >= 0)
        heads.setCurrentIndex(heads.findData("diamond"))
        assert session.arrow_head == "diamond"
        toolbar.style_popup.close()
    finally:
        dispose(app, overlay)


def test_last_color_and_each_tool_parameters_survive_new_capture_pin_and_restart(app, store):
    overlay = make_overlay(app, store)
    managers = []
    try:
        assert overlay.annotation.color == QColor(255, 0, 4)
        overlay.choose_tool("arrow")
        overlay.choose_color(QColor(12, 88, 180, 200))
        overlay.set_styles({"arrow_head": "hollow", "arrow_mode": "reverse"})
        overlay.set_option("pen_width", 11)
        overlay.choose_tool("text")
        assert overlay.annotation.color == QColor(12, 88, 180, 200)
        overlay.set_styles({"bold": True, "font_family": "Segoe UI"})
        overlay.set_option("font_size", 32)
        overlay.choose_tool("arrow")
        assert overlay.annotation.arrow_head == "hollow" and overlay.annotation.pen_width == 11
        # Reading a new Store verifies persistence independently of the live config.
        reopened = Store(store.root)
        second = make_overlay(app, reopened)
        try:
            second.toolbar.activate_group("lines")
            assert second.annotation.color == QColor(12, 88, 180, 200)
            assert second.annotation.arrow_head == "hollow"
            assert second.annotation.arrow_mode == "reverse" and second.annotation.pen_width == 11
            second.choose_tool("text")
            assert second.annotation.bold and second.annotation.font_size == 32
            assert second.annotation.font_family == "Segoe UI"
            second.choose_color(QColor("#c82030"))
            second.cancel()
            app.processEvents()
        finally:
            from shiboken6 import isValid
            if isValid(second): dispose(app, second)
        reopened = Store(store.root)
        manager = PinManager(reopened, lambda *args: None)
        managers.append(manager)
        image = QImage(360, 200, QImage.Format_ARGB32)
        image.fill(Qt.white)
        pin = manager.create(image)
        pin.start_editing()
        assert pin.annotation.color == QColor("#c82030")
        pin.choose_tool("arrow")
        assert pin.annotation.arrow_head == "hollow" and pin.annotation.pen_width == 11
        pin.set_option("color", QColor("#a05020"))
        pin.end_editing()
        third = make_overlay(app, Store(store.root))
        try:
            third.choose_tool("pen")
            assert third.annotation.color == QColor("#a05020")
        finally: dispose(app, third)
    finally:
        dispose(app, overlay)
        for manager in managers:
            for pin in list(manager.windows): manager.handle(pin, "destroy")
            manager.timer.stop()
        app.processEvents()


@pytest.mark.parametrize("offset", [QPoint(0, 0), QPoint(-800, -200), QPoint(1920, 0)])
def test_escape_confirmation_is_centered_on_capture_region(app, offset):
    overlay = make_overlay(app)
    overlay.move(offset)
    overlay.choose_tool("line")
    overlay.annotation.begin_stroke(QPointF(40, 80))
    overlay.annotation.end_stroke(QPointF(250, 80))
    centers = []
    def inspect():
        prompt = QApplication.activeModalWidget()
        centers.append(prompt.frameGeometry().center())
        prompt.done(QMessageBox.Cancel)
    try:
        QTimer.singleShot(30, inspect)
        QTest.keyClick(overlay, Qt.Key_Escape)
        assert (centers[0]-overlay.global_rect().center()).manhattanLength() <= 1
        assert overlay.isVisible() and len(overlay.annotation.document.marks) == 1
        overlay.annotation.document.marks.clear()
    finally: dispose(app, overlay)
