#pragma once
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <cmath>

inline QString compactToolbarStyle() {
  return "QFrame#annotationToolbar,QFrame#annotationOptions,QFrame#recordToolbar,"
         "QWidget#recordingControls{background:#fbfcfe;border:1px solid #d9dfe8;"
         "border-radius:10px;}QToolButton{background:transparent;border:0;padding:0;"
         "border-radius:8px;color:#333;}QToolButton:hover{background:#eaf0f8;}"
         "QToolButton:pressed{background:#dce8f7;}"
         "QToolButton:checked{background:#087ae8;}";
}
inline QIcon recordingIcon(const QString &name, QColor color = QColor("#333333")) {
  static QMap<QString,QIcon> cache;
  const auto key=name+color.name();
  if (cache.contains(key)) return cache.value(key);
  QPixmap image(48,48); image.fill(Qt::transparent);
  QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
  p.scale(2,2);
  p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (name=="settings") {
    p.drawLine(4,6,20,6); p.drawLine(4,12,20,12); p.drawLine(4,18,20,18);
    p.setBrush(QColor("#f2f2f2"));
    p.drawEllipse(QPointF(9,6),2,2); p.drawEllipse(QPointF(16,12),2,2); p.drawEllipse(QPointF(7,18),2,2);
  } else if (name=="video") {
    p.drawRoundedRect(QRectF(3,6,12,12),1,1);
    p.drawPolygon(QPolygonF{{15,10},{21,6},{21,18},{15,14}});
  } else if (name=="back") {
    p.drawLine(4,12,20,12); p.drawPolyline(QPolygonF{{10,6},{4,12},{10,18}});
  } else if (name=="grip") {
    p.setPen(Qt::NoPen); p.setBrush(QColor("#858585"));
    for (int x:{9,15}) for (int y:{6,12,18}) p.drawEllipse(QPointF(x,y),1,1);
  } else if (name=="record") {
    p.setPen(QPen(QColor("#ff0004"),1.8)); p.drawEllipse(QRectF(4,4,16,16));
    p.setPen(Qt::NoPen); p.setBrush(QColor("#ff0004")); p.drawEllipse(QRectF(8,8,8,8));
  } else if (name=="pause") {
    p.drawLine(8,5,8,19); p.drawLine(16,5,16,19);
  } else if (name=="play") {
    p.drawPolygon(QPolygonF{{7,4},{19,12},{7,20}});
  } else if (name=="stop") {
    p.setPen(QPen(QColor("#ff0004"),1.8)); p.drawRect(QRectF(6,6,12,12));
  } else if (name=="cancel") {
    p.drawLine(6,6,18,18); p.drawLine(6,18,18,6);
  } else if (name=="microphone") {
    p.drawRoundedRect(QRectF(9,3,6,11),3,3);
    p.drawArc(QRectF(6,7,12,11),180*16,180*16); p.drawLine(12,18,12,21); p.drawLine(8,21,16,21);
  } else {
    p.drawPolygon(QPolygonF{{4,9},{8,9},{13,5},{13,19},{8,15},{4,15}});
    p.drawArc(QRectF(11,6,10,12),-70*16,140*16);
  }
  QIcon icon(image); cache.insert(key,icon); return icon;
}
inline QIcon picutIcon() {
  static const QIcon result = [] {
    QIcon icon;
    const QPixmap source(":/app/picut.png");
    for (int size : {16,20,24,32,40,48,64,96,128,256})
      icon.addPixmap(source.scaled(size,size,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    return icon;
  }();
  return result;
}
