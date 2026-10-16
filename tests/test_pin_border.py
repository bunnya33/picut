from PySide6.QtCore import QPoint, QRect, Qt
from PySide6.QtGui import QColor, QImage, QPainter
from PySide6.QtWidgets import QWidget

from floatshot.pins import PinManager, PinWindow


def dispose(app, pin):
    pin.hide()
    pin.deleteLater()
    app.processEvents()


def test_pin_renders_reference_border_and_fading_transparent_shadow(app):
    image = QImage(96, 72, QImage.Format_ARGB32)
    image.fill(QColor("#edf2f6"))
    pin = PinWindow(image, source_rect=QRect(100, 100, 96, 72))
    try:
        pin.show()
        app.processEvents()
        rendered = pin.grab().toImage()
        content = pin.content_rect()
        x, y = content.center().x(), content.top()
        assert rendered.copy(content).convertToFormat(image.format()) == image
        assert rendered.pixelColor(x, y-1) == QColor(170, 207, 249)
        alphas = [rendered.pixelColor(x, y-distance).alpha() for distance in range(2, 12)]
        assert 0 < alphas[0] < 128
        assert all(a >= b for a, b in zip(alphas, alphas[1:]))
        assert alphas[-1] == 0
        assert rendered.pixelColor(0, content.center().y()).alpha() == 0
        assert rendered.pixelColor(0, 0).alpha() == 0
        # The same soft outline must appear on both horizontal and vertical edges.
        for distance in range(1, 11):
            assert rendered.pixelColor(x, y-distance) == rendered.pixelColor(content.left()-distance, content.center().y())
            assert rendered.pixelColor(x, y-distance) == rendered.pixelColor(x, content.bottom()+distance)
        white = QImage(rendered.size(), QImage.Format_ARGB32)
        white.fill(Qt.white)
        painter = QPainter(white)
        painter.drawImage(QPoint(), rendered)
        painter.end()
        # Reference screenshot: the first shadow row over white is RGB 194,220,251.
        sample = white.pixelColor(x, y-2)
        assert max(abs(a-b) for a, b in zip(sample.getRgb()[:3], (194, 220, 251))) <= 1
        assert pin.export_image() == image
    finally:
        dispose(app, pin)


def test_pin_focus_does_not_change_the_reference_border(app):
    image = QImage(96, 72, QImage.Format_ARGB32)
    image.fill(Qt.white)
    pin, other = PinWindow(image), QWidget()
    try:
        pin.show()
        pin.activateWindow()
        pin.setFocus()
        app.processEvents()
        focused = pin.grab().toImage()
        other.show()
        other.activateWindow()
        other.setFocus()
        app.processEvents()
        assert not pin.hasFocus()
        assert pin.grab().toImage() == focused
    finally:
        dispose(app, pin)
        dispose(app, other)


def test_legacy_workspace_keeps_image_origin_and_new_layout_roundtrips(app, store):
    image = QImage(96, 72, QImage.Format_ARGB32)
    image.fill(Qt.white)
    manager = PinManager(store, lambda *args: None)
    restored = PinManager(store, lambda *args: None)
    try:
        # v0.4.3 saved window coordinates with a 2 px margin around the image.
        pin = manager.create(image, {"id": "legacy", "x": 98, "y": 78, "scale": 1})
        assert pin.content_geometry() == QRect(100, 80, 96, 72)
        manager.persist()
        data = store.workspace()[0]
        assert data["frame_margin"] == pin.FRAME_MARGIN
        restored.restore()
        assert restored.windows[0].content_geometry() == QRect(100, 80, 96, 72)
        assert restored.windows[0].export_image() == image
    finally:
        for owner in (manager, restored):
            for pin in list(owner.windows):
                owner.handle(pin, "destroy")
            owner.timer.stop()
        app.processEvents()


def test_transparent_image_mask_preserves_outline_and_pixel_passthrough(app):
    image = QImage(96, 72, QImage.Format_ARGB32)
    image.fill(Qt.transparent)
    image.setPixelColor(48, 36, Qt.red)
    pin = PinWindow(image, source_rect=QRect(100, 100, 96, 72))
    try:
        pin.background_mode = "透明"
        pin.update_input_mask()
        content = pin.content_rect()
        assert pin.mask().contains(content.topLeft()+QPoint(48, 36))
        assert not pin.mask().contains(content.topLeft()+QPoint(20, 20))
        assert pin.mask().contains(QPoint(content.center().x(), content.top()-1))
        assert pin.mask().contains(QPoint(content.center().x(), content.top()-2))
        assert pin.export_image() == image
    finally:
        dispose(app, pin)
