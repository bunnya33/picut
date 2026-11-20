#pragma once
#include "capture.h"
#include "pin.h"
#include "recorder.h"
#include "recording_overlay.h"
#include "store.h"
#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QPointer>
#include <QtCore/QCache>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSystemTrayIcon>
class RecordingEditor;

class ApplicationController : public QObject,
                              public QAbstractNativeEventFilter {
  Q_OBJECT
public:
  explicit ApplicationController(bool testing = false);
  ~ApplicationController() override;
  Store store;
  PinManager pins;
  QPointer<CaptureOverlay> overlay;
  QPointer<QMainWindow> home;
  std::unique_ptr<RecordingOverlay> recordOverlay;
  std::unique_ptr<RecordingEditor> recordEditor;
  std::unique_ptr<Recorder> recorder;
  void showHome();
  void beginCapture(bool recording = false);
  void pinClipboard();
  void openSettings();
  void prepareRecord(QRect region, bool showSettings = true,
                     RecordingPainter painter = {}, CaptureOverlay *source = nullptr);
  void refreshHistory();
  quint64 historyThumbnailReads = 0;
  void previewVideo(const QString &file);
  void notify(QString text);
  bool imageOutput(const QImage &image, QRect region, QString action);
  bool nativeEventFilter(const QByteArray &, void *message,
                         qintptr *result) override;
  bool recording() const { return recorder && recorder->active(); }
  bool quitting = false;

private:
  QSystemTrayIcon tray;
  QMenu *trayMenu = nullptr;
  QPointer<QListWidget> historyList;
  QPointer<QWidget> recordCountdown;
  QPointer<CaptureOverlay> recordSource;
  bool testing = false, pending = false, recordSetup = false, hotkeysSuspended = false;
  bool historyDirty = true;
  QCache<QString, QPixmap> historyThumbnails{128};
  QList<int> hotkeyIds;
  quint64 clipboardPinRequest = 0;
  void tryPinClipboard(quint64 request, quint32 sequence, int attempt);
  void registerHotkeys();
  void clearHotkeys();
  bool dispatchHotkey(int id);
  void updateTrayMenu();
  void makeRecorder();
  void restoreRecordSource();
  void quit();
};
bool verifyOcrMemory(ApplicationController &controller, const QString &folder);
bool verifyNativeFeatures(ApplicationController &controller,
                          const QString &folder, bool memory = false);
bool verifyNativeUi(ApplicationController &controller, const QString &folder);
bool verifyDesktopMemory(ApplicationController &controller,
                         const QString &folder);

bool verifyPinLatency(ApplicationController &controller, const QString &folder);

#ifdef Q_OS_MACOS
bool verifyMacTranslation(Store *store);
#endif
