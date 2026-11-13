#pragma once
#include <QtGui/QImage>
#include <QtCore/QJsonObject>
#include <QtWidgets/QWidget>
class Store;
class OcrService;
class PaddleOcrJob : public QObject {
  Q_OBJECT
public:
  explicit PaddleOcrJob(QObject *parent = nullptr, QString gpuWorker = {});
  ~PaddleOcrJob() override;
  void start(const QImage &image, const QString &profile = "common", int rotation = 0, const QString &acceleration = "auto");
  void cancel();
  bool running() const { return active; }
  // OCR images now travel in shared memory; no temporary image is created.
  QString temporaryPath() const { return {}; }
  static void prewarm(const QString &profile = "common", const QString &acceleration = "auto");
  static QString modelsPath();
  static QJsonObject profiles();
signals:
  void finished(QJsonObject result);
  void progress(QString phase,int done,int total);
private:
  friend class OcrService;
  bool active = false;
  QString gpuProgram;
  quint64 generation = 0;
  void complete(quint64 token, QJsonObject result);
};
QString recognizeText(const QImage &image);
QString recognizeWindowsText(const QImage &image);
QString recognizeCodes(const QImage &image);
void showRecognition(const QImage &image, bool codes, QWidget *parent, Store *store = nullptr);
