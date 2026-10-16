#pragma once
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>
#include <QtCore/QObject>
#include <QtCore/QThreadPool>
#include <QtCore/QSet>
#include <functional>
#include <mutex>
#include <vector>

class Store : public QObject {
  Q_OBJECT
public:
  QString root;
  QJsonObject config;
  QJsonArray history;
  explicit Store(QString directory = {});
  ~Store() override;
  QString path(const QString &name) const;
  bool saveConfig();
  bool saveHistory();
  QJsonArray workspace() const;
  bool saveWorkspace(const QJsonArray &data);
  QJsonObject addImage(const QImage &image, const QString &output = {});
  void addImageAsync(const QImage &image, const QString &output = {});
  void submitIo(std::function<QString()> work,
                std::function<void(const QString &)> completed = {});
  void flushIo();
  static bool writeImage(const QString &path, const QImage &image);
  void addVideo(const QJsonObject &item);
  void removeRecord(const QString &id);
  QString outputPath(const QString &folder, const QString &prefix,
                     const QString &extension) const;
  QString reserveOutputPath(const QString &folder, const QString &prefix,
                            const QString &extension);
  static bool writeJson(const QString &path, const QJsonValue &value);
  static QJsonValue readJson(const QString &path, const QJsonValue &fallback);
  static bool importLegacy(const QString &destination, const QString &source);

signals:
  void historyChanged();
  void ioFailed(QString message);

private:
  QThreadPool ioPool;
  std::mutex completionMutex;
  std::vector<std::function<void()>> ioCompletions;
  void drainIoResults();
  int pendingIoJobs = 0;
  QSet<QString> pendingOutputPaths;
  bool prune();
};
