from __future__ import annotations

import json
import os
import uuid
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import QStandardPaths


DEFAULTS = {
    "restore_pins": True,
    "auto_save": False,
    "notifications_enabled": False,
    "confirm_capture_exit": True,
    "closed_pin_limit": 1,
    "cancel_capture_on_deactivate": True,
    "magnifier_enabled": True,
    "annotation_options": {},
    "capture_delay": 0,
    "capture_cursor": False,
    "copy_image_as_file": False,
    "keep_all_capture_history": False,
    "magnifier_grid": True,
    "magnifier_pixels": 11,
    "pen_tips": [2, 4, 8],
    "pin_file_paths": True,
    "history_limit": 100,
    "cache_limit_mb": 512,
    "startup": False,
    "countdown": 3,
    "fps": 30,
    "quality": 22,
    "system_audio": False,
    "microphone": False,
    "mic_id": "",
    "speaker_id": "",
    "cursor": True,
    "hotkeys": {
        "capture": "F1", "pin": "F3", "toggle_pins": "Shift+F3",
        "record": "Ctrl+Alt+R", "pause": "Ctrl+Alt+P", "stop": "Ctrl+Alt+S",
        "through": "Ctrl+Alt+T",
    },
}


def atomic_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".new")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(path)


class Store:
    def __init__(self, root: Path | None = None):
        self.root = root or Path(os.environ.get("FLOATSHOT_DATA_DIR") or
                                QStandardPaths.writableLocation(QStandardPaths.AppLocalDataLocation))
        self.root.mkdir(parents=True, exist_ok=True)
        self.cache = self.root / "history"
        self.pins_dir = self.root / "pins"
        self.temp = self.root / "recording"
        for folder in (self.cache, self.pins_dir, self.temp):
            folder.mkdir(exist_ok=True)
        self.config = json.loads(json.dumps(DEFAULTS))
        loaded = self._read("settings.json", {})
        self.config.update(loaded)
        self.config["hotkeys"] = {**DEFAULTS["hotkeys"], **loaded.get("hotkeys", {})}
        documents = Path(QStandardPaths.writableLocation(QStandardPaths.DocumentsLocation))
        self.config.setdefault("image_dir", str(documents / "FloatShot" / "截图"))
        self.config.setdefault("video_dir", str(documents / "FloatShot" / "录屏"))
        self.history = self._read("history.json", [])

    def _read(self, name: str, default):
        try:
            return json.loads((self.root / name).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return default

    def save_config(self):
        atomic_json(self.root / "settings.json", self.config)

    def save_workspace(self, pins):
        atomic_json(self.root / "workspace.json", pins)

    def workspace(self):
        return self._read("workspace.json", [])

    @staticmethod
    def output_path(folder: str | Path, kind: str, extension: str) -> Path:
        folder = Path(folder)
        folder.mkdir(parents=True, exist_ok=True)
        stem = f"{kind}_{datetime.now():%Y%m%d_%H%M%S}"
        candidate = folder / f"{stem}.{extension}"
        index = 1
        while candidate.exists():
            candidate = folder / f"{stem}_{index}.{extension}"
            index += 1
        return candidate

    def add_image(self, image, output: str = ""):
        key = uuid.uuid4().hex
        cached = self.cache / f"{key}.png"
        if not image.save(str(cached), "PNG"):
            raise OSError("无法保存截图历史，请检查本地数据目录。")
        item = {"id": key, "kind": "image", "path": str(cached), "output": output,
                "created": datetime.now().isoformat(timespec="seconds"),
                "width": image.width(), "height": image.height()}
        self.history.insert(0, item)
        self.prune()
        return item

    def add_video(self, path, duration, width, height, thumbnail):
        item = {"id": uuid.uuid4().hex, "kind": "video", "path": str(path),
                "thumbnail": str(thumbnail), "created": datetime.now().isoformat(timespec="seconds"),
                "duration": duration, "width": width, "height": height}
        self.history.insert(0, item)
        self.prune()
        return item

    def remove_record(self, key):
        removed = [x for x in self.history if x["id"] == key]
        self.history = [x for x in self.history if x["id"] != key]
        for item in removed:
            self._remove_cached(item)
        self.save_history()

    def _remove_cached(self, item):
        # A history eviction must never remove the user's exported image or video.
        names = [item.get("thumbnail", "")]
        if item["kind"] == "image":
            names.append(item["path"])
        for name in names:
            path = Path(name) if name else None
            if path and path.resolve().parent == self.cache.resolve():
                path.unlink(missing_ok=True)

    def prune(self):
        limit = max(1, int(self.config["history_limit"]))
        while len(self.history) > limit:
            self._remove_cached(self.history.pop())
        limit_bytes = max(1, int(self.config["cache_limit_mb"])) * 1024 * 1024
        def size(item):
            name = item["path"] if item["kind"] == "image" else item.get("thumbnail", "")
            path = Path(name)
            return path.stat().st_size if name and path.is_file() else 0
        total = sum(size(item) for item in self.history)
        while total > limit_bytes and len(self.history) > 1:
            item = self.history.pop()
            total -= size(item)
            self._remove_cached(item)
        self.save_history()

    def save_history(self):
        atomic_json(self.root / "history.json", self.history)
