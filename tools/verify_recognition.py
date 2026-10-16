"""Exercise Windows OCR and barcode decoding with synthetic content."""
import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtWidgets import QApplication
from floatshot.theme import load_fonts
from floatshot.recognition import recognize_text, scan_codes

app = QApplication([])
load_fonts()
image = QImage(1000, 250, QImage.Format_ARGB32)
image.fill(Qt.white)
p = QPainter(image)
p.setPen(Qt.black)
font = QFont("Segoe UI")
font.setPixelSize(52)
p.setFont(font)
p.drawText(QRect(25, 30, 900, 160), Qt.AlignVCenter, "FLOATSHOT 12345")
p.end()
text = recognize_text(image)
assert "FLOATSHOT" in text.upper().replace(" ", "") and "12345" in text, text
print("Windows OCR:", repr(text))

import zxingcpp
import numpy as np
code = zxingcpp.write_barcode(zxingcpp.BarcodeFormat.QRCode, "FloatShot local QR", width=300, height=300)
array = np.asarray(code)
qr = QImage(array.data, array.shape[1], array.shape[0], array.strides[0], QImage.Format_Grayscale8).copy()
result = scan_codes(qr)
assert "FloatShot local QR" in result, result
print("QR:", repr(result))
