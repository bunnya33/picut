#pragma once
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

// Draw on a transparent surface instead of cropping controls from a screenshot.
// The same single circular arrow is used by text, shape and effect handles.
inline void paintRotationControl(QPainter &painter, QRectF rect,
                                  QColor color = QColor("#1688ef")) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(rect.topLeft());
  painter.scale(rect.width() / 14., rect.height() / 14.);
  QPainterPath arrow;
  arrow.arcMoveTo(QRectF(2.5, 2.5, 9, 9), 135);
  arrow.arcTo(QRectF(2.5, 2.5, 9, 9), 135, -300);
  arrow.moveTo(3.82, 1.2);
  arrow.lineTo(3.82, 3.82);
  arrow.lineTo(6.44, 3.82);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(arrow);
  painter.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(arrow);
  painter.restore();
}

inline QIcon annotationControlIcon(const QString &name) {
  QIcon icon;
  for (double dpr : {1., 1.5, 2., 3., 4.}) {
    QPixmap bitmap(qRound(14 * dpr), qRound(14 * dpr));
    bitmap.setDevicePixelRatio(dpr);
    bitmap.fill(Qt::transparent);
    QPainter painter(&bitmap);
    painter.setRenderHint(QPainter::Antialiasing);
    if (name == "rotate") {
      paintRotationControl(painter, {0, 0, 14, 14});
    } else {
      painter.setPen(QPen(Qt::white, .8));
      painter.setBrush(QColor("#1688ef"));
      painter.drawRoundedRect(QRectF(1, 1, 12, 12), .8, .8);
      painter.setPen(QPen(Qt::white, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      if (name == "delete") {
        painter.drawLine(QPointF(4.2, 4.2), QPointF(9.8, 9.8));
        painter.drawLine(QPointF(4.2, 9.8), QPointF(9.8, 4.2));
      } else {
        if (name == "sw") { painter.translate(14, 0); painter.scale(-1, 1); }
        painter.drawLine(QPointF(4, 4), QPointF(10, 10));
        painter.drawPolyline(QPolygonF{{4, 7}, {4, 4}, {7, 4}});
        painter.drawPolyline(QPolygonF{{7, 10}, {10, 10}, {10, 7}});
      }
    }
    painter.end();
    icon.addPixmap(bitmap);
  }
  return icon;
}
