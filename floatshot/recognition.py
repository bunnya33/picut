"""Local text and barcode recognition; no uploads or network service."""
import asyncio
import tempfile
from pathlib import Path

import numpy as np
from PySide6.QtCore import QThread, Qt, Signal
from PySide6.QtGui import QImage
from PySide6.QtWidgets import QApplication, QDialog, QHBoxLayout, QLabel, QPlainTextEdit, QVBoxLayout

from .theme import button


def scan_codes(image):
    import zxingcpp
    gray = image.convertToFormat(QImage.Format_Grayscale8)
    data = np.frombuffer(gray.constBits(), dtype=np.uint8).reshape(gray.height(), gray.bytesPerLine())[:, :gray.width()].copy()
    results = zxingcpp.read_barcodes(data)
    return "\n\n".join(f"{result.format}:\n{result.text}" for result in results)


def recognize_text(image):
    from winrt.windows.media.ocr import OcrEngine
    from winrt.windows.graphics.imaging import BitmapDecoder, BitmapPixelFormat, BitmapAlphaMode
    from winrt.windows.storage import FileAccessMode, StorageFile

    async def read(path):
        engine = OcrEngine.try_create_from_user_profile_languages()
        if engine is None:
            raise RuntimeError("Windows 未安装可用的 OCR 语言，请在系统语言设置中安装语言包。")
        file = await StorageFile.get_file_from_path_async(str(path))
        stream = await file.open_async(FileAccessMode.READ)
        bitmap = None
        try:
            decoder = await BitmapDecoder.create_async(stream)
            bitmap = await decoder.get_software_bitmap_converted_async(BitmapPixelFormat.BGRA8, BitmapAlphaMode.PREMULTIPLIED)
            result = await engine.recognize_async(bitmap)
            return "\n".join(line.text for line in result.lines)
        finally:
            if bitmap: bitmap.close()
            stream.close()

    with tempfile.TemporaryDirectory(prefix="floatshot-ocr-") as folder:
        path = Path(folder) / "input.png"
        maximum = OcrEngine.max_image_dimension
        if max(image.width(), image.height()) > maximum:
            image = image.scaled(maximum, maximum, Qt.KeepAspectRatio, Qt.SmoothTransformation)
        if not image.save(str(path), "PNG"):
            raise OSError("无法生成 OCR 临时图片。")
        return asyncio.run(read(path.resolve()))


def verify_recognition():
    """Portable-build verification uses generated content only."""
    from PySide6.QtCore import QRect
    from PySide6.QtGui import QFont, QPainter
    import zxingcpp
    image = QImage(1000, 250, QImage.Format_ARGB32)
    image.fill(Qt.white)
    painter = QPainter(image)
    painter.setPen(Qt.black)
    font = QFont("Segoe UI")
    font.setPixelSize(52)
    painter.setFont(font)
    painter.drawText(QRect(25, 30, 900, 160), Qt.AlignVCenter, "FLOATSHOT 12345")
    painter.end()
    text = recognize_text(image)
    if "FLOATSHOT" not in text.upper().replace(" ", "") or "12345" not in text:
        raise RuntimeError(f"OCR verification failed: {text!r}")
    code = zxingcpp.write_barcode(zxingcpp.BarcodeFormat.QRCode, "FloatShot local QR", width=300, height=300)
    array = np.asarray(code)
    qr = QImage(array.data, array.shape[1], array.shape[0], array.strides[0], QImage.Format_Grayscale8).copy()
    result = scan_codes(qr)
    if "FloatShot local QR" not in result: raise RuntimeError("QR verification failed")
    return {"ocr": text, "qr": result}


class RecognitionWorker(QThread):
    result = Signal(str)
    failure = Signal(str)

    def __init__(self, image, codes, parent):
        super().__init__(parent)
        self.image = image.copy()
        self.codes = codes

    def run(self):
        try:
            self.result.emit(scan_codes(self.image) if self.codes else recognize_text(self.image))
        except Exception as error:
            self.failure.emit(str(error))


class RecognitionDialog(QDialog):
    def __init__(self, image, codes=False, parent=None):
        super().__init__(parent)
        self.setWindowTitle("二维码 / 条形码识别" if codes else "文字识别")
        self.setWindowFlag(Qt.WindowStaysOnTopHint)
        self.resize(600, 430)
        layout = QVBoxLayout(self)
        self.status = QLabel("正在本地识别…")
        layout.addWidget(self.status)
        self.text = QPlainTextEdit()
        layout.addWidget(self.text)
        row = QHBoxLayout()
        row.addStretch()
        row.addWidget(button("复制结果", lambda: QApplication.clipboard().setText(self.text.toPlainText()), True))
        row.addWidget(button("关闭", self.accept))
        layout.addLayout(row)
        self.worker = RecognitionWorker(image, codes, self)
        self.worker.result.connect(self.received)
        self.worker.failure.connect(lambda error: self.status.setText("识别失败："+error))
        self.worker.finished.connect(lambda: self.setEnabled(True))
        self.worker.start()

    def received(self, text):
        self.status.setText("识别完成，可编辑并复制结果。" if text else "未识别到内容。")
        self.text.setPlainText(text)

    def done(self, code):
        # Keep the owning image/worker alive until WinRT finishes.
        if self.worker.isRunning():
            self.status.setText("识别仍在进行，请稍候。")
            return
        super().done(code)
