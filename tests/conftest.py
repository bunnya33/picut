import os

os.environ["QT_QPA_PLATFORM"] = "offscreen"

import pytest
from PySide6.QtWidgets import QApplication

from floatshot.theme import STYLE, load_fonts


@pytest.fixture(scope="session")
def app():
    application = QApplication.instance() or QApplication([])
    application.setQuitOnLastWindowClosed(False)
    load_fonts()
    application.setStyleSheet(STYLE)
    yield application


@pytest.fixture
def store(app, tmp_path):
    from floatshot.storage import Store
    result = Store(tmp_path / "data")
    result.config["image_dir"] = str(tmp_path / "images")
    result.config["video_dir"] = str(tmp_path / "videos")
    return result
