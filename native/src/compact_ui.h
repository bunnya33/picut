#pragma once
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <cmath>

inline QString compactToolbarStyle() {
  return "QFrame#annotationToolbar,QFrame#annotationOptions,QFrame#recordToolbar,"
         "QWidget#recordingControls{background:#f2f2f2;border:1px solid #2080f0;"
         "border-radius:0;}QToolButton{background:transparent;border:0;padding:0;"
         "color:#333;}QToolButton:hover{background:#dedede;}"
         "QToolButton:checked{background:#2080f0;}";
}
inline QIcon recordingIcon(const QString &name, QColor color = QColor("#333333")) {
  static QMap<QString,QIcon> cache;
  const auto key=name+color.name();
  if (cache.contains(key)) return cache.value(key);
  QPixmap image(32,32); image.fill(Qt::transparent);
  QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
  p.scale(32.0/24,32.0/24);
  p.setPen(QPen(color,1.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (name=="settings") {
    p.drawEllipse(QRectF(6,6,12,12)); p.drawEllipse(QRectF(9,9,6,6));
    for (int n=0;n<8;++n) {
      const double a=n*3.141592653589793/4;
      p.drawLine(QPointF(12+6*std::cos(a),12+6*std::sin(a)),
                 QPointF(12+9*std::cos(a),12+9*std::sin(a)));
    }
  } else if (name=="record") {
    p.setPen(Qt::NoPen); p.setBrush(QColor("#ff0004")); p.drawEllipse(QRectF(6,6,12,12));
  } else if (name=="pause") {
    p.fillRect(QRectF(6,5,4,14),color); p.fillRect(QRectF(14,5,4,14),color);
  } else if (name=="play") {
    p.setBrush(color); p.drawPolygon(QPolygonF{{7,4},{19,12},{7,20}});
  } else if (name=="stop") {
    p.fillRect(QRectF(6,6,12,12),QColor("#ff0004"));
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
  QIcon result;
  for (int size : {16,24,32,48,64,128,256}) {
    QPixmap image(size,size); image.fill(Qt::transparent); QPainter p(&image);
    const int margin=qMax(1,size/16), gap=qMax(1,size/16), tile=(size-2*margin-gap)/2;
    const QColor colors[]{QColor("#4d90fe"),QColor("#ffc700"),QColor("#e94242"),QColor("#80c342")};
    for (int n=0;n<4;++n) p.fillRect(margin+(n%2)*(tile+gap),margin+(n/2)*(tile+gap),tile,tile,colors[n]);
    p.end(); result.addPixmap(image);
  }
  return result;
}
