#pragma once
#include <QtCore/QPointer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include <QtGui/QRegion>
#include <QtWidgets/QWidget>
#include <functional>
#include <memory>

class Store;
class QLabel;
class QFrame;
class QToolButton;
class QPainter;
class ScrollFramePump;
class ScrollMatchPump;

// Original pixels live in temporary strips; matching references and small
// previews stay resident. No full-height image is rebuilt on scroll.
class ScrollStitcher {
public:
  enum Result { Started, Appended, Unchanged, Revisited, BoundaryMismatch,
                NoOverlap, LimitReached, CacheError };
  struct Preview {
    struct Part { QImage thumbnail; int y,height,sourceY,sourceHeight; };
    QSize size;
    quint64 revision=0;
    QList<Part> parts;
    void paint(QPainter &painter,QRectF target) const;
  };
  Result accept(const QImage &image, int direction = 0);
  QImage image() const;
  QSize size() const { return {width, height}; }
  int stripCount() const { return int(strips.size()); }
  quint64 contentRevision() const { return revision; }
  void paint(QPainter &painter, QRectF target) const;
  Preview preview() const;
  static QSize previewSize(QSize original, QSize available);
private:
  struct Strip { QString path; QImage thumbnail; int y, height, sourceY=0, sourceHeight=0; };
  QTemporaryDir cache;
  QList<Strip> strips;
  QImage previous, previousGray;
  QList<QRegion> repaintAreas;
  int width = 0, height = 0, position = 0;
  quint64 nextStrip=0;
  quint64 revision=0;
  bool animationSeen=false;
  bool append(const QImage &image, bool prepend = false, int replacePixels = 0);
  QImage readRows(int y,int count) const;
  QImage referenceMask() const;
  void rememberRepaints(const QList<QRect> &areas, int at, int matchingWidth);
  bool refreshVisible(const QImage &frame, int at);
};

class ScrollCapture : public QWidget {
  Q_OBJECT
public:
  ScrollCapture(Store *store, QRect region, quintptr sourceWindow,
                QWidget *parent = nullptr);
  ~ScrollCapture() override;
  void begin(bool live = true);
  ScrollStitcher::Result acceptFrame(const QImage &image, int direction = 0);
  void sample();
  void noteScrollInput(int direction, bool manual = true);
  int scrollDirection() const {
    return inputTime.isValid() && inputTime.elapsed()<600 ? inputDirection : 0;
  }
  bool acceptsScrollInput(QPoint physicalPoint, bool keyboard = false) const;
  void finish(const QString &action);
  QRect previewGeometry() const;
  QSize imageSize() const { return displayed.size; }
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
  ScrollStitcher::Preview displayed;
  QWidget *preview;
  QFrame *bar;
  QLabel *status;
  QToolButton *automatic;
  QTimer timer;
  std::unique_ptr<ScrollFramePump> framePump;
  std::unique_ptr<ScrollMatchPump> matchPump;
  quint64 matchSerial=0;
  QImage candidate;
  int stalledScrolls = 0, inputDirection = 0;
  quintptr mouseHook = 0, keyboardHook = 0;
  QElapsedTimer inputTime, idleTime, failureTime;
  ScrollStitcher::Result pendingFailure = ScrollStitcher::Unchanged;
  bool inputTickQueued = false;
  bool failureReported = false;
  bool live = false, autoScrolling = false, closing = false, finishing = false;
  bool excluded = false, capturing = false;
  bool backgroundAcquisition=false;
  QList<int> hotkeys;
  void installInputHooks();
  void releaseInputHooks();
  void reportAlignmentFailure(ScrollStitcher::Result result);
  void displayResult(ScrollStitcher::Result result,ScrollStitcher::Preview next);
  void stopPumps(bool drain);
  QImage takeFrame();
  void place();
  void updateMask();
  void setAutomatic(bool enabled);
  void releaseHotkeys();
};
