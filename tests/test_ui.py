from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest

from floatshot.app import MainWindow
from floatshot.dialogs import RecordingBar
from floatshot.editor import EditorDialog
from floatshot.recorder import RecordOptions, Recorder


def test_home_clipboard_history_and_image_pin(app, store, tmp_path):
    window = MainWindow(store, testing=True)
    image = QImage(160, 100, QImage.Format_ARGB32)
    image.fill(QColor("#26baa1"))
    assert window.image_output(image, "copy")
    assert app.clipboard().image().pixelColor(80, 40) == QColor("#26baa1")
    window.pin_clipboard()
    assert len(window.pins.windows) == 1
    assert len(store.history) == 1
    window.history_list.setCurrentRow(0)
    window.history_action("remove")
    assert len(store.history) == 0
    transparent = QImage(30, 30, QImage.Format_ARGB32)
    transparent.fill(Qt.transparent)
    path = tmp_path / "transparent.jpg"
    window.save_image(transparent, path)
    assert QImage(str(path)).pixelColor(15, 15).lightness() > 245
    for pin in list(window.pins.windows):
        window.pins.handle(pin, "destroy")
    window.hotkeys.clear()
    app.removeNativeEventFilter(window.hotkeys)
    window.close()
    window.deleteLater()
    app.processEvents()


def test_record_controls_emit_actions_without_clicked_boolean(app, tmp_path):
    recorder = Recorder(tmp_path, tmp_path)
    recorder.options = RecordOptions({}, tmp_path / "output.mp4")
    recorder.state = "recording"
    bar = RecordingBar(recorder)
    actions = []
    bar.pause_requested.connect(lambda: actions.append("pause"))
    bar.stop_requested.connect(lambda: actions.append("stop"))
    bar.show()
    QTest.mouseClick(bar.pause_button, Qt.LeftButton)
    from PySide6.QtWidgets import QPushButton
    stop = next(control for control in bar.findChildren(QPushButton) if control.text() == "停止")
    QTest.mouseClick(stop, Qt.LeftButton)
    assert actions == ["pause", "stop"]
    bar.close()
    bar.deleteLater()


def test_editor_and_home_render_with_chinese_fonts(app, store):
    from floatshot.theme import label
    image = QImage(800, 430, QImage.Format_ARGB32)
    image.fill(QColor("#e7eff3"))
    from PySide6.QtCore import QPointF
    from floatshot.editor import Mark
    editor = EditorDialog(image)
    editor.document.add(Mark("rect", [QPointF(70, 60), QPointF(380, 200)], QColor("#178b75"), 4))
    editor.document.add(Mark("arrow", [QPointF(600, 320), QPointF(380, 210)], QColor("#f45b69"), 6))
    editor.document.add(Mark("text", [QPointF(90, 90)], QColor("#183548"), text="浮光 · 截图与标注", font_size=26))
    editor.canvas.refresh()
    editor.show()
    home = MainWindow(store, testing=True)
    home.show()
    app.processEvents()
    folder = Path(".test-artifacts")
    folder.mkdir(exist_ok=True)
    editor.grab().save(str(folder / "editor.png"))
    home.grab().save(str(folder / "home.png"))
    assert editor.size().width() >= 1000
    editor.close()
    home.hotkeys.clear()
    app.removeNativeEventFilter(home.hotkeys)
    home.close()
    home.deleteLater()
    app.processEvents()
