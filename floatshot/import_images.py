"""Import explicitly copied/dropped image URLs without blocking the UI."""
from html.parser import HTMLParser
from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtGui import QImage
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest


class ImageSource(HTMLParser):
    def __init__(self):
        super().__init__()
        self.source = ""

    def handle_starttag(self, tag, attrs):
        if tag == "img" and not self.source: self.source = dict(attrs).get("src", "")


def image_url(mime):
    urls = mime.urls() if mime.hasUrls() else []
    if mime.hasHtml():
        parser = ImageSource()
        parser.feed(mime.html())
        if parser.source: urls.insert(0, QUrl(parser.source))
    return next((url for url in urls if url.scheme().lower() in ("http", "https")), QUrl())


class ImageDownloader(QObject):
    loaded = Signal(QImage)
    failed = Signal(str)
    limit = 32*1024*1024

    def __init__(self, parent):
        super().__init__(parent)
        self.manager = QNetworkAccessManager(self)
        self.replies = set()

    def load(self, url):
        if url.scheme().lower() not in ("http", "https"):
            self.failed.emit("只支持 HTTP/HTTPS 图片地址。")
            return
        request = QNetworkRequest(url)
        request.setTransferTimeout(15000)
        request.setAttribute(QNetworkRequest.RedirectPolicyAttribute, QNetworkRequest.NoLessSafeRedirectPolicy)
        request.setRawHeader(b"User-Agent", b"FloatShot/0.3")
        reply = self.manager.get(request)
        self.replies.add(reply)
        reply.downloadProgress.connect(lambda received, total: reply.abort() if received > self.limit or total > self.limit else None)
        reply.finished.connect(lambda: self.complete(reply))

    def complete(self, reply):
        try:
            if reply.error() != QNetworkReply.NoError:
                self.failed.emit("网页图片读取失败："+reply.errorString())
                return
            content = bytes(reply.readAll())
            image = QImage.fromData(content) if len(content) <= self.limit else QImage()
            if image.isNull(): self.failed.emit("该地址没有返回可读取的图片。")
            else: self.loaded.emit(image)
        finally:
            self.replies.discard(reply)
            reply.deleteLater()
