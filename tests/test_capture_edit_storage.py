from pathlib import Path

import pytest
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest

from floatshot.capture import Desktop, ScreenImage, SelectionOverlay
from floatshot.editor import ImageCanvas, ImageDocument, Mark
from floatshot.pins import PinManager


def solid(width, height, color):
    image = QImage(width, height, QImage.Format_ARGB32)
    image.fill(QColor(color))
    return image


def test_crop_mixed_dpi_and_negative_monitor_origin(app):
    desktop = Desktop.__new__(Desktop)
    desktop.screens = [ScreenImage(QRect(-100, 0, 100, 100), solid(100, 100, "red")),
                       ScreenImage(QRect(0, 0, 100, 100), solid(200, 200, "blue"))]
    desktop.bounds = QRect(-100, 0, 200, 100)
    result = desktop.crop(QRect(-20, 10, 40, 30))
    assert (result.width(), result.height()) == (80, 60)
    assert result.pixelColor(39, 20) == QColor("red")
    assert result.pixelColor(40, 20) == QColor("blue")


def test_selection_export_and_keyboard_adjustments(app):
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(0, 0, 640, 400)
    desktop.screens = [ScreenImage(desktop.bounds, solid(640, 400, "#294a63"))]
    desktop.windows = []
    overlay = SelectionOverlay(desktop)
    received = []
    overlay.completed.connect(lambda image, rect, action: received.append((image, rect, action)))
    overlay.show()
    QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(50, 50))
    QTest.mouseMove(overlay, QPoint(249, 169))
    QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(249, 169))
    QTest.keyClick(overlay, Qt.Key_Right)
    QTest.keyClick(overlay, Qt.Key_Return)
    assert received[0][2] == "copy"
    assert received[0][1] == QRect(51, 50, 200, 120)
    assert received[0][0].size().width() == 200
    app.processEvents()


@pytest.mark.parametrize("recording", [False, True])
def test_selection_feedback_is_visible_during_drag_and_after_release(app, monkeypatch, recording):
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(0, 0, 640, 400)
    background = QColor("#f4f1ec")
    desktop.screens = [ScreenImage(desktop.bounds, solid(640, 400, background))]
    desktop.windows = []

    def reject_exclusion(widget):
        pytest.fail("The selection UI must remain visible in remote/captured desktops")

    monkeypatch.setattr("floatshot.native.exclude_from_capture", reject_exclusion)
    overlay = SelectionOverlay(desktop, recording=recording)
    try:
        overlay.show()
        QTest.mousePress(overlay, Qt.LeftButton, pos=QPoint(80, 80))
        QTest.mouseMove(overlay, QPoint(379, 239))

        def assert_feedback():
            image = overlay.grab().toImage()
            assert image.pixelColor(200, 150) == background
            assert image.pixelColor(80, 150) == QColor("#48d6be" if recording else "#1688ef")
            assert image.pixelColor(40, 150).lightness() < background.lightness() - 60
            assert overlay.selection == QRect(80, 80, 300, 160)

        assert_feedback()
        assert not overlay.toolbar.isVisible()
        QTest.mouseRelease(overlay, Qt.LeftButton, pos=QPoint(379, 239))
        assert_feedback()
        assert overlay.toolbar.isVisible()
        exported = desktop.crop(overlay.global_rect())
        assert exported.pixelColor(0, 0) == background
        assert exported.pixelColor(299, 159) == background
    finally:
        overlay.close()
        overlay.deleteLater()
        app.processEvents()


def test_annotations_redaction_undo_and_eraser(app):
    original = solid(120, 80, "white")
    for x in range(20, 60):
        for y in range(20, 60):
            original.setPixelColor(x, y, QColor("black" if (x+y) % 2 else "white"))
    doc = ImageDocument(original)
    mark = Mark("line", [QPointF(10, 10), QPointF(90, 10)], QColor("red"), 6)
    doc.add(mark)
    assert doc.render().pixelColor(40, 10).red() == 255
    assert doc.render().pixelColor(40, 10).green() < 10
    doc.add(Mark("eraser", [QPointF(40, 8), QPointF(40, 14)], width=6))
    assert doc.render().pixelColor(40, 10) == QColor("white")
    doc.undo()
    assert doc.render().pixelColor(40, 10).green() < 10
    doc.redo()
    assert doc.render().pixelColor(40, 10) == QColor("white")
    doc.add(Mark("mosaic", [QPointF(20, 20), QPointF(60, 60)], width=8))
    pixels = {doc.render().pixelColor(x, 35).name() for x in range(22, 58)}
    assert len(pixels) == 1
    doc.add(Mark("blur", [QPointF(20, 20), QPointF(60, 60)], width=4))
    assert not doc.render().isNull()


def test_canvas_drag_creates_one_undoable_mark(app):
    canvas = ImageCanvas(ImageDocument(solid(200, 120, "white")))
    canvas.show()
    QTest.mousePress(canvas, Qt.LeftButton, pos=QPoint(10, 10))
    QTest.mouseMove(canvas, QPoint(100, 70))
    QTest.mouseRelease(canvas, Qt.LeftButton, pos=QPoint(100, 70))
    assert len(canvas.document.marks) == 1
    canvas.document.undo()
    assert canvas.document.render() == canvas.document.original
    canvas.close()


def test_history_pruning_never_removes_exports(store, tmp_path):
    store.config["history_limit"] = 1
    user_file = tmp_path / "user-image.png"
    user_file.write_bytes(b"must survive")
    first = store.add_image(solid(40, 40, "red"), str(user_file))
    store.add_image(solid(40, 40, "blue"))
    assert not Path(first["path"]).exists()
    assert user_file.read_bytes() == b"must survive"
    store.remove_record(store.history[0]["id"])
    assert user_file.exists()


def test_pin_states_and_workspace_restore(app, store):
    manager = PinManager(store, lambda pin, action: None)
    pin = manager.create(solid(100, 80, "green"))
    pin.move(100, 120)
    pin.set_alpha(.5)
    manager.persist()
    assert store.workspace()[0]["alpha"] == .5
    manager.toggle_all()
    assert pin.state == "hidden"
    manager.persist()
    restored = PinManager(store, lambda pin, action: None)
    restored.restore()
    assert restored.windows[0].state == "hidden"
    assert restored.windows[0].alpha == .5
    manager.handle(pin, "close")
    assert pin.state == "closed"
    assert manager.restore_closed()
    assert pin.state == "visible"
    manager.handle(pin, "destroy")
    assert not manager.windows
    assert not (store.pins_dir / f"{pin.key}.png").exists()
    for window in restored.windows:
        window.hide()
        window.deleteLater()
    app.processEvents()
