#pragma once
#include <QtCore/QPointer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>
#include <functional>

class Store;
class QLabel;
class QFrame;
class QToolButton;
class QPainter;

// Original pixels live in temporary strips; only two frames and small preview
// strips stay resident while capturing. No full-height image is rebuilt on scroll.
class ScrollStitcher {
public:
  enum Result { Started, Appended, Unchanged, Revisited, BoundaryMismatch,
                NoOverlap, LimitReached, CacheError };
  Result accept(const QImage &image);
  QImage image() const;
  QSize size() const { return {width, height}; }
  int stripCount() const { return int(strips.size()); }
  void paint(QPainter &painter, QRectF target) const;
  static QSize previewSize(QSize original, QSize available);
private:
  struct Strip { QString path; QImage thumbnail; int y, height; };
  QTemporaryDir cache;
  QList<Strip> strips;
  QImage previous;
  int width = 0, height = 0, position = 0;
  bool append(const QImage &image);
};

class ScrollCapture : public QWidget {
  Q_OBJECT
public:
  ScrollCapture(Store *store, QRect region, quintptr sourceWindow,
                QWidget *parent = nullptr);
  ~ScrollCapture() override;
  void begin(bool live = true);
  ScrollStitcher::Result acceptFrame(const QImage &image);
  void finish(const QString &action);
  QRect previewGeometry() const;
  QSize imageSize() const { return stitcher.size(); }
  std::function<QImage()> frameProvider;
  std::function<bool(const QImage &, const QString &)> output;
signals:
  void finished(bool accepted);
protected:
  void paintEvent(QPaintEvent *) override;
  void closeEvent(QCloseEvent *) override;
  bool nativeEvent(const QByteArray &, void *, qintptr *) override;
private:
  Store *store;
  QRect region, available;
  quintptr sourceWindow;
  ScrollStitcher stitcher;
  QWidget *preview;
  QFrame *bar;
  QLabel *status;
  QToolButton *automatic;
  QTimer timer;
  QImage candidate;
  int stableFrames = 0, idleTicks = 0, stalledScrolls = 0;
  bool live = false, autoScrolling = false, closing = false, finishing = false;
  bool excluded = false, capturing = false;
  QList<int> hotkeys;
  void tick();
  QImage takeFrame();
  void place();
  void updateMask();
  void setAutomatic(bool enabled);
  void releaseHotkeys();
};
