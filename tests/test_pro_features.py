import json
from PySide6.QtCore import QPoint, QPointF, QRect, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest
from floatshot.editor import AnnotationSession, ImageDocument, Mark
from floatshot.pins import PinManager
from floatshot.commands import parse_command
from floatshot.native import program_ignored
from floatshot.image_tools import decorate_image


def document():
    image = QImage(320, 240, QImage.Format_ARGB32)
    image.fill(Qt.white)
    return ImageDocument(image)


def test_styles_number_and_magnifier_render_without_ui_in_exports(app):
    doc = document()
    session = AnnotationSession(doc)
    session.select_tool("number")
    session.begin_stroke(QPointF(50, 50))
    session.end_stroke(QPointF(50, 50))
    session.begin_stroke(QPointF(120, 50))
    session.end_stroke(QPointF(120, 50))
    assert [m.number for m in doc.marks] == [1, 2]
    assert session.number == 3
    doc.add(Mark("rect", [QPointF(20, 100), QPointF(160, 200)], radius=30, dash="dash"))
    assert doc.render().pixelColor(20, 100) == QColor("white")
    doc.add(Mark("magnify", [QPointF(250, 100), QPointF(280, 100)]))
    image = doc.render()
    assert image.pixelColor(250, 100) == QColor("white")
    assert image.pixelColor(280, 100) != QColor("white")
    assert Mark.from_dict(doc.marks[-1].to_dict()).magnification == 2


def test_text_background_outline_and_resize_rotation_undo(app):
    doc = document()
    session = AnnotationSession(doc)
    session.select_tool("text")
    session.text_background, session.text_outline, session.outline_width = "#ff0000ff", "#ff000000", 2
    session.add_text(QPointF(50, 50), "Text")
    assert doc.render().pixelColor(48, 50) == QColor("blue")
    assert session.begin_edit(QPointF(60, 60))
    session.end_edit()
    box = session.bounds(doc.marks[0])
    assert session.begin_edit(box.bottomRight())
    session.move_edit(box.center()+(box.bottomRight()-box.center())*1.5)
    session.end_edit()
    assert doc.marks[0].font_size == 33
    doc.undo()
    assert doc.marks[0].font_size == 22


def test_object_copy_paste_is_editable_and_free_selection_crosses_tools(app):
    doc = document()
    session = AnnotationSession(doc)
    doc.add(Mark("rect", [QPointF(50, 50), QPointF(120, 120)], radius=12))
    session.select_tool("objects")
    assert session.begin_edit(QPointF(50, 80))
    session.end_edit()
    assert session.copy_selected() and session.paste_selected()
    assert len(doc.marks) == 2
    assert doc.marks[1].points[0] == QPointF(66, 66)
    session.delete_selected()
    assert len(doc.marks) == 1
    doc.undo()
    assert len(doc.marks) == 2


def test_pin_restart_retains_original_and_confirmed_annotation(app, store):
    manager = PinManager(store, lambda *args: None)
    pin = manager.create(document().original)
    pin.start_editing()
    pin.annotation.document.add(Mark("line", [QPointF(30, 100), QPointF(200, 100)]))
    pin.annotation.refresh()
    pin.end_editing()
    manager.persist()
    assert QImage(str(store.pins_dir/f"{pin.key}.png")).pixelColor(50, 100) == QColor("white")
    restored_manager = PinManager(store, lambda *args: None)
    restored_manager.restore()
    restored = restored_manager.windows[0]
    assert restored.export_image().pixelColor(50, 100) == QColor("#ff0004")
    restored.start_editing()
    assert restored.annotation.begin_edit(QPointF(50, 100)) is False  # default tool is arrow
    restored.choose_tool("line")
    assert not restored.annotation.begin_edit(QPointF(50, 100))
    restored.annotation.begin_stroke(QPointF(30, 150))
    restored.annotation.end_stroke(QPointF(200, 150))
    assert restored.annotation.selected_index == 1
    restored.annotation.delete_selected()
    assert restored.export_image().pixelColor(50, 100) == QColor("#ff0004")
    assert restored.export_image().pixelColor(50, 150) == QColor("white")
    for m in (manager, restored_manager):
        for p in list(m.windows): m.handle(p, "destroy")


def test_thumbnail_scales_visible_region_and_gray_preserves_transparency(app, store):
    image = document().original
    image.setPixelColor(5, 5, Qt.transparent)
    manager = PinManager(store, lambda *args: None)
    pin = manager.create(image)
    pin.thumbnail_rect = QRect(50, 60, 100, 80)
    pin.set_scale(2)
    assert pin.content_rect().width() == 200 and pin.content_rect().height() == 160
    assert pin.export_image().size() == pin.thumbnail_rect.size()
    manager.handle(pin, "reset")
    pin.gray = True
    assert pin.export_image().pixelColor(5, 5).alpha() == 0
    manager.handle(pin, "background:透明")
    assert not pin.mask().contains(pin.content_rect().topLeft()+QPoint(5, 5))
    manager.handle(pin, "destroy")


def test_hidden_group_does_not_reappear_when_through_or_topmost_changes(app, store):
    manager = PinManager(store, lambda *args: None)
    pin = manager.create(document().original)
    manager.handle(pin, "group:参考")
    assert not pin.isVisible()
    manager.handle(pin, "through")
    manager.handle(pin, "topmost")
    assert not pin.isVisible()
    manager.handle(pin, "destroy")


def test_commands_validate_and_ignore_program_patterns():
    import pytest
    args = parse_command('snip --last -o clipboard --delay 2')
    assert args.last and args.delay == 2
    assert parse_command('snip --file "C:\\Pictures\\shot.png" --region 10 20 300 200').file == r"C:\Pictures\shot.png"
    for value in ("unknown", "snip --delay -1", "snip --bogus", "snip -o bad", ""):
        with pytest.raises(ValueError): parse_command(value)
    assert program_ignored(r"C:\Games\app.exe", "editor.exe;games")
    assert program_ignored(r"C:\Tools\editor.exe", "*.exe")
    assert not program_ignored(r"C:\Tools\app.exe", "editor.exe")


def test_command_capture_file_saves_image_history_and_last_region(app, store, monkeypatch, tmp_path):
    from floatshot.app import MainWindow
    from floatshot.capture import Desktop, ScreenImage
    import floatshot.app as app_module
    desktop = Desktop.__new__(Desktop)
    desktop.bounds = QRect(0, 0, 320, 240)
    desktop.windows = []
    desktop.screens = [ScreenImage(desktop.bounds, document().original)]
    monkeypatch.setattr(app_module, "Desktop", lambda **kwargs: desktop)
    window = MainWindow(store, testing=True)
    target = tmp_path/"command.png"
    try:
        window.execute_command(f'snip --region 20 30 100 80 --file "{target.as_posix()}"')
        for _ in range(100):
            if target.is_file(): break
            QTest.qWait(10)
        assert target.is_file()
        assert QImage(str(target)).size().width() == 100
        assert store.history[0]["output"] == str(target.as_posix())
        assert window.last_rect == QRect(20, 30, 100, 80)
    finally:
        app.removeNativeEventFilter(window.hotkeys)
        window.corners.timer.stop()
        window.super_snip.close()
        window.deleteLater()
        app.processEvents()


def test_decoration_preserves_pixels_and_adds_alpha_shadow():
    image = document().original
    result = decorate_image(image)
    assert result.width() == image.width()+36
    assert result.pixelColor(30, 30) == QColor("white")
    assert result.pixelColor(0, 0).alpha() < 10
    assert result.pixelColor(15, 100).alpha() > 0


def test_cancelled_selection_can_be_replayed_without_touching_recording(app, store):
    from test_inline_capture import make_overlay
    store.config["keep_all_capture_history"] = True
    overlay = make_overlay(app, store)
    overlay.cancel()
    assert len(store.history) == 1 and store.history[0]["kind"] == "image"
    app.processEvents()


def test_web_image_downloader_imports_async_from_explicit_url(app, tmp_path):
    import threading
    from http.server import BaseHTTPRequestHandler, HTTPServer
    from PySide6.QtCore import QUrl
    from floatshot.import_images import ImageDownloader
    path = tmp_path/"download.png"
    document().original.save(str(path))
    body = path.read_bytes()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *args): pass
    server = HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    downloader = ImageDownloader(None)
    received, errors = [], []
    downloader.loaded.connect(received.append)
    downloader.failed.connect(errors.append)
    try:
        downloader.load(QUrl(f"http://127.0.0.1:{server.server_port}/image.png"))
        for _ in range(50):
            if received or errors: break
            QTest.qWait(20)
        assert not errors and len(received) == 1
        assert received[0].size() == document().original.size()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(2)
        downloader.deleteLater()
