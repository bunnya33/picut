"""Explicit, short backend verification for validating a portable build."""
import re
import subprocess
import time
from pathlib import Path

import imageio_ffmpeg
from PySide6.QtCore import QCoreApplication, QEvent, QUrl

from . import native
from .capture import Desktop
from .dialogs import VideoPreview
from .recorder import CREATE_FLAGS, RecordOptions, Recorder


def verify_backend(app, root: Path):
    root.mkdir(parents=True, exist_ok=True)
    cache, temp = root / "cache", root / "temp"
    cache.mkdir(exist_ok=True)
    temp.mkdir(exist_ok=True)
    desktop = Desktop()
    screen = desktop.screens[0]
    sample = desktop.crop(screen.rect.adjusted(0, 0, -(screen.rect.width()-80), -(screen.rect.height()-60)))
    assert not sample.isNull(), "Screenshot is empty"
    physical = native.to_physical(screen.rect)
    region = {**physical, "width": min(1920, physical["width"]), "height": min(1080, physical["height"])}
    result = {"screen_capture": [sample.width(), sample.height()], "physical_monitors":
              [(name, [rect.x(), rect.y(), rect.width(), rect.height()]) for name, rect in native.physical_monitors()]}
    hotkeys = native.Hotkeys({"capture": lambda: None})
    result["hotkey_probe_conflicts"] = hotkeys.register({"capture": "Ctrl+Alt+Shift+F23"})
    hotkeys.clear()
    warnings, failures, completed = [], [], []
    recorder = Recorder(temp, cache)
    recorder.warning.connect(warnings.append)
    recorder.failed.connect(failures.append)
    recorder.finished.connect(completed.append)
    output = root / f"wasapi-{time.time_ns()}.mp4"
    recorder.start(RecordOptions(region, output, fps=30, system_audio=True, cursor=True))
    deadline = time.monotonic()+12
    while recorder.state == "starting" and time.monotonic() < deadline:
        app.processEvents()
        time.sleep(.01)
    assert recorder.state == "recording", failures
    for duration, pause_after in ((2.2, True), (1.2, False)):
        until = time.monotonic()+duration
        while time.monotonic() < until:
            app.processEvents()
            time.sleep(.01)
        if pause_after:
            recorder.pause()
            time.sleep(.5)
            recorder.pause()
    recorder.stop()
    recorder.thread.join(timeout=30)
    app.processEvents()
    assert not recorder.thread.is_alive(), "Recorder did not stop"
    assert output.exists(), failures
    checked = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-i", str(output), "-f", "null", "-"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=CREATE_FLAGS)
    assert checked.returncode == 0, "MP4 failed to decode"
    info = checked.stderr.decode(errors="replace")
    assert "h264" in info and "aac" in info, "Missing video/audio track"
    match = re.search(r"Duration: (\d+):(\d+):([\d.]+)", info)
    result.update({"recording_seconds": float(match[3]), "recording_dimensions": [region["width"], region["height"]],
                   "fps": 30, "video": "H.264", "audio": "AAC / WASAPI loopback", "warnings": warnings,
                   "microphone_hardware_tested": False})
    preview = VideoPreview(completed[0])
    preview.audio.setMuted(True)
    preview.player.play()
    until = time.monotonic()+2
    while time.monotonic() < until and preview.player.position() < 200:
        app.processEvents()
        time.sleep(.01)
    result["qt_playback_position_ms"] = preview.player.position()
    assert result["qt_playback_position_ms"] >= 100, preview.player.errorString()
    preview.player.stop()
    preview.player.setSource(QUrl())
    preview.close()
    preview.deleteLater()
    app.processEvents()
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    deadline = time.monotonic()+2
    while True:
        try:
            output.unlink()
            break
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            app.processEvents()
            time.sleep(.02)
    for path in cache.glob("*.png"):
        path.unlink()
    return result
