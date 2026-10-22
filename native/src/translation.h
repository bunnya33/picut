#pragma once
#include "recognition.h"
#include <QtCore/QJsonArray>
#include <QtGui/QFont>
#include <QtGui/QPolygonF>

class LocalTranslationJob : public QObject {
  Q_OBJECT
public:
  explicit LocalTranslationJob(QObject *parent = nullptr);
  ~LocalTranslationJob() override;
  void start(const QJsonArray &blocks, const QString &source, const QString &target);
  void cancel();
  bool running() const { return process.state() != QProcess::NotRunning; }
  QString temporaryPath() const { return temporary ? temporary->path() : QString(); }
  static QString modelsPath();
signals:
  void finished(QJsonObject result);
  void progress(QString phase, int done, int total);
  void blockFinished(QJsonObject block);
private:
  QProcess process;
  QTimer timeout;
  QByteArray buffer;
  std::unique_ptr<QTemporaryDir> temporary;
  bool active = false;
  void complete(const QJsonObject &result);
};

struct TranslationBlock {
  int id = 0;
  QString original, translated;
  QList<QPolygonF> lines;
  QRect bounds;
  QColor background = Qt::white, foreground = Qt::black;
  double fontSize = 16;
  bool bold = false, unchanged = false, clipped = false;
};
class TranslationPage {
public:
  QList<TranslationBlock> blocks;
  void load(const QImage &image, const QJsonObject &ocr);
  QJsonArray request() const;
  void apply(const QJsonObject &result);
  QImage render(const QImage &source);
  QString text(bool original = false) const;
};
