#pragma once
#include <QtCore/QJsonObject>
#include <QtCore/QUuid>
#include <QtGui/QImage>
#include <windows.h>
#include <cstring>
#include <stdexcept>

// A request owns its mapping until the worker replies or has been terminated.
// Random Local names avoid disk files and work on Windows 10 without a service.
class OcrSharedImage {
  HANDLE handle = nullptr;
  const void *view = nullptr;
public:
  OcrSharedImage() = default;
  OcrSharedImage(const OcrSharedImage &) = delete;
  OcrSharedImage &operator=(const OcrSharedImage &) = delete;
  ~OcrSharedImage() { if (view) UnmapViewOfFile(view); if (handle) CloseHandle(handle); }
  QJsonObject create(const QImage &source) {
    if (source.isNull() || qint64(source.width()) * source.height() > 40000000)
      throw std::runtime_error("图片无效或超过 4000 万像素，请分区域识别。");
    const auto image = source.convertToFormat(QImage::Format_ARGB32);
    const auto bytes = quint64(image.width()) * image.height() * 4;
    const auto name = "Local\\picut-ocr-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        DWORD(bytes >> 32), DWORD(bytes), reinterpret_cast<const wchar_t *>(name.utf16()));
    if (!handle) throw std::runtime_error("无法分配 OCR 共享内存。");
    auto writable = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, SIZE_T(bytes));
    if (!writable) throw std::runtime_error("无法写入 OCR 共享内存。");
    std::memcpy(writable, image.constBits(), size_t(bytes));
    UnmapViewOfFile(writable);
    return {{"mapping", name}, {"width", image.width()}, {"height", image.height()},
            {"bytes", qint64(bytes)}, {"alpha", source.hasAlphaChannel()}};
  }
  QImage open(const QJsonObject &request) {
    const auto width = request["width"].toInt(), height = request["height"].toInt();
    const auto pixels = qint64(width) * height;
    const auto name = request["mapping"].toString();
    if (width < 1 || height < 1 || width > 40000000 || height > 40000000 ||
        pixels > 40000000 || request["bytes"].toInteger() != pixels * 4 ||
        !name.startsWith("Local\\picut-ocr-") || name.size() > 128)
      throw std::runtime_error("OCR 共享图像参数无效。");
    const auto bytes=pixels*4;
    handle = OpenFileMappingW(FILE_MAP_READ, FALSE, reinterpret_cast<const wchar_t *>(name.utf16()));
    if (!handle) throw std::runtime_error("OCR 共享图像已释放。");
    view = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, SIZE_T(bytes));
    if (!view) throw std::runtime_error("OCR 共享图像大小无效。");
    return QImage(static_cast<const uchar *>(view), width, height, width * 4,
        request["alpha"].toBool() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
  }
};
