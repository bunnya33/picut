from __future__ import annotations

import ctypes
import threading
from pathlib import Path

import numpy as np
from PySide6.QtCore import QObject, QTimer, QUrl, Qt, Signal
from PySide6.QtGui import QDesktopServices, QGuiApplication, QKeySequence
from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
from PySide6.QtMultimediaWidgets import QVideoWidget
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QDialog, QFileDialog, QFormLayout,
                               QHBoxLayout, QKeySequenceEdit, QLabel, QLineEdit, QMessageBox,
                               QProgressBar, QScrollArea, QSlider, QSpinBox, QTabWidget, QTableWidget,
                               QTableWidgetItem, QVBoxLayout, QWidget)

from . import native
from .recorder import audio_devices
from .theme import button, label


class AudioProbe(QObject):
    level = Signal(float)
    error = Signal(str)

    def __init__(self, kind, identifier):
        super().__init__()
        self.kind, self.identifier = kind, identifier
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        if native.WINDOWS:
            ctypes.windll.ole32.CoInitializeEx(None, 0)
        try:
            import soundcard as sc
            device = sc.get_microphone(self.identifier, include_loopback=self.kind == "system")
            with device.recorder(samplerate=48000, blocksize=960) as stream:
                while not self.stopped.is_set():
                    samples = stream.record(numframes=None)
                    if samples is not None and len(samples):
                        self.level.emit(min(1.0, float(np.sqrt(np.mean(samples*samples)))*4))
                    else:
                        self.stopped.wait(.02)
        except Exception as exc:
            self.error.emit(str(exc))
        finally:
            if native.WINDOWS:
                ctypes.windll.ole32.CoUninitialize()

    def stop(self):
        self.stopped.set()


class RecordSetup(QDialog):
    def __init__(self, store, region=None, parent=None):
        super().__init__(parent)
        self.store = store
        self.region = region
        self.probes = []
        self.current_probes = {}
        self.setWindowTitle("浮光 · 准备录屏")
        self.setMinimumWidth(540)
        root = QVBoxLayout(self)
        root.addWidget(label("准备录屏", "title"))
        root.addWidget(label("画面和声音保存在本地，停止后即可预览。", "subtitle"))
        form = QFormLayout()
        self.screen = QComboBox()
        for screen in QGuiApplication.screens():
            self.screen.addItem(f"{screen.name()} · {screen.geometry().width()} × {screen.geometry().height()}", screen.name())
        form.addRow("录制目标", label(f"已框选 {region.width()} × {region.height()} 逻辑像素") if region else self.screen)
        self.system = QCheckBox("录入系统声音")
        self.system.setChecked(store.config["system_audio"])
        self.speaker = QComboBox()
        self.microphone = QCheckBox("录入麦克风")
        self.microphone.setChecked(store.config["microphone"])
        self.mic = QComboBox()
        self.system_level = QProgressBar()
        self.mic_level = QProgressBar()
        for meter in (self.system_level, self.mic_level):
            meter.setRange(0, 100)
            meter.setValue(0)
            meter.setTextVisible(False)
            meter.setMaximumHeight(6)
        self.audio_message = label("未开启的声音不会被录入。", "muted")
        try:
            speakers, microphones = audio_devices()
            for key, name in speakers:
                self.speaker.addItem(name, key)
            for key, name in microphones:
                self.mic.addItem(name, key)
            for combo, key in ((self.speaker, "speaker_id"), (self.mic, "mic_id")):
                index = combo.findData(store.config[key])
                if index >= 0: combo.setCurrentIndex(index)
            if not speakers:
                self.system.setChecked(False)
                self.system.setEnabled(False)
                self.speaker.addItem("未检测到声音输出设备")
            if not microphones:
                self.microphone.setChecked(False)
                self.microphone.setEnabled(False)
                self.mic.addItem("未检测到麦克风")
                self.audio_message.setText("未检测到麦克风，可连接后重新打开此窗口。")
        except Exception as exc:
            self.system.setChecked(False)
            self.microphone.setChecked(False)
            self.audio_message.setText(f"声音设备读取失败：{exc}")
        form.addRow(self.system)
        form.addRow("声音输出设备", self.speaker)
        form.addRow("声音电平", self.system_level)
        form.addRow(self.microphone)
        form.addRow("麦克风设备", self.mic)
        form.addRow("麦克风电平", self.mic_level)
        form.addRow(self.audio_message)
        self.cursor = QCheckBox("显示鼠标指针")
        self.cursor.setChecked(store.config["cursor"])
        form.addRow(self.cursor)
        self.fps = QComboBox()
        self.fps.addItem("30 fps · 推荐", 30)
        self.fps.addItem("15 fps · 更小文件", 15)
        self.fps.setCurrentIndex(1 if store.config["fps"] == 15 else 0)
        form.addRow("帧率", self.fps)
        self.quality = QComboBox()
        for name, value in (("清晰 · 推荐", 22), ("更清晰", 18), ("更小文件", 28)):
            self.quality.addItem(name, value)
        self.quality.setCurrentIndex(max(0, self.quality.findData(store.config["quality"])))
        form.addRow("画质", self.quality)
        self.countdown = QSpinBox()
        self.countdown.setRange(0, 10)
        self.countdown.setSuffix(" 秒")
        self.countdown.setValue(store.config["countdown"])
        form.addRow("倒计时", self.countdown)
        root.addLayout(form)
        root.addWidget(label("MP4 / H.264  ·  最高 1080p  ·  无水印", "badge"))
        root.addWidget(label(f"保存到：{store.config['video_dir']}", "muted"))
        actions = QHBoxLayout()
        actions.addStretch()
        actions.addWidget(button("取消", self.reject))
        actions.addWidget(button("开始录制", self.accept, True))
        root.addLayout(actions)
        self.system.toggled.connect(lambda: self.start_probe("system"))
        self.microphone.toggled.connect(lambda: self.start_probe("mic"))
        self.speaker.currentIndexChanged.connect(lambda: self.start_probe("system"))
        self.mic.currentIndexChanged.connect(lambda: self.start_probe("mic"))
        self.start_probe("system")
        self.start_probe("mic")
        self.finished.connect(self.stop_probes)

    def start_probe(self, kind):
        previous = self.current_probes.pop(kind, None)
        if previous:
            previous.stop()
        checked, combo, meter = (self.system, self.speaker, self.system_level) if kind == "system" else (self.microphone, self.mic, self.mic_level)
        combo.setEnabled(checked.isChecked())
        meter.setValue(0)
        if checked.isChecked() and combo.currentData():
            probe = AudioProbe(kind, combo.currentData())
            probe.level.connect(lambda level: meter.setValue(round(level*100)))
            probe.error.connect(lambda message: self.audio_message.setText(f"设备预览不可用：{message}"))
            self.probes.append(probe)
            self.current_probes[kind] = probe

    def stop_probes(self):
        for probe in self.probes:
            probe.stop()

    def save_options(self):
        for key, value in {"system_audio": self.system.isChecked(), "microphone": self.microphone.isChecked(),
                           "speaker_id": self.speaker.currentData() or "", "mic_id": self.mic.currentData() or "",
                           "cursor": self.cursor.isChecked(), "fps": self.fps.currentData(),
                           "quality": self.quality.currentData(), "countdown": self.countdown.value()}.items():
            self.store.config[key] = value
        self.store.save_config()


class RecordingBar(QWidget):
    pause_requested = Signal()
    stop_requested = Signal()
    mute_requested = Signal(str, bool)

    def __init__(self, recorder):
        super().__init__(None, Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.recorder = recorder
        self.setObjectName("recordingBar")
        self.setStyleSheet("QWidget#recordingBar { background: #152831; border: 1px solid #48d6be; border-radius: 10px; }")
        row = QHBoxLayout(self)
        self.time_label = label("● 00:00", "badge")
        row.addWidget(self.time_label)
        self.pause_button = button("暂停", self.pause_requested.emit)
        row.addWidget(self.pause_button)
        row.addWidget(button("停止", self.stop_requested.emit, True))
        self.audio_buttons = {}
        for kind, title, enabled in (("system", "系统", recorder.options.system_audio),
                                     ("mic", "麦克风", recorder.options.microphone)):
            control = button(title)
            control.setCheckable(True)
            control.setEnabled(enabled)
            control.setToolTip("点击静音 / 取消静音")
            control.toggled.connect(lambda checked, k=kind: self.mute_requested.emit(k, checked))
            row.addWidget(control)
            self.audio_buttons[kind] = control
        self.adjustSize()
        self.drag_offset = None
        self.timer = QTimer(self)
        self.timer.setInterval(150)
        self.timer.timeout.connect(self.refresh)
        self.timer.start()

    def showEvent(self, event):
        super().showEvent(event)
        if not native.exclude_from_capture(self):
            self.setToolTip("若系统无法排除此控制条，请把它移出录屏区域。")

    def refresh(self):
        if self.recorder.state == "finalizing":
            return
        seconds = int(self.recorder.clock.elapsed())
        self.time_label.setText(f"{'Ⅱ' if self.recorder.state == 'paused' else '●'} {seconds//60:02}:{seconds%60:02}")
        self.pause_button.setText("继续" if self.recorder.state == "paused" else "暂停")
        self.pause_button.setEnabled(self.recorder.state in ("paused", "recording"))
        for kind, control in self.audio_buttons.items():
            track = next((t for t in self.recorder.tracks if t.kind == kind), None)
            if track:
                control.setText(f"{'静音' if track.muted.is_set() else '▮'*max(1, round(track.level*4))} {'系统' if kind == 'system' else '麦克风'}")

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.drag_offset = event.globalPosition().toPoint()-self.pos()

    def mouseMoveEvent(self, event):
        if self.drag_offset is not None:
            self.move(event.globalPosition().toPoint()-self.drag_offset)

    def mouseReleaseEvent(self, event):
        self.drag_offset = None


class VideoPreview(QDialog):
    def __init__(self, item, parent=None):
        super().__init__(parent)
        self.setWindowTitle("浮光 · 录屏预览")
        self.setAttribute(Qt.WA_DeleteOnClose)
        self.resize(900, 650)
        self.path = Path(item["path"])
        self.player = QMediaPlayer(self)
        self.audio = QAudioOutput(self)
        self.player.setAudioOutput(self.audio)
        video = QVideoWidget()
        self.player.setVideoOutput(video)
        root = QVBoxLayout(self)
        root.addWidget(label("录屏已保存", "title"))
        root.addWidget(label(f"{self.path.name}  ·  {item['width']} × {item['height']}  ·  {item['duration']:.1f} 秒", "subtitle"))
        root.addWidget(video, 1)
        self.slider = QSlider(Qt.Horizontal)
        self.player.durationChanged.connect(lambda duration: self.slider.setRange(0, duration))
        self.player.positionChanged.connect(self.slider.setValue)
        self.slider.sliderMoved.connect(self.player.setPosition)
        root.addWidget(self.slider)
        row = QHBoxLayout()
        row.addWidget(button("播放 / 暂停", self.toggle))
        row.addStretch()
        row.addWidget(button("打开目录", lambda: QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.path.parent)))))
        row.addWidget(button("另存为", self.save_as))
        row.addWidget(button("复制文件", self.copy_file, True))
        root.addLayout(row)
        self.message = label(str(self.path), "muted")
        root.addWidget(self.message)
        self.player.errorOccurred.connect(lambda error, text: self.message.setText(f"播放失败：{text}。可打开文件使用系统播放器。"))
        self.player.setSource(QUrl.fromLocalFile(str(self.path)))
        self.finished.connect(self.release_media)

    def release_media(self):
        self.player.stop()
        self.player.setSource(QUrl())

    def toggle(self):
        self.player.pause() if self.player.playbackState() == QMediaPlayer.PlayingState else self.player.play()

    def copy_file(self):
        from PySide6.QtCore import QMimeData
        mime = QMimeData()
        mime.setUrls([QUrl.fromLocalFile(str(self.path))])
        QApplication.clipboard().setMimeData(mime)
        self.message.setText("视频文件已复制，可在资源管理器或支持文件粘贴的应用中粘贴。")

    def save_as(self):
        target, _ = QFileDialog.getSaveFileName(self, "另存视频", str(self.path), "MP4 视频 (*.mp4)")
        if target and Path(target).resolve() != self.path.resolve():
            try:
                import shutil
                shutil.copy2(self.path, target)
                self.message.setText(f"已保存到 {target}")
            except OSError as exc:
                QMessageBox.warning(self, "保存失败", str(exc))


class SettingsDialog(QDialog):
    def __init__(self, store, parent=None):
        super().__init__(parent)
        self.store = store
        self.setWindowTitle("浮光 · 设置")
        self.resize(670, 600)
        root = QVBoxLayout(self)
        root.addWidget(label("设置", "title"))
        tabs = QTabWidget()
        general = QWidget()
        general.setObjectName("scrollContent")
        form = QFormLayout(general)
        self.restore = QCheckBox("启动时恢复贴图与布局")
        self.restore.setChecked(store.config["restore_pins"])
        form.addRow(self.restore)
        self.startup = QCheckBox("开机启动，常驻托盘")
        self.startup.setChecked(store.config["startup"])
        form.addRow(self.startup)
        self.auto_save = QCheckBox("完成截图后自动保存到图片目录")
        self.auto_save.setChecked(store.config["auto_save"])
        form.addRow(self.auto_save)
        self.notifications = QCheckBox("显示操作通知（截图、贴图、保存等）")
        self.notifications.setChecked(store.config.get("notifications_enabled", False))
        form.addRow(self.notifications)
        self.confirm_capture_exit = QCheckBox("已有标注时，退出截图前提示确认")
        self.confirm_capture_exit.setChecked(store.config["confirm_capture_exit"])
        form.addRow(self.confirm_capture_exit)
        self.closed_limit = QSpinBox()
        self.closed_limit.setRange(0, 100)
        self.closed_limit.setValue(store.config["closed_pin_limit"])
        form.addRow("可恢复的已关闭贴图数", self.closed_limit)
        self.magnifier = QCheckBox("自动显示截图放大镜（关闭后可按住 Alt 召唤）")
        self.magnifier.setChecked(store.config["magnifier_enabled"])
        form.addRow(self.magnifier)
        self.magnifier_grid = QCheckBox("录屏选区放大镜显示像素网格（截图固定无网格）")
        self.magnifier_grid.setChecked(store.config["magnifier_grid"])
        form.addRow(self.magnifier_grid)
        self.magnifier_pixels = QSpinBox()
        self.magnifier_pixels.setRange(5, 31)
        self.magnifier_pixels.setSingleStep(2)
        self.magnifier_pixels.setValue(store.config["magnifier_pixels"])
        form.addRow("放大镜取样边长（像素，越小放得越大）", self.magnifier_pixels)
        self.keep_all = QCheckBox("取消截图时也保留已选区域的截图记录")
        self.keep_all.setChecked(store.config["keep_all_capture_history"])
        form.addRow(self.keep_all)
        self.file_paths = QCheckBox("将剪贴板中的非图像文件路径贴出为文字")
        self.file_paths.setChecked(store.config["pin_file_paths"])
        form.addRow(self.file_paths)
        self.copy_file = QCheckBox("复制图片时同时复制图片文件")
        self.copy_file.setChecked(store.config["copy_image_as_file"])
        form.addRow(self.copy_file)
        self.capture_delay = QSpinBox()
        self.capture_delay.setRange(0, 30)
        self.capture_delay.setSuffix(" 秒")
        self.capture_delay.setValue(store.config["capture_delay"])
        form.addRow("截图延时", self.capture_delay)
        self.capture_cursor = QCheckBox("截图中包含鼠标指针")
        self.capture_cursor.setChecked(store.config["capture_cursor"])
        form.addRow(self.capture_cursor)
        self.deactivate = QCheckBox("激活其他窗口时退出截图（有标注时提示确认）")
        self.deactivate.setChecked(store.config["cancel_capture_on_deactivate"])
        form.addRow(self.deactivate)
        self.limit = QSpinBox()
        self.limit.setRange(10, 1000)
        self.limit.setValue(store.config["history_limit"])
        form.addRow("历史记录上限", self.limit)
        self.cache_limit = QSpinBox()
        self.cache_limit.setRange(32, 4096)
        self.cache_limit.setSuffix(" MB")
        self.cache_limit.setValue(store.config["cache_limit_mb"])
        form.addRow("图片历史缓存上限", self.cache_limit)
        self.paths = {}
        for key, title in (("image_dir", "截图目录"), ("video_dir", "录屏目录")):
            edit = QLineEdit(store.config[key])
            row = QHBoxLayout()
            row.addWidget(edit)
            row.addWidget(button("浏览", lambda checked=False, e=edit: self.browse(e)))
            form.addRow(title, row)
            self.paths[key] = edit
        form.addRow(label(f"本地数据：{store.root}", "muted"))
        form.addRow(label("关闭主窗口会收起到托盘。请通过托盘菜单退出。", "muted"))
        general_scroll = QScrollArea()
        general_scroll.setWidgetResizable(True)
        general_scroll.setWidget(general)
        tabs.addTab(general_scroll, "常规与存储")
        keys = QWidget()
        form = QFormLayout(keys)
        self.hotkeys = {}
        names = {"capture": "截图", "pin": "贴图", "toggle_pins": "显示/隐藏全部贴图", "record": "录屏",
                 "pause": "暂停/继续录制", "stop": "停止录制", "through": "鼠标穿透开关"}
        for key, title in names.items():
            edit = QKeySequenceEdit(QKeySequence(store.config["hotkeys"][key]))
            edit.setMaximumSequenceLength(1)
            self.hotkeys[key] = edit
            form.addRow(title, edit)
        form.addRow(label("支持字母、数字、F1–F24 和 Ctrl / Alt / Shift / Win 组合。保存时检测系统冲突。", "muted"))
        tabs.addTab(keys, "全局快捷键")
        controls = QWidget()
        controls.setObjectName("scrollContent")
        control_layout = QVBoxLayout(controls)
        form = QFormLayout()
        control_layout.addLayout(form)
        self.tray_actions = {}
        for key, title, default in (("tray_left", "托盘左键", "snip"), ("tray_middle", "托盘中键", "paste"),
                                    ("tray_right", "托盘右键", "")):
            combo = QComboBox()
            for name, command in (("显示菜单（右键）", ""), ("截图", "snip"), ("贴图", "paste"), ("隐藏 / 显示贴图", "toggle"),
                                  ("打开工作台", "home"), ("复制上次区域", "snip --last -o clipboard")):
                if command or key == "tray_right": combo.addItem(name, command)
            combo.setCurrentIndex(max(0, combo.findData(store.config.get(key, default))))
            self.tray_actions[key] = combo
            form.addRow(title, combo)
        self.super_snip = QCheckBox("启用 Win + 鼠标拖拽快速截图（松开后复制）")
        self.super_snip.setChecked(store.config.get("super_snip", False))
        form.addRow(self.super_snip)
        self.corner_actions = {}
        for key, title in (("tl", "屏幕左上角"), ("tr", "右上角"), ("bl", "左下角"), ("br", "右下角")):
            combo = QComboBox()
            for name, command in (("关闭", ""), ("显示 / 隐藏贴图", "toggle"), ("截图", "snip"), ("贴图", "paste")):
                combo.addItem(name, command)
            combo.setCurrentIndex(max(0, combo.findData(store.config.get("corners", {}).get(key, ""))))
            self.corner_actions[key] = combo
            form.addRow(title+"（停留 0.65 秒）", combo)
        control_layout.addWidget(label("自定义命令快捷键；忽略程序用分号分隔，可用 *.exe 或路径片段。", "muted"))
        self.command_table = QTableWidget(0, 3)
        self.command_table.setHorizontalHeaderLabels(["命令", "快捷键", "忽略程序"])
        self.command_table.horizontalHeader().setStretchLastSection(True)
        self.command_table.setColumnWidth(0, 260)
        for item in store.config.get("custom_commands", []):
            self.add_command_row(item.get("command", ""), item.get("hotkey", ""), item.get("ignore", ""))
        control_layout.addWidget(self.command_table)
        row = QHBoxLayout()
        row.addWidget(button("添加命令", lambda: self.add_command_row("snip --last -o clipboard")))
        row.addWidget(button("删除选中命令", lambda: self.command_table.removeRow(self.command_table.currentRow())))
        control_layout.addLayout(row)
        control_layout.addWidget(label("发送截图到应用：参数中用 %file% 代表图片文件。", "muted"))
        self.apps_table = QTableWidget(0, 3)
        self.apps_table.setHorizontalHeaderLabels(["名称", "程序路径", "参数"])
        self.apps_table.setMinimumHeight(180)
        self.apps_table.horizontalHeader().setStretchLastSection(True)
        self.apps_table.setColumnWidth(1, 270)
        for item in store.config.get("external_apps", []):
            self.add_app_row(item.get("name", ""), item.get("program", ""), item.get("arguments", "%file%"))
        control_layout.addWidget(self.apps_table)
        row = QHBoxLayout()
        row.addWidget(button("添加应用…", self.choose_application))
        row.addWidget(button("删除应用", lambda: self.apps_table.removeRow(self.apps_table.currentRow())))
        control_layout.addLayout(row)
        controls_scroll = QScrollArea()
        controls_scroll.setWidgetResizable(True)
        controls_scroll.setWidget(controls)
        tabs.addTab(controls_scroll, "鼠标与命令")
        root.addWidget(tabs)
        row = QHBoxLayout()
        row.addStretch()
        row.addWidget(button("取消", self.reject))
        row.addWidget(button("保存设置", self.save, True))
        root.addLayout(row)

    def browse(self, edit):
        folder = QFileDialog.getExistingDirectory(self, "选择目录", edit.text())
        if folder:
            edit.setText(folder)

    def add_command_row(self, command, hotkey="", ignore=""):
        row = self.command_table.rowCount()
        self.command_table.insertRow(row)
        for column, value in enumerate((command, hotkey, ignore)):
            self.command_table.setItem(row, column, QTableWidgetItem(value))

    def add_app_row(self, name, program, arguments):
        row = self.apps_table.rowCount()
        self.apps_table.insertRow(row)
        for column, value in enumerate((name, program, arguments)):
            self.apps_table.setItem(row, column, QTableWidgetItem(value))

    def choose_application(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择接收图片的应用", "", "程序 (*.exe)")
        if path: self.add_app_row(Path(path).stem, path, "%file%")

    def save(self):
        bindings = {name: edit.keySequence().toString(QKeySequence.PortableText) for name, edit in self.hotkeys.items()}
        if any(not text for text in bindings.values()) or len(set(bindings.values())) != len(bindings):
            QMessageBox.warning(self, "快捷键设置", "快捷键不能为空，也不能重复。")
            return
        try:
            from .commands import parse_command
            commands = []
            applications = []
            for row in range(self.apps_table.rowCount()):
                name, program, arguments = [self.apps_table.item(row, c).text().strip() for c in range(3)]
                if program: applications.append({"name": name, "program": program, "arguments": arguments})
            taken = set(bindings.values())
            for row in range(self.command_table.rowCount()):
                command, hotkey, ignore = [self.command_table.item(row, c).text().strip() for c in range(3)]
                if not command: continue
                parse_command(command)
                if hotkey:
                    sequence = QKeySequence(hotkey)
                    if sequence.isEmpty() or sequence.count() != 1: raise ValueError("命令快捷键无效。")
                    hotkey = sequence.toString(QKeySequence.PortableText)
                    if hotkey in taken: raise ValueError("命令快捷键与其他快捷键重复。")
                    taken.add(hotkey)
                commands.append({"command": command, "hotkey": hotkey, "ignore": ignore})
            for edit in self.paths.values():
                if not edit.text().strip():
                    raise ValueError("保存目录不能为空。")
                Path(edit.text()).mkdir(parents=True, exist_ok=True)
            native.set_startup(self.startup.isChecked())
            self.store.config.update({"restore_pins": self.restore.isChecked(), "startup": self.startup.isChecked(),
                                      "auto_save": self.auto_save.isChecked(), "history_limit": self.limit.value(),
                                      "notifications_enabled": self.notifications.isChecked(),
                                      "confirm_capture_exit": self.confirm_capture_exit.isChecked(),
                                      "closed_pin_limit": self.closed_limit.value(), "magnifier_enabled": self.magnifier.isChecked(),
                                      "copy_image_as_file": self.copy_file.isChecked(),
                                      "magnifier_grid": self.magnifier_grid.isChecked(), "magnifier_pixels": self.magnifier_pixels.value(),
                                      "keep_all_capture_history": self.keep_all.isChecked(), "pin_file_paths": self.file_paths.isChecked(),
                                      "capture_delay": self.capture_delay.value(), "capture_cursor": self.capture_cursor.isChecked(),
                                      "cancel_capture_on_deactivate": self.deactivate.isChecked(),
                                      "custom_commands": commands, "super_snip": self.super_snip.isChecked(),
                                      "external_apps": applications,
                                      "corners": {k: c.currentData() for k, c in self.corner_actions.items()},
                                      **{k: c.currentData() for k, c in self.tray_actions.items()},
                                      "cache_limit_mb": self.cache_limit.value(), "hotkeys": bindings,
                                      **{key: edit.text() for key, edit in self.paths.items()}})
            self.store.save_config()
            self.store.prune()
        except (OSError, ValueError) as exc:
            QMessageBox.warning(self, "保存失败", str(exc))
            return
        self.accept()
