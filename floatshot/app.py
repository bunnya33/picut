from __future__ import annotations

import argparse
import hashlib
import logging
import json
import shlex
import sys
import time
from pathlib import Path

from PySide6.QtCore import QPoint, QProcess, QRect, QSize, QTimer, QUrl, Qt
from PySide6.QtGui import QColor, QDesktopServices, QGuiApplication, QIcon, QImage, QPainter, QPixmap
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWidgets import (QApplication, QDialog, QFileDialog, QFrame, QHBoxLayout, QLabel,
                               QListWidget, QListWidgetItem, QMainWindow, QMenu, QMessageBox,
                               QStackedWidget, QSystemTrayIcon, QVBoxLayout, QWidget)

from . import __version__, native
from .capture import Desktop, SelectionOverlay
from .dialogs import RecordSetup, RecordingBar, SettingsDialog, VideoPreview
from .editor import EditorDialog
from .pins import PinManager
from .image_tools import color_card, parse_color, text_card
from .recorder import RecordOptions, Recorder
from .storage import Store
from .theme import STYLE, app_icon, button, label, load_fonts


class MainWindow(QMainWindow):
    def __init__(self, store, testing=False):
        super().__init__()
        self.store = store
        self.testing = testing
        self.overlay = None
        self.selection_pending = False
        self.setup_active = False
        self.launch_pending = False
        self.editor = None
        last_region = store.config.get("last_capture_rect", [])
        from PySide6.QtCore import QRect
        self.last_rect = QRect(*last_region) if len(last_region) == 4 else None
        self.record_bar = None
        self.countdown_dialog = None
        self.countdown_timer = QTimer(self)
        self.countdown_timer.setInterval(1000)
        self.countdown_timer.timeout.connect(self.countdown_tick)
        self.pins = PinManager(store, self.pin_action)
        from .import_images import ImageDownloader
        self.image_downloader = ImageDownloader(self)
        self.image_downloader.loaded.connect(lambda image: self.pins.create(image))
        self.image_downloader.failed.connect(self.notify)
        self.recorder = Recorder(store.temp, store.cache)
        self.recorder.started.connect(self.record_started)
        self.recorder.status.connect(self.record_status)
        self.recorder.warning.connect(self.notify)
        self.recorder.finished.connect(self.record_finished)
        self.recorder.failed.connect(self.record_failed)
        self.setWindowTitle(f"浮光 FloatShot · {__version__}")
        self.setWindowIcon(app_icon())
        self.setMinimumSize(860, 610)
        self.resize(1060, 760)
        self.build_ui()
        self.build_tray()
        self.hotkeys = native.Hotkeys({
            "capture": self.begin_capture, "pin": self.pin_clipboard, "toggle_pins": self.pins.toggle_all,
            "record": self.begin_record, "pause": self.pause_record, "stop": self.stop_record,
            "through": self.pins.toggle_through_at_cursor,
            "interrupt": lambda: self.interrupt_record("系统锁屏、断开会话或进入休眠。"),
        })
        QApplication.instance().installNativeEventFilter(self.hotkeys)
        from .triggers import DesktopCorners, SuperSnip
        self.super_snip = SuperSnip(self)
        self.super_snip.allowed = lambda: not self.recorder.active and not self.overlay and not self.selection_pending and not self.setup_active
        self.super_snip.selected.connect(lambda rect: self.execute_command(
            f"snip --region {rect.x()} {rect.y()} {rect.width()} {rect.height()} -o clipboard"), Qt.QueuedConnection)
        self.corners = DesktopCorners(self, store.config, lambda: not testing and not self.recorder.active and
                                     not self.overlay and not self.selection_pending and not self.setup_active)
        self.corners.triggered.connect(self.execute_command)
        if not testing:
            native.watch_session(self)
            self.register_hotkeys()
            if store.config["restore_pins"]:
                self.pins.restore()
        QGuiApplication.instance().screenRemoved.connect(lambda screen: self.interrupt_record("显示器已断开。"))
        self.refresh_history()

    def build_ui(self):
        root = QWidget()
        layout = QHBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName("sidebar")
        sidebar.setFixedWidth(190)
        nav = QVBoxLayout(sidebar)
        nav.setContentsMargins(18, 28, 18, 20)
        icon = QLabel()
        icon.setPixmap(app_icon().pixmap(44, 44))
        nav.addWidget(icon)
        nav.addWidget(label("浮光", "brand"))
        nav.addWidget(label("FLOATSHOT", "muted"))
        nav.addSpacing(30)
        self.pages = QStackedWidget()
        self.nav_buttons = []
        for index, text in enumerate(("工作台", "历史记录")):
            control = button(text, lambda checked=False, i=index: self.navigate(i))
            control.setObjectName("nav")
            control.setCheckable(True)
            control.setChecked(index == 0)
            self.nav_buttons.append(control)
            nav.addWidget(control)
        settings = button("设置", self.open_settings)
        settings.setObjectName("nav")
        nav.addWidget(settings)
        nav.addStretch()
        nav.addWidget(label("本地保存\n无需账号", "muted"))
        nav.addSpacing(12)
        nav.addWidget(label(f"Qt 6  /  v{__version__}", "muted"))
        layout.addWidget(sidebar)
        home = QWidget()
        home_layout = QVBoxLayout(home)
        home_layout.setContentsMargins(32, 26, 32, 26)
        header = QHBoxLayout()
        header.addWidget(label("把灵感留在屏幕上", "title"))
        header.addStretch()
        header.addWidget(label("桌面工作助手", "badge"))
        home_layout.addLayout(header)
        home_layout.addWidget(label("截图、标注、贴图与录屏，快捷键一步调用。", "subtitle"))
        home_layout.addSpacing(18)
        cards = QHBoxLayout()
        for title, shortcut, text, action in (
            ("截图与标注", "F1", "框选屏幕内容，标注重点，\n复制、保存或贴到桌面。", self.begin_capture),
            ("桌面贴图", "F3", "让参考图保持可见，\n自由调整大小和透明度。", self.pin_clipboard)):
            card = QFrame()
            card.setObjectName("card")
            box = QVBoxLayout(card)
            box.setContentsMargins(20, 18, 20, 18)
            box.addWidget(label(shortcut, "badge"), 0, Qt.AlignLeft)
            heading = label(title)
            heading.setStyleSheet("font-size: 20px; font-weight: 600; padding-top: 8px;")
            box.addWidget(heading)
            box.addWidget(label(text, "subtitle"))
            box.addStretch()
            box.addWidget(button("开始截图" if shortcut == "F1" else "贴出剪贴板", action, True))
            cards.addWidget(card, 1)
        home_layout.addLayout(cards, 2)
        home_layout.addSpacing(12)
        record_card = QFrame()
        record_card.setObjectName("card")
        record_layout = QHBoxLayout(record_card)
        record_layout.setContentsMargins(22, 20, 22, 20)
        copy = QVBoxLayout()
        copy.addWidget(label("录下每一个操作", "brand"))
        copy.addWidget(label("MP4  ·  系统声音 + 麦克风  ·  可暂停  ·  无水印", "subtitle"))
        self.record_info = label("Ctrl + Alt + R 开始录屏", "muted")
        copy.addWidget(self.record_info)
        record_layout.addLayout(copy, 1)
        actions = QVBoxLayout()
        actions.addWidget(button("区域录屏", self.begin_record, True))
        actions.addWidget(button("整屏录制", self.begin_fullscreen_record))
        record_layout.addLayout(actions)
        home_layout.addWidget(record_card)
        home_layout.addSpacing(12)
        recent_header = QHBoxLayout()
        recent_header.addWidget(label("最近捕获"))
        recent_header.addStretch()
        recent_header.addWidget(button("查看全部 →", lambda: self.navigate(1)))
        home_layout.addLayout(recent_header)
        self.recent_list = QListWidget()
        self.recent_list.setIconSize(QPixmap(80, 52).size())
        self.recent_list.setMaximumHeight(170)
        self.recent_list.itemDoubleClicked.connect(self.open_item)
        home_layout.addWidget(self.recent_list, 1)
        home_layout.addWidget(label("关闭窗口后常驻托盘  ·  Ctrl+Alt+T 鼠标穿透  ·  快捷键可在设置中修改", "muted"))
        self.pages.addWidget(home)
        history = QWidget()
        history_layout = QVBoxLayout(history)
        history_layout.setContentsMargins(32, 26, 32, 26)
        history_layout.addWidget(label("历史记录", "title"))
        history_layout.addWidget(label("重新打开截图与录屏。移除记录不会删除另存的原文件。", "subtitle"))
        self.history_list = QListWidget()
        self.history_list.setIconSize(QPixmap(116, 70).size())
        self.history_list.itemDoubleClicked.connect(self.open_item)
        history_layout.addWidget(self.history_list, 1)
        row = QHBoxLayout()
        for title, action in (("打开", "open"), ("复制", "copy"), ("贴图", "pin"),
                              ("文件目录", "folder"), ("移除记录", "remove")):
            row.addWidget(button(title, lambda checked=False, a=action: self.history_action(a), action == "open"))
        row.addStretch()
        history_layout.addLayout(row)
        self.pages.addWidget(history)
        layout.addWidget(self.pages, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage("就绪 · 文件仅保存在本地")

    def build_tray(self):
        self.tray = QSystemTrayIcon(app_icon(), self)
        self.tray.setToolTip("浮光 FloatShot · 截图 / 贴图 / 录屏")
        menu = QMenu()
        for title, callback in (("打开工作台", self.show_home), ("截图", self.begin_capture),
                                ("从剪贴板贴图", self.pin_clipboard), ("区域录屏", self.begin_record),
                                ("整屏录制", self.begin_fullscreen_record), ("暂停/继续录制", self.pause_record),
                                ("停止录制", self.stop_record)):
            menu.addAction(title, callback)
        menu.addSeparator()
        for title, callback in (("显示/隐藏全部贴图", self.pins.toggle_all),
                                ("恢复最近关闭的贴图", self.pins.restore_closed),
                                ("解除全部鼠标穿透", self.pins.clear_through),
                                ("找回屏幕外贴图", self.pins.find_lost),
                                ("历史记录", lambda: self.navigate(1)), ("设置", self.open_settings)):
            menu.addAction(title, callback)
        menu.addSeparator()
        groups = menu.addMenu("贴图分组")
        def refresh_groups():
            groups.clear()
            for name in self.store.config.get("pin_groups", ["默认"]):
                action = groups.addAction(name, lambda checked=False, n=name: self.pins.set_group(n))
                action.setCheckable(True)
                action.setChecked(name == self.pins.active_group)
            groups.addSeparator()
            groups.addAction("新建分组…", self.pins.add_group)
            groups.addAction("管理分组…", self.pins.manage_groups)
        groups.aboutToShow.connect(refresh_groups)
        menu.addAction("退出", self.request_quit)
        self.tray_menu = menu
        self.tray.setContextMenu(menu)
        self.tray.activated.connect(self.tray_action)
        if not self.testing:
            self.tray.show()

    def tray_action(self, reason):
        if reason == QSystemTrayIcon.Trigger:
            self.execute_command(self.store.config.get("tray_left", "snip"))
        elif reason == QSystemTrayIcon.MiddleClick:
            self.execute_command(self.store.config.get("tray_middle", "paste"))
        elif reason == QSystemTrayIcon.DoubleClick:
            self.show_home()
        elif reason == QSystemTrayIcon.Context and self.store.config.get("tray_right"):
            self.execute_command(self.store.config["tray_right"])

    def navigate(self, index):
        self.pages.setCurrentIndex(index)
        for number, control in enumerate(self.nav_buttons):
            control.setChecked(number == index)
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def show_home(self):
        self.navigate(0)

    def notify(self, text):
        if not self.store.config.get("notifications_enabled", False): return
        self.statusBar().showMessage(text, 12000)
        if self.tray.isVisible() and not self.isVisible():
            self.tray.showMessage("浮光", text, QSystemTrayIcon.Information, 4000)

    def register_hotkeys(self):
        bindings = dict(self.store.config["hotkeys"])
        self.hotkeys.ignored = {}
        for i, item in enumerate(self.store.config.get("custom_commands", [])):
            if not item.get("hotkey"): continue
            name = f"command_{i}"
            bindings[name] = item["hotkey"]
            self.hotkeys.actions[name] = lambda c=item["command"]: self.execute_command(c)
            self.hotkeys.ignored[name] = item.get("ignore", "")
        failed = self.hotkeys.register(bindings)
        self.tray.setContextMenu(None if self.store.config.get("tray_right") else self.tray_menu)
        if not self.super_snip.enable(self.store.config.get("super_snip", False)): failed.append("Win+拖拽手势不可用")
        if failed:
            self.notify("快捷键冲突："+"、".join(failed)+"。仍可通过工作台或托盘使用。")

    def begin_capture(self):
        self.begin_selection(False)

    def execute_command(self, command):
        from .commands import parse_command
        try:
            args = parse_command(command)
            if args.group: self.pins.set_group(args.group)
            callbacks = {"paste": self.pin_clipboard, "toggle": self.pins.toggle_all, "restore": self.pins.restore_closed,
                         "home": self.show_home, "history": lambda: self.navigate(1), "settings": self.open_settings,
                         "through": self.pins.toggle_through_at_cursor}
            if args.action != "snip": callbacks[args.action](); return
            if self.recorder.active or self.overlay or self.selection_pending or self.setup_active or self.launch_pending or self.countdown_dialog:
                self.notify("当前截图或录屏流程进行中。")
                return
            if not (args.last or args.active or args.full or args.region):
                self.begin_capture()
                return
            rectangle = (self.last_rect if args.last else native.foreground_rect() if args.active else
                         QRect(*args.region) if args.region else None)
            if args.last and not rectangle:
                self.notify("还没有上一次截图区域。")
                return
            self.hide()
            self.selection_pending = True
            def capture():
                self.selection_pending = False
                try:
                    desktop = Desktop(include_cursor=self.store.config["capture_cursor"], detect_elements=False)
                    image = desktop.crop(rectangle or desktop.bounds)
                    if image.isNull(): raise ValueError("截图区域为空。")
                    if args.file:
                        self.save_image(image, Path(args.file))
                        self.store.add_image(image, args.file)
                        self.refresh_history()
                    else: self.image_output(image, f"send:{args.send}" if args.send is not None else
                                            {"clipboard": "copy", "file": "quick_save", "pin": "pin"}[args.output])
                    self.last_rect = rectangle or desktop.bounds
                    self.store.config["last_capture_rect"] = list(self.last_rect.getRect())
                    self.store.save_config()
                except Exception as error: self.notify(str(error))
            QTimer.singleShot(130+round(args.delay*1000), capture)
        except (ValueError, argparse.ArgumentError, SystemExit) as error:
            self.notify(f"命令执行失败：{error}")

    def begin_record(self):
        if self.recorder.active:
            if self.record_bar:
                self.record_bar.show()
                self.record_bar.raise_()
            return
        self.begin_selection(True)

    def begin_selection(self, recording):
        if self.recorder.active or self.countdown_dialog or self.launch_pending or self.setup_active:
            self.notify("正在录屏，请先停止录制。")
            return
        if self.overlay:
            self.overlay.raise_()
            return
        if self.selection_pending:
            return
        self.selection_pending = True
        self.selection_requested_at = time.perf_counter()
        settle = 130 if recording or self.isVisible() else 0
        self.hide()
        # Wait for the window compositor to remove our home window before freezing the desktop.
        delay = 0 if recording else self.store.config["capture_delay"]*1000
        QTimer.singleShot(settle+delay, lambda: self.show_selection(recording))

    def show_selection(self, recording):
        self.selection_pending = False
        try:
            history = [item for item in self.store.history if item["kind"] == "image"]
            self.overlay = SelectionOverlay(recording=recording, last_rect=self.last_rect, history=history,
                                            store=self.store, output_handler=self.capture_output)
            self.overlay.completed.connect(self.selection_done)
            self.overlay.cancelled.connect(self.selection_cancelled)
            self.overlay.show()
            logging.info("Selection ready: %.1f ms (recording=%s)",
                         (time.perf_counter()-getattr(self, "selection_requested_at", time.perf_counter()))*1000, recording)
        except Exception as exc:
            self.overlay = None
            self.show_home()
            QMessageBox.warning(self, "无法截图", str(exc))

    def selection_cancelled(self):
        self.overlay = None
        self.refresh_history()

    def selection_done(self, image, rect, action):
        self.overlay = None
        self.last_rect = rect
        if action != "record":
            self.store.config["last_capture_rect"] = list(rect.getRect())
            self.store.save_config()
        if action == "record":
            self.prepare_record(rect)

    def open_editor(self, image, pin=None):
        dialog = EditorDialog(image, pin_edit=pin is not None)
        self.editor = dialog
        dialog.output.connect(lambda result, action: self.editor_output(dialog, result, action, pin))
        dialog.show()
        dialog.raise_()
        dialog.activateWindow()

    def editor_output(self, dialog, image, action, pin):
        if action == "apply" and pin:
            pin.replace_image(image)
            dialog.close()
        elif self.image_output(image, action):
            dialog.close()

    def capture_output(self, image, action):
        pin_rect = None
        if action == "pin" and self.overlay is not None and not self.overlay.recording:
            region = self.overlay.global_rect()
            source = self.overlay.annotation.document.original if self.overlay.annotation else image
            sx, sy = region.width()/source.width(), region.height()/source.height()
            # Optional output shadow adds symmetric padding in image pixels.
            # Keep the original screenshot pixels aligned with the selected area.
            offset = QPoint(round((image.width()-source.width())/2*sx),
                            round((image.height()-source.height())/2*sy))
            pin_rect = QRect(region.topLeft()-offset,
                             QSize(round(image.width()*sx), round(image.height()*sy)))
        return self.image_output(image, action, pin_rect=pin_rect)

    def image_output(self, image, action, pin_rect=None):
        if action.startswith("send:"):
            try:
                item = self.store.config.get("external_apps", [])[int(action.partition(":")[2])]
                path = self.store.output_path(self.store.cache, "发送", "png")
                self.save_image(image, path)
                arguments = [argument.replace("%file%", str(path)) for argument in shlex.split(item.get("arguments", "%file%"))]
                started, _ = QProcess.startDetached(item["program"], arguments)
                if not started: raise OSError("无法启动目标应用，请检查程序路径。")
                self.store.add_image(image)
                self.refresh_history()
                return True
            except (OSError, ValueError, IndexError, KeyError) as error:
                QMessageBox.warning(self, "发送失败", str(error))
                return False
        try:
            output = ""
            if action == "save":
                default = self.store.output_path(self.store.config["image_dir"], "截图", "png")
                target, selected_filter = QFileDialog.getSaveFileName(self, "保存截图", str(default),
                                        "PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;WebP 图片 (*.webp)")
                if not target:
                    return False
                if not Path(target).suffix:
                    target += ".jpg" if selected_filter.startswith("JPEG") else ".webp" if selected_filter.startswith("WebP") else ".png"
                self.save_image(image, Path(target))
                output = target
            elif action == "quick_save":
                target = self.store.output_path(self.store.config["image_dir"], "截图", "png")
                self.save_image(image, target)
                output = str(target)
            elif action == "copy":
                if self.store.config["copy_image_as_file"]:
                    from PySide6.QtCore import QMimeData
                    path = self.store.cache / "clipboard.png"
                    self.save_image(image, path)
                    mime = QMimeData()
                    mime.setImageData(image)
                    mime.setUrls([QUrl.fromLocalFile(str(path))])
                    QApplication.clipboard().setMimeData(mime)
                else:
                    QApplication.clipboard().setImage(image)
            elif action == "pin":
                self.pins.create(image, source_rect=pin_rect)
            if self.store.config["auto_save"] and not output:
                target = self.store.output_path(self.store.config["image_dir"], "截图", "png")
                self.save_image(image, target)
                output = str(target)
            self.store.add_image(image, output)
            self.refresh_history()
            self.notify({"copy": "截图已复制", "pin": "已贴到桌面", "save": "截图已保存",
                         "quick_save": "截图已快速保存"}.get(action, "操作完成"))
            return True
        except OSError as exc:
            QMessageBox.warning(self, "输出失败", str(exc))
            return False

    @staticmethod
    def save_image(image, target):
        result = image
        if target.suffix.lower() in (".jpg", ".jpeg"):
            result = QImage(image.size(), QImage.Format_RGB32)
            result.fill(Qt.white)
            painter = QPainter(result)
            painter.drawImage(0, 0, image)
            painter.end()
        if not result.save(str(target), quality=95):
            raise OSError(f"保存失败，请检查路径和文件格式：{target}")

    def pin_clipboard(self):
        # RegisterHotKey consumes F3 before Qt shortcuts see it. The global
        # action must share the screenshot's local pin behavior.
        if self.overlay is not None and not self.overlay.recording:
            if self.overlay.isVisible(): self.overlay.finish("pin")
            return
        clipboard = QApplication.clipboard()
        mime = clipboard.mimeData()
        signature = self.clipboard_signature(mime)
        if signature == getattr(self, "last_pin_clipboard", None) and self.pins.restore_closed():
            return
        image = clipboard.image()
        if image.isNull():
            mime = clipboard.mimeData()
            if mime.hasUrls():
                local_paths = [url.toLocalFile() for url in mime.urls() if url.isLocalFile()]
                for url in mime.urls():
                    if url.isLocalFile():
                        image = QImage(url.toLocalFile()) if signature != getattr(self, "last_pin_clipboard", None) else QImage()
                        if not image.isNull(): break
                if image.isNull() and local_paths and self.store.config.get("pin_file_paths", True):
                    image = text_card("\n".join(local_paths))
            if image.isNull():
                from .import_images import image_url
                url = image_url(mime)
                if not url.isEmpty():
                    self.image_downloader.load(url)
                    self.last_pin_clipboard = signature
                    return
            if image.isNull() and clipboard.text().strip():
                text = clipboard.text().strip()[:5000]
                color = parse_color(text)
                image = color_card(color) if color.isValid() else text_card(mime.html() if mime.hasHtml() else text, mime.hasHtml())
        if not image.isNull():
            pin = self.pins.create(image)
            color = parse_color(clipboard.text())
            if color.isValid(): pin.color_value = color.name(QColor.HexArgb)
            self.last_pin_clipboard = signature
            self.notify("剪贴板内容已贴到桌面")
        elif not self.pins.restore_closed():
            self.notify("剪贴板中没有可贴出的图片或文字。先截图或复制一张图片。")

    @staticmethod
    def clipboard_signature(mime):
        digest = hashlib.sha256()
        if mime:
            for name in sorted(mime.formats()):
                digest.update(name.encode())
                digest.update(bytes(mime.data(name)))
        return digest.hexdigest()

    def pin_action(self, pin, action):
        if action == "edit":
            pin.start_editing()
        elif action == "save_decorated":
            from .image_tools import decorate_image
            self.image_output(decorate_image(pin.export_image()), "save")
        else:
            self.image_output(pin.export_image(), action)

    def begin_fullscreen_record(self):
        if not self.recorder.active and not self.countdown_dialog and not self.setup_active and not self.launch_pending:
            self.prepare_record()

    def prepare_record(self, rect=None):
        if rect:
            try:
                native.to_physical(rect)
            except ValueError as exc:
                QMessageBox.warning(self, "录制区域", str(exc))
                return
        setup = RecordSetup(self.store, rect, self)
        self.setup_active = True
        result = setup.exec()
        self.setup_active = False
        if result != QDialog.Accepted:
            return
        setup.save_options()
        if rect is None:
            screen = next((screen for screen in QGuiApplication.screens() if screen.name() == setup.screen.currentData()),
                          QGuiApplication.primaryScreen())
            rect = screen.geometry()
        try:
            region = native.to_physical(rect)
            path = self.store.output_path(self.store.config["video_dir"], "录屏", "mp4")
            c = self.store.config
            self.pending_record = RecordOptions(region, path, c["fps"], c["quality"], c["cursor"],
                                                 c["system_audio"], c["microphone"], c["speaker_id"], c["mic_id"])
            self.record_rect = rect
            self.hide()
            self.countdown_remaining = c["countdown"]
            if self.countdown_remaining:
                self.countdown_dialog = QDialog(None, Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
                layout = QVBoxLayout(self.countdown_dialog)
                self.countdown_label = label(str(self.countdown_remaining))
                self.countdown_label.setAlignment(Qt.AlignCenter)
                self.countdown_label.setStyleSheet("font-size: 64px; font-weight: 700; padding: 25px 60px;")
                layout.addWidget(self.countdown_label)
                layout.addWidget(button("取消录制", self.cancel_countdown))
                self.countdown_dialog.finished.connect(self.cancel_countdown)
                self.countdown_dialog.show()
                native.exclude_from_capture(self.countdown_dialog)
                self.countdown_timer.start()
            else:
                self.launch_pending = True
                QTimer.singleShot(150, self.launch_record)
        except Exception as exc:
            QMessageBox.warning(self, "无法开始录制", str(exc))

    def countdown_tick(self):
        self.countdown_remaining -= 1
        if self.countdown_remaining <= 0:
            self.countdown_timer.stop()
            if self.countdown_dialog:
                self.countdown_dialog.finished.disconnect()
                self.countdown_dialog.close()
                self.countdown_dialog.deleteLater()
                self.countdown_dialog = None
            QTimer.singleShot(130, self.launch_record)
            self.launch_pending = True
        else:
            self.countdown_label.setText(str(self.countdown_remaining))

    def cancel_countdown(self):
        self.countdown_timer.stop()
        dialog, self.countdown_dialog = self.countdown_dialog, None
        if dialog:
            dialog.finished.disconnect()
            dialog.close()
            dialog.deleteLater()
        self.notify("已取消录制")

    def launch_record(self):
        if not self.launch_pending:
            return
        self.launch_pending = False
        try:
            self.recorder.start(self.pending_record)
            self.record_info.setText("正在启动录制…")
        except Exception as exc:
            self.record_failed(str(exc))

    def record_started(self):
        self.record_bar = RecordingBar(self.recorder)
        self.record_bar.pause_requested.connect(self.pause_record)
        self.record_bar.stop_requested.connect(self.stop_record)
        self.record_bar.mute_requested.connect(self.recorder.set_muted)
        screen = QGuiApplication.screenAt(self.record_rect.center()) or QGuiApplication.primaryScreen()
        available = screen.availableGeometry()
        self.record_bar.move(available.center().x()-self.record_bar.width()//2, available.top()+15)
        self.record_bar.show()
        self.record_info.setText("正在录制 · Ctrl+Alt+P 暂停 · Ctrl+Alt+S 停止")

    def pause_record(self):
        self.recorder.pause()
        if self.record_bar:
            self.record_bar.refresh()

    def stop_record(self):
        if self.countdown_dialog:
            self.cancel_countdown()
        elif self.launch_pending:
            self.launch_pending = False
            self.notify("已取消录制")
        else:
            self.recorder.stop()

    def interrupt_record(self, reason):
        if self.recorder.active:
            self.notify(reason+"正在保存已录制内容。")
            self.recorder.stop()

    def record_status(self, text):
        self.record_info.setText(text)
        if self.record_bar:
            self.record_bar.time_label.setText(text)
            self.record_bar.pause_button.setEnabled(False)

    def clear_record_ui(self):
        if self.record_bar:
            self.record_bar.close()
            self.record_bar.deleteLater()
            self.record_bar = None
        self.record_info.setText("Ctrl + Alt + R 开始录屏")

    def record_finished(self, result):
        self.clear_record_ui()
        item = self.store.add_video(result["path"], result["duration"], result["width"], result["height"], result["thumbnail"])
        self.refresh_history()
        self.notify("录屏已保存")
        self.preview = VideoPreview(item, self)
        self.preview.show()
        self.preview.raise_()

    def record_failed(self, text):
        self.clear_record_ui()
        self.show_home()
        QMessageBox.warning(self, "录屏未完成", text)

    def refresh_history(self):
        for widget, records in ((self.history_list, self.store.history), (self.recent_list, self.store.history[:3])):
            widget.clear()
            for item in records:
                video = item["kind"] == "video"
                exists = Path(item["path"]).is_file()
                title = ("录屏" if video else "截图")+f"  ·  {item['created'].replace('T', ' ')}"
                detail = f"{item['width']} × {item['height']}"
                if video: detail += f"  ·  {item['duration']:.1f} 秒"
                if not exists: detail += "  ·  原文件已移动或删除"
                path = item.get("thumbnail", "") if video else item["path"]
                row = QListWidgetItem(QIcon(path), title+"\n"+detail)
                row.setData(Qt.UserRole, item)
                widget.addItem(row)
            if not records:
                empty = QListWidgetItem("还没有记录。截一张图，或录下一个操作吧。")
                empty.setFlags(Qt.NoItemFlags)
                widget.addItem(empty)

    def open_item(self, row):
        item = row.data(Qt.UserRole)
        if not item:
            return
        if not Path(item["path"]).is_file():
            self.notify("原文件已移动或删除。可移除这条失效记录。")
            return
        if item["kind"] == "video":
            self.preview = VideoPreview(item, self)
            self.preview.show()
        else:
            self.open_editor(QImage(item["path"]))

    def history_action(self, action):
        row = self.history_list.currentItem()
        item = row.data(Qt.UserRole) if row else None
        if not item:
            self.notify("先选择一条历史记录。")
            return
        if action == "remove":
            self.store.remove_record(item["id"])
            self.refresh_history()
            return
        if not Path(item["path"]).is_file():
            self.notify("原文件不存在。")
            return
        if action == "open":
            self.open_item(row)
        elif action == "folder":
            path = Path(item.get("output") or item["path"])
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(path.parent)))
        elif action == "pin":
            if item["kind"] == "image":
                self.pins.create(QImage(item["path"]))
            else:
                self.notify("首版只支持图片贴图。")
        elif action == "copy":
            if item["kind"] == "image":
                QApplication.clipboard().setImage(QImage(item["path"]))
            else:
                from PySide6.QtCore import QMimeData
                mime = QMimeData()
                mime.setUrls([QUrl.fromLocalFile(item["path"])])
                QApplication.clipboard().setMimeData(mime)
            self.notify("已复制")

    def open_settings(self):
        dialog = SettingsDialog(self.store, self)
        if dialog.exec() == QDialog.Accepted:
            self.register_hotkeys()
            self.refresh_history()
            if not self.store.config.get("notifications_enabled", False): self.statusBar().clearMessage()
            self.notify("设置已保存")

    def closeEvent(self, event):
        if self.testing:
            event.accept()
            return
        event.ignore()
        self.hide()
        self.notify("已收起到托盘，双击托盘图标可打开工作台。")

    def request_quit(self):
        if self.recorder.active or self.countdown_dialog or self.launch_pending:
            QMessageBox.information(self, "录制进行中", "请先停止录制并等待保存完成，再退出应用。")
            return
        self.pins.persist()
        self.hotkeys.clear()
        self.super_snip.close()
        self.corners.timer.stop()
        self.tray.hide()
        QApplication.instance().quit()


def main():
    parser = argparse.ArgumentParser(description="浮光 FloatShot")
    parser.add_argument("--tray", action="store_true", help="Start in the system tray")
    parser.add_argument("--smoke-test", action="store_true", help="Render the home window and exit")
    parser.add_argument("--verify-backend", action="store_true", help="Run explicit short screen/audio/codec verification and exit")
    parser.add_argument("--verify-recognition", action="store_true", help="Verify OCR and QR with synthetic images")
    parser.add_argument("--verify-features", action="store_true", help="Verify synthetic screenshot/pin workflows")
    parser.add_argument("--command", help='Run a screenshot/pin command, e.g. "snip --last -o clipboard"')
    args = parser.parse_args()
    native.enable_dpi()
    app = QApplication(sys.argv[:1])
    app.setApplicationName("FloatShot")
    app.setOrganizationName("FloatShot")
    app.setApplicationVersion(__version__)
    app.setQuitOnLastWindowClosed(False)
    app.setStyle("Fusion")
    load_fonts()
    app.setStyleSheet(STYLE)
    app.setWindowIcon(app_icon())
    store = Store()
    from logging.handlers import RotatingFileHandler
    handler = RotatingFileHandler(store.root / "app.log", maxBytes=1024*1024, backupCount=2, encoding="utf-8")
    logging.basicConfig(level=logging.INFO, handlers=[handler])
    if args.verify_features:
        from .feature_checks import verify_features
        from .storage import atomic_json
        try:
            report = verify_features(app, store.root / "features")
            atomic_json(store.root / "feature-verification.json", {"passed": True, **report})
            return 0
        except Exception as error:
            logging.exception("Feature verification failed")
            atomic_json(store.root / "feature-verification.json", {"passed": False, "error": str(error)})
            return 1
    if args.verify_recognition:
        from .recognition import verify_recognition
        from .storage import atomic_json
        try:
            report = verify_recognition()
            atomic_json(store.root / "recognition-verification.json", {"passed": True, **report})
            return 0
        except Exception as error:
            atomic_json(store.root / "recognition-verification.json", {"passed": False, "error": str(error)})
            return 1
    if args.verify_backend:
        from .diagnostics import verify_backend
        from .storage import atomic_json
        try:
            report = verify_backend(app, store.root / "verification")
            atomic_json(store.root / "verification.json", {"passed": True, **report})
            return 0
        except Exception as error:
            logging.exception("Backend verification failed")
            atomic_json(store.root / "verification.json", {"passed": False, "error": str(error)})
            return 1
    def unhandled(kind, error, traceback):
        logging.error("Unhandled exception", exc_info=(kind, error, traceback))
        QMessageBox.warning(None, "浮光", f"操作未完成：{error}\n诊断信息已保存到 {store.root / 'app.log'}")
    sys.excepthook = unhandled
    lock = "FloatShot-"+hashlib.sha256(str(store.root).encode()).hexdigest()[:16]
    server = QLocalServer(app)
    if not args.smoke_test:
        socket = QLocalSocket()
        socket.connectToServer(lock)
        if socket.waitForConnected(250):
            socket.write(json.dumps({"command": args.command or "home"}).encode()+b"\n")
            socket.flush()
            socket.waitForBytesWritten(250)
            return 0
        QLocalServer.removeServer(lock)
        if not server.listen(lock):
            return 1
    window = MainWindow(store, testing=args.smoke_test)
    def activate_existing():
        connection = server.nextPendingConnection()
        if connection:
            def received():
                if not connection.canReadLine(): return
                try:
                    payload = json.loads(bytes(connection.readLine()))
                    window.execute_command(payload["command"])
                except (ValueError, KeyError, TypeError): window.show_home()
                connection.disconnectFromServer()
            connection.readyRead.connect(received)
            connection.disconnected.connect(connection.deleteLater)
            received()
    server.newConnection.connect(activate_existing)
    if (not args.tray and not args.command) or args.smoke_test:
        window.show()
    if args.command: QTimer.singleShot(0, lambda: window.execute_command(args.command))
    if args.smoke_test:
        def complete():
            destination = Path.cwd() / ".test-artifacts"
            destination.mkdir(exist_ok=True)
            window.grab().save(str(destination / "home.png"))
            app.quit()
        QTimer.singleShot(500, complete)
    return app.exec()
