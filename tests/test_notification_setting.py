from floatshot.app import MainWindow
from floatshot.dialogs import SettingsDialog
from floatshot.storage import Store


def test_notifications_default_off_saved_setting_enables_and_disables_output(app, store, monkeypatch):
    monkeypatch.setattr("floatshot.dialogs.native.set_startup", lambda value: None)
    window = MainWindow(store, testing=True)
    sent = []
    monkeypatch.setattr(window.tray, "isVisible", lambda: True)
    monkeypatch.setattr(window.tray, "showMessage", lambda *args: sent.append(args[1]))
    dialog = SettingsDialog(store)
    try:
        initial = window.statusBar().currentMessage()
        assert not dialog.notifications.isChecked()
        window.notify("截图已复制")
        assert not sent and window.statusBar().currentMessage() == initial
        dialog.notifications.setChecked(True)
        dialog.save()
        assert Store(store.root).config["notifications_enabled"] is True
        window.notify("已贴到桌面")
        assert sent == ["已贴到桌面"]
        assert window.statusBar().currentMessage() == "已贴到桌面"
        second = SettingsDialog(store)
        try:
            assert second.notifications.isChecked()
            second.notifications.setChecked(False)
            second.save()
        finally:
            second.deleteLater()
        assert Store(store.root).config["notifications_enabled"] is False
        window.notify("截图已保存")
        assert sent == ["已贴到桌面"]
        assert window.statusBar().currentMessage() == "已贴到桌面"
    finally:
        dialog.deleteLater()
        window.pins.timer.stop()
        window.hotkeys.clear()
        app.removeNativeEventFilter(window.hotkeys)
        window.tray.hide()
        window.close()
        window.deleteLater()
        app.processEvents()
