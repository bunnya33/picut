#pragma once
#include <QtGui/QImage>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
#include <memory>
class Store;
class PaddleOcrJob : public QObject {
  Q_OBJECT
public:
  explicit PaddleOcrJob(QObject *parent = nullptr, QString gpuWorker = {});
  ~PaddleOcrJob() override;
  void start(const QImage &image, const QString &profile = "common", int rotation = 0, const QString &acceleration = "auto");
  void cancel();
  bool running() const { return process.state() != QProcess::NotRunning; }
  QString temporaryPath() const { return temporary ? temporary->path() : QString(); }
  static QString modelsPath();
  static QJsonObject profiles();
signals:
  void finished(QJsonObject result);
  void progress(QString phase,int done,int total);
private:
  QProcess process;
  QTimer timeout;
  std::unique_ptr<QTemporaryDir> temporary;
  bool active = false;
  bool gpuAttempt = false;
  QString gpuProgram,selectedProfile,selectedAcceleration,gpuError;
  int selectedRotation = 0;
  quint64 generation = 0;
  QByteArray progressBuffer;
  void launch(bool gpu);
  void complete(QJsonObject result);
};
QString recognizeText(const QImage &image);
QString recognizeWindowsText(const QImage &image);
QString recognizeCodes(const QImage &image);
void showRecognition(const QImage &image, bool codes, QWidget *parent, Store *store = nullptr);
