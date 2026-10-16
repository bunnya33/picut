import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from PySide6.QtWidgets import QApplication
from PIL import Image
from floatshot.theme import app_icon

app = QApplication([])
folder = Path("assets")
folder.mkdir(exist_ok=True)
app_icon(256).pixmap(256, 256).save(str(folder / "floatshot.png"))
Image.open(folder / "floatshot.png").save(folder / "floatshot.ico", sizes=[(16,16), (32,32), (48,48), (64,64), (128,128), (256,256)])
