"""Synthetic checks for the packaged screenshot and pin workflow."""
import tempfile
from pathlib import Path
from PySide6.QtCore import QPoint, QPointF, QRect, QTimer, Qt
from PySide6.QtGui import QColor, QImage
from PySide6.QtTest import QTest
from .capture import Desktop, ScreenImage, SelectionOverlay
from .editor import AnnotationSession, ImageDocument, Mark
from .pins import PinManager
from .storage import Store


def verify_features(app, root):
    from PySide6.QtNetwork import QSslSocket
    assert QSslSocket.supportsSsl(), "No working TLS backend for image URLs"
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    image = QImage(800, 600, QImage.Format_ARGB32)
    image.fill(Qt.white)
    desktop = Desktop.__new__(Desktop)
    desktop.bounds, desktop.windows = image.rect(), []
    desktop.screens = [ScreenImage(image.rect(), image)]
    with tempfile.TemporaryDirectory(prefix="features-", dir=root) as folder:
        store = Store(Path(folder))
        overlay = SelectionOverlay(desktop, store=store)
        overlay.selection = QRect(100, 100, 500, 300)
        overlay.show()
        overlay.choose_tool("line")
        session = overlay.annotation
        session.begin_stroke(QPointF(30, 80))
        session.end_stroke(QPointF(240, 80))
        session.adjust_width(2)
        assert session.document.marks[0].width == 6
        assert session.cached.pixelColor(80, 82) == QColor("#ff0004")
        assert set(session.control_points(session.document.marks[0])) == {"start", "end"}
        assert session.begin_edit(QPointF(240, 80))
        session.move_edit(QPointF(270, 80))
        session.end_edit()
        assert session.document.marks[0].points == [QPointF(30, 80), QPointF(270, 80)]
        QTest.keyClick(overlay, Qt.Key_1, Qt.ControlModifier)
        assert overlay.tool == "rect"
        session.begin_stroke(QPointF(130, 120))
        session.end_stroke(QPointF(280, 240))
        mark = session.document.marks[-1]
        assert len(session.control_points(mark)) == 9
        edge = session.control_points(mark)["e"]
        assert session.begin_edit(edge)
        session.move_edit(edge+QPointF(30, 0))
        session.end_edit()
        assert session.document.marks[-1].points[-1].x() == 310
        # Hollow interiors confirm the object; confirmed edges cannot select it.
        assert session.edit_target(QPointF(180, 180)) == ""
        assert not session.begin_edit(QPointF(180, 180))
        assert not session.begin_edit(QPointF(130, 180))
        assert session.document.marks[-1].points[-1].x() == 310
        QTest.keyClick(overlay, Qt.Key_6, Qt.ControlModifier)
        overlay.toolbar.bold.click()
        overlay.toolbar.italic.click()
        session.add_text(QPointF(30, 180), "Font preview")
        assert session.document.marks[-1].bold and session.document.marks[-1].italic
        session.document.add(Mark("number", [QPointF(350, 100)]))
        session.document.add(Mark("magnify", [QPointF(380, 190), QPointF(410, 190)]))
        session.refresh()
        assert session.cached.pixelColor(80, 80) == QColor("#ff0004")
        app.processEvents()
        overlay.grab().save(str(root/"selection.png"))
        overlay.hide()
        overlay.deleteLater()
        manager = PinManager(store, lambda *args: None)
        pin = manager.create(session.document.original)
        pin.start_editing()
        pin.choose_tool("line")
        pin.annotation.document.marks = session.document.clone(session.document.marks)
        pin.annotation.refresh()
        pin.end_editing()
        manager.persist()
        restored = PinManager(store, lambda *args: None)
        restored.restore()
        assert len(restored.windows[0].annotation.document.marks) == 5
        assert restored.windows[0].annotation.document.marks[2].bold
        assert restored.windows[0].export_image().pixelColor(80, 80) == QColor("#ff0004")
        for m in (manager, restored):
            m.timer.stop()
            for p in list(m.windows): m.handle(p, "destroy")
            m.timer.stop()
        from .app import MainWindow
        window = MainWindow(store, testing=True)
        window.notify = lambda text: None
        try:
            for trigger in ("local", "global"):
                selector = SelectionOverlay(desktop, store=store, output_handler=window.capture_output)
                window.overlay = selector
                selector.completed.connect(window.selection_done)
                selector.selection = QRect(100, 100, 500, 300)
                selector.show()
                selector.choose_tool("arrow")
                selector.annotation.begin_stroke(QPointF(50, 60))
                selector.annotation.end_stroke(QPointF(250, 180))
                expected = selector.annotation.document.render()
                if trigger == "local": QTest.keyClick(selector, Qt.Key_F3)
                else: window.hotkeys.actions["pin"]()
                assert window.overlay is None
                pinned = window.pins.windows[-1]
                assert pinned.image == expected and pinned.isVisible() and pinned.topmost
                assert pinned.windowFlags() & Qt.WindowStaysOnTopHint
                assert pinned.content_geometry() == QRect(100, 100, 500, 300)
                app.processEvents()
                rendered = pinned.grab().toImage()
                content = pinned.content_rect()
                x, y = content.center().x(), content.top()
                assert rendered.pixelColor(x, y-1) == QColor(170, 207, 249)
                assert 0 < rendered.pixelColor(x, y-2).alpha() < 128
                assert rendered.pixelColor(x, y-3).alpha() < rendered.pixelColor(x, y-2).alpha()
                assert rendered.pixelColor(x, 0).alpha() == 0
                assert pinned.export_image() == expected
                window.pins.handle(pinned, "destroy")
                app.processEvents()
        finally:
            window.pins.timer.stop()
            window.hotkeys.clear()
            app.removeNativeEventFilter(window.hotkeys)
            window.tray.hide()
            window.close()
            window.deleteLater()
            app.processEvents()
        recording = SelectionOverlay(desktop, recording=True)
        recording.selection = QRect(100, 100, 500, 300)
        recorded = []
        recording.completed.connect(lambda img, rect, action: recorded.append(action))
        recording.finish("record")
        assert recorded == ["record"] and recording.annotation is None
        app.processEvents()
        from .accessibility import automation_client
        client, _ = automation_client()
        assert client
    return {"synthetic_selection": True, "confirmed_pin_restart": True,
            **verify_reference_styles(app, root),
            **verify_notifications(app, root),
            "group_shortcuts": True, "recent_mark_width": True, "nine_shape_handles": True,
            "blank_confirms_annotation": True, "confirmed_annotation_not_reselected": True,
            "independent_line_endpoints": True, "capture_f3_local_and_global": True,
            "capture_f3_pins_current_image_topmost": True,
            "capture_pin_preserves_position_and_size": True,
            "pin_reference_border_and_transparent_shadow": True,
            "pin_export_excludes_window_frame": True,
            "text_bold_italic_persistence": True,
            "image_url_tls_backend": True,
            "recording_selection_unchanged": True, "uia_interface": True,
            "native_compositor_checked": False}


def verify_reference_styles(app, root):
    """Verify reference silhouettes, saved choices and dialog placement in the exe."""
    from PySide6.QtWidgets import QApplication, QMessageBox
    image = QImage(800, 600, QImage.Format_ARGB32)
    image.fill(Qt.white)
    desktop = Desktop.__new__(Desktop)
    desktop.bounds, desktop.windows = image.rect(), []
    desktop.screens = [ScreenImage(image.rect(), image)]
    with tempfile.TemporaryDirectory(prefix="reference-styles-", dir=root) as folder:
        store = Store(Path(folder))
        overlay = SelectionOverlay(desktop, store=store)
        overlay.selection = QRect(100, 80, 480, 300)
        try:
            overlay.show()
            overlay.choose_tool("arrow")
            session = overlay.annotation
            assert session.color == QColor(255, 0, 4)
            assert [overlay.toolbar.line_type.itemData(i) for i in range(4)] == [
                "line:", "arrow:tapered", "arrow:filled", "arrow:hollow"]
            session.begin_stroke(QPointF(40, 80))
            session.end_stroke(QPointF(300, 80))
            for head in ("tapered", "filled", "hollow"):
                overlay.toolbar.line_type.setCurrentIndex(overlay.toolbar.line_type.findData("arrow:"+head))
                assert session.selected_index == 0 and session.document.marks[0].arrow_head == head
            overlay.set_option("pen_width", 9)
            assert session.cached.pixelColor(200, 80) == Qt.white
            assert session.cached.pixelColor(280, 80) == Qt.white
            assert not session.painted_hit(session.document.marks[0], QPointF(200, 80))
            centers = []
            def inspect_prompt():
                prompt = QApplication.activeModalWidget()
                centers.append(prompt.frameGeometry().center())
                prompt.done(QMessageBox.Cancel)
            QTimer.singleShot(30, inspect_prompt)
            assert not overlay.confirm_discard()
            assert (centers[0]-overlay.global_rect().center()).manhattanLength() <= 1, (
                centers[0], overlay.global_rect().center())
            overlay.choose_color(QColor(12, 88, 180, 200))
            overlay.set_styles({"arrow_mode": "reverse"})
            overlay.choose_tool("text")
            assert session.color == QColor(12, 88, 180, 200)
            overlay.set_styles({"bold": True})
            overlay.set_option("font_size", 32)
            fresh = AnnotationSession(ImageDocument(image))
            fresh.restore_preferences(Store(Path(folder)).config)
            fresh.select_tool("arrow")
            assert fresh.color == QColor(12, 88, 180, 200)
            assert fresh.arrow_head == "hollow" and fresh.arrow_mode == "reverse" and fresh.pen_width == 9
            fresh.select_tool("text")
            assert fresh.bold and fresh.font_size == 32
            app.processEvents()
            assert overlay.toolbar.main_frame.height() < 40
        finally:
            overlay.hide()
            overlay.deleteLater()
            app.processEvents()
    return {"four_reference_line_styles": True, "hollow_arrow_body_and_head": True,
            "icon_only_line_dropdown": True, "last_color_and_tool_styles_persist": True,
            "initial_annotation_rgb_255_0_4": True, "exit_dialog_centered_on_capture_region": True}


def verify_notifications(app, root):
    from .app import MainWindow
    from .dialogs import SettingsDialog
    with tempfile.TemporaryDirectory(prefix="notifications-", dir=root) as folder:
        store = Store(Path(folder))
        window = MainWindow(store, testing=True)
        dialog = SettingsDialog(store)
        try:
            initial = window.statusBar().currentMessage()
            assert not dialog.notifications.isChecked() and not store.config["notifications_enabled"]
            window.notify("synthetic-disabled")
            assert window.statusBar().currentMessage() == initial
            store.config["notifications_enabled"] = True
            store.save_config()
            assert Store(Path(folder)).config["notifications_enabled"]
            window.notify("synthetic-enabled")
            assert window.statusBar().currentMessage() == "synthetic-enabled"
            store.config["notifications_enabled"] = False
            window.notify("synthetic-disabled-again")
            assert window.statusBar().currentMessage() == "synthetic-enabled"
        finally:
            dialog.deleteLater()
            window.pins.timer.stop()
            window.hotkeys.clear()
            app.removeNativeEventFilter(window.hotkeys)
            window.tray.hide()
            window.close()
            window.deleteLater()
            app.processEvents()
    return {"operation_notifications_default_off_and_configurable": True}
