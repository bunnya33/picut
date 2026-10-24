#include "recording_markup.h"
#include <QtGui/QPainterPathStroker>
#include <stdexcept>
#include <vector>

namespace {
struct Operation {
  QImage image;
  QPoint position;
  std::optional<Mark> effect;
  QPainterPath erase;
};
}

RecordingPainter recordingPainter(const Annotation &annotation, QSize frameSize) {
  if (annotation.marks.isEmpty()) return {};
  const auto sourceSize=annotation.original.size();
  const auto size=frameSize.isEmpty()?sourceSize:frameSize;
  if (sourceSize.isEmpty() || size.isEmpty()) return {};
  const double sx=double(size.width())/sourceSize.width(),
               sy=double(size.height())/sourceSize.height(), scale=(sx+sy)/2;
  std::vector<Operation> operations;
  QImage layer(size,QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  bool painted=false,hasErase=false;
  auto flush=[&] {
    if (!painted) return;
    int left=size.width(),top=size.height(),right=-1,bottom=-1;
    for (int y=0;y<layer.height();++y) {
      const auto row=reinterpret_cast<const QRgb *>(layer.constScanLine(y));
      for (int x=0;x<layer.width();++x) if (qAlpha(row[x])) {
        left=qMin(left,x); right=qMax(right,x); top=qMin(top,y); bottom=y;
      }
    }
    if (right>=left && bottom>=top)
      operations.push_back({layer.copy(QRect(QPoint(left,top),QPoint(right,bottom))),{left,top},{},{}});
    layer.fill(Qt::transparent); painted=false;
  };
  for (auto mark:annotation.marks) {
    // Fractional Windows scaling can round the screenshot's uniform scale
    // and the recorder's physical X/Y scales to sizes that differ by one pixel.
    // Normalize once here; keep the exact recording rectangle and per-frame cache.
    if (size!=sourceSize) {
      for (auto &point:mark.points) point={point.x()*sx,point.y()*sy};
      mark.width*=scale; mark.fontSize*=scale;
      mark.radius=qRound(mark.radius*scale); mark.intensity=qRound(mark.intensity*scale);
      if (mark.outlineWidth) mark.outlineWidth=qMax(1,qRound(mark.outlineWidth*scale));
    }
    if (mark.kind=="eraser") {
      flush();
      auto path=markPath(mark);
      if (mark.areaShape=="stroke") {
        QPainterPathStroker stroker; stroker.setWidth(mark.width); stroker.setCapStyle(Qt::RoundCap);
        path=stroker.createStroke(path);
      }
      operations.push_back({{},{},{},path}); hasErase=true;
    } else if (mark.kind=="mosaic" || mark.kind=="blur" || mark.kind=="magnify") {
      flush(); operations.push_back({{},{},mark,{}});
    } else {
      paintMark(layer,mark); painted=true;
    }
  }
  flush();
  if (operations.empty()) return {};
  return [size,operations=std::move(operations),hasErase](QImage &frame) {
    if (frame.size()!=size) throw std::runtime_error("录制画面尺寸发生变化，无法对齐截图标注。");
    const auto original=hasErase?frame:QImage();
    for (const auto &operation:operations) {
      if (operation.effect) paintMark(frame,*operation.effect);
      else if (!operation.erase.isEmpty()) {
        QPainter p(&frame); p.setClipPath(operation.erase);
        p.setCompositionMode(QPainter::CompositionMode_Source); p.drawImage(0,0,original);
      } else {
        QPainter p(&frame); p.drawImage(operation.position,operation.image);
      }
    }
  };
}
