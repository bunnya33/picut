#pragma once
#include "editing.h"
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <functional>
#include <optional>

class PinWindow : public EditingSurface {
  Q_OBJECT
public:
  static constexpr int Margin = 12;
  explicit PinWindow(const QImage &image, Store *store, QRect source = {},
                     QString id = {});
  QImage image;
  QString id, state = "visible", group = "默认", background = "暗色棋盘";
  double scale = 1, alpha = 1;
  bool through = false, gray = false, inverted = false, selected = false;
  QRect thumbnail;
  QRect contentRect() const override;
  QRect contentGeometry() const { return contentRect().translated(pos()); }
  QImage displayedImage() const;
  void setScale(double value);
  void setAlpha(double value);
  void setThrough(bool value);
  void startEditing();
  void endEditing();
  bool complete(const QString &action) override;
  QJsonObject layout() const;
signals:
  void changed();
  void requested(PinWindow *pin, QString action);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void contextMenuEvent(QContextMenuEvent *) override;
  void moveEvent(QMoveEvent *) override;

private:
  void applyScale(double value, std::optional<QPointF> anchor);
  QPoint dragOffset;
  bool moving = false, scaling = false;
};
class PinManager : public QObject {
  Q_OBJECT
public:
  explicit PinManager(Store *store, QObject *parent = nullptr);
  ~PinManager() override;
  PinWindow *create(const QImage &image, QRect source = {},
                    QJsonObject layout = {});
  void handle(PinWindow *pin, const QString &action);
  void toggleAll();
  void restoreClosed();
  void restore();
  void persist();
  void toggleThrough();
  void clearThrough();
  QList<QPointer<PinWindow>> windows, closed;
  Store *store;
  QTimer timer;
  std::function<void(PinWindow *, QString)> output;

private:
  bool hidden = false;
  QHash<QString, qint64> submittedImages;
  QJsonArray submittedLayout;
};
