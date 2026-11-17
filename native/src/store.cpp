#include "store.h"
#include "hotkey.h"
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <stdexcept>

Store::Store(QString directory) {
  ioPool.setMaxThreadCount(1);
  ioPool.setExpiryTimeout(1000);
  bool explicitDirectory =
      !directory.isEmpty() ||
      !qEnvironmentVariable("FLOATSHOT_DATA_DIR").isEmpty();
  root = directory.isEmpty() ? qEnvironmentVariable("FLOATSHOT_DATA_DIR")
                             : directory;
  if (root.isEmpty())
    root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (!explicitDirectory && !QFileInfo::exists(path("settings.json"))) {
    auto legacy = QFileInfo(root).absolutePath() + "/FloatShot";
    importLegacy(root, legacy);
  }
  for (const auto &folder :
       {QString(), QString("history"), QString("pins"), QString("recording")})
    QDir().mkpath(path(folder));
  auto documents =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
      "/FloatShot";
  config = {{"notifications_enabled", false},
            {"ocr_engine", "paddle"},
            {"ocr_profile", "common"},
            {"ocr_acceleration", "auto"},
            {"confirm_capture_exit", true},
            {"restore_pins", true},
            {"auto_save", false},
            {"capture_delay", 0},
            {"capture_cursor", false},
            {"capture_color_format", "RGB"},
            {"magnifier_enabled", true},
            {"magnifier_pixels", 11},
            {"magnifier_grid", true},
            {"history_limit", 100},
            {"cache_limit_mb", 512},
            {"closed_pin_limit", 1},
            {"cancel_capture_on_deactivate", true},
            {"keep_all_capture_history", false},
            {"copy_image_as_file", false},
            {"pin_file_paths", true},
            {"startup", true},
            {"startup_setting_v1", true},
            {"annotation_color", "#ffff0004"},
            {"annotation_last_tool", "arrow"},
            {"image_dir", documents + "/截图"},
            {"video_dir", documents + "/录屏"},
            {"countdown", 3},
            {"fps", 60},
            {"quality", 14},
            {"cursor", true},
            {"click_feedback", false},
            {"system_audio", false},
            {"microphone", false},
            {"speaker_id", ""},
            {"mic_id", ""},
            {"hotkeys", QJsonObject{{"capture", "F1"},
                                    {"pin", "F3"},
                                    {"toggle_pins", "Shift+F3"},
                                    {"through", "Ctrl+Alt+T"},
                                    {"pause", "Ctrl+Alt+P"},
                                    {"stop", "Ctrl+Alt+S"}}}};
  auto loaded = readJson(path("settings.json"), QJsonObject()).toObject();
  for (auto i = loaded.begin(); i != loaded.end(); ++i)
    if (i.key() == "hotkeys") {
      auto keys = config["hotkeys"].toObject();
      auto imported = i.value().toObject();
      for (auto key = imported.begin(); key != imported.end(); ++key)
        keys[key.key()] = key.value();
      config["hotkeys"] = keys;
    } else
      config[i.key()] = i.value();
  // Earlier releases saved an unused false placeholder. Enable the new setting
  // once on upgrade, then preserve an explicit choice made in the settings UI.
  if (!loaded["startup_setting_v1"].toBool()) {
    config["startup"] = true;
    config["startup_setting_v1"] = true;
  }
  // Recording now starts from the F1 selection toolbar. Retired bindings must
  // not reserve Shift+F1 (or a former custom combination) after an upgrade.
  auto keys=config["hotkeys"].toObject(); keys.remove("record"); config["hotkeys"]=keys;
  history = readJson(path("history.json"), QJsonArray()).toArray();
}
bool Store::importLegacy(const QString &destination, const QString &source) {
  QString legacy =
      QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath());
  QString target =
      QDir::fromNativeSeparators(QFileInfo(destination).absoluteFilePath());
  if (QFileInfo::exists(target + "/settings.json") ||
      !QFileInfo::exists(legacy + "/settings.json"))
    return false;
  auto settings = readJson(legacy + "/settings.json", QJsonObject()).toObject();
  if (settings.isEmpty())
    return false;
  settings["startup"] = false;
  for (const auto &folder : QStringList{"history", "pins"}) {
    if (!QDir().mkpath(target + "/" + folder))
      return false;
    for (const auto &file :
         QDir(legacy + "/" + folder).entryInfoList(QDir::Files)) {
      auto output = target + "/" + folder + "/" + file.fileName();
      if (!QFileInfo::exists(output) &&
          !QFile::copy(file.absoluteFilePath(), output))
        return false;
    }
  }
  QJsonArray migrated;
  for (auto value :
       readJson(legacy + "/history.json", QJsonArray()).toArray()) {
    auto item = value.toObject();
    for (const auto &key : QStringList{"path", "thumbnail"}) {
      auto file = QDir::fromNativeSeparators(item[key].toString());
      if (file.startsWith(legacy + "/", Qt::CaseInsensitive))
        item[key] = target + file.mid(legacy.size());
    }
    migrated.append(item);
  }
  return writeJson(target + "/history.json", migrated) &&
         writeJson(target + "/workspace.json",
                   readJson(legacy + "/workspace.json", QJsonArray())) &&
         writeJson(target + "/migration.json",
                   QJsonObject{{"source", legacy},
                               {"old_data_preserved", true},
                               {"version", "0.5.15"}}) &&
         writeJson(target + "/settings.json", settings);
}
QString Store::path(const QString &name) const {
  return QDir(root).filePath(name);
}
bool Store::writeJson(const QString &file, const QJsonValue &value) {
  QDir().mkpath(QFileInfo(file).absolutePath());
  QSaveFile output(file);
  if (!output.open(QIODevice::WriteOnly))
    return false;
  auto data = value.isArray() ? QJsonDocument(value.toArray())
                              : QJsonDocument(value.toObject());
  return output.write(data.toJson()) >= 0 && output.commit();
}
QJsonValue Store::readJson(const QString &name, const QJsonValue &fallback) {
  QFile file(name);
  if (!file.open(QIODevice::ReadOnly))
    return fallback;
  QJsonParseError error;
  auto data = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError)
    return fallback;
  return data.isArray() ? QJsonValue(data.array()) : QJsonValue(data.object());
}
bool Store::saveConfig() { return writeJson(path("settings.json"), config); }
bool Store::saveHistory() { return writeJson(path("history.json"), history); }
QJsonArray Store::workspace() const {
  return readJson(path("workspace.json"), QJsonArray()).toArray();
}
bool Store::saveWorkspace(const QJsonArray &data) {
  return writeJson(path("workspace.json"), data);
}
QString Store::outputPath(const QString &folder, const QString &prefix,
                          const QString &extension) const {
  QDir().mkpath(folder);
  auto stem =
      prefix + "_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
  auto candidate = QDir(folder).filePath(stem + "." + extension);
  for (int n = 1; QFileInfo::exists(candidate) || pendingOutputPaths.contains(candidate); ++n)
    candidate = QDir(folder).filePath(stem + "_" + QString::number(n) + "." +
                                      extension);
  return candidate;
}
QString Store::reserveOutputPath(const QString &folder, const QString &prefix,
                                 const QString &extension) {
  auto file = outputPath(folder, prefix, extension);
  pendingOutputPaths.insert(file);
  return file;
}
QJsonObject Store::addImage(const QImage &image, const QString &output) {
  auto id = QUuid::createUuid().toString(QUuid::Id128);
  auto file = path("history/" + id + ".png");
  if (!image.save(file, "PNG"))
    throw std::runtime_error("无法保存截图历史，请检查数据目录。");
  QJsonObject item{
      {"id", id},
      {"kind", "image"},
      {"path", file},
      {"output", output},
      {"created", QDateTime::currentDateTime().toString(Qt::ISODate)},
      {"width", image.width()},
      {"height", image.height()}};
  history.prepend(item);
  if (!prune()) {
    history.removeFirst();
    QFile::remove(file);
    throw std::runtime_error("无法保存截图历史索引，请检查数据目录。");
  }
  return item;
}
void Store::addVideo(const QJsonObject &item) {
  history.prepend(item);
  if (!prune()) {
    history.removeFirst();
    emit ioFailed("无法保存录屏历史索引，请检查数据目录。");
  }
}
void Store::removeRecord(const QString &id) {
  for (int n = history.size() - 1; n >= 0; --n)
    if (history[n].toObject()["id"].toString() == id)
      history.removeAt(n);
  saveHistory();
}
bool Store::prune() {
  auto kept = history;
  QStringList removed;
  auto limit = qMax(1, config["history_limit"].toInt(100));
  qint64 bytes = 0;
  for (const auto &value : kept) {
    auto item = value.toObject();
    bytes += QFileInfo(item[item["kind"] == "image" ? "path" : "thumbnail"].toString()).size();
  }
  auto maxBytes = qint64(config["cache_limit_mb"].toInt(512)) * 1024 * 1024;
  while (kept.size() > limit || (bytes > maxBytes && kept.size() > 1)) {
    auto item = kept.last().toObject();
    kept.removeLast();
    auto file = item[item["kind"] == "image" ? "path" : "thumbnail"].toString();
    bytes -= QFileInfo(file).size();
    if (QFileInfo(file).absolutePath() == QFileInfo(path("history")).absoluteFilePath())
      removed.append(file);
  }
  if (!writeJson(path("history.json"), kept)) return false;
  history = kept;
  if (!removed.isEmpty()) submitIo([removed] {
    for (const auto &file : removed)
      if (QFile::exists(file) && !QFile::remove(file))
        return QString("无法清理截图历史缓存：%1").arg(file);
    return QString();
  });
  return true;
}

Store::~Store() { flushIo(); }

// Only immutable images and value snapshots enter the worker. GUI state stays here.
void Store::submitIo(std::function<QString()> work,
                    std::function<void(const QString &)> completed) {
  ++pendingIoJobs;
  ioPool.start([this, work = std::move(work), completed = std::move(completed)] {
    QString error;
    try { error = work(); }
    catch (const std::exception &e) { error = QString::fromUtf8(e.what()); }
    catch (...) { error = "图片保存失败。"; }
    {
      std::scoped_lock lock(completionMutex);
      ioCompletions.push_back([this, completed, error] {
        --pendingIoJobs;
        if (completed) completed(error);
        if (!error.isEmpty()) emit ioFailed(error);
      });
    }
    QMetaObject::invokeMethod(this, [this] { drainIoResults(); }, Qt::QueuedConnection);
  });
}
void Store::drainIoResults() {
  std::vector<std::function<void()>> completed;
  {
    std::scoped_lock lock(completionMutex);
    completed.swap(ioCompletions);
  }
  for (auto &callback : completed) callback();
}
void Store::flushIo() {
  // Also runs GUI completions when the application event loop has stopped.
  // A completion may enqueue another write, so drain until both queues are empty.
  while (pendingIoJobs > 0) {
    ioPool.waitForDone();
    drainIoResults();
  }
}
bool Store::writeImage(const QString &file, const QImage &image) {
  if (image.isNull() || !QDir().mkpath(QFileInfo(file).absolutePath())) return false;
  QSaveFile output(file);
  auto format = QFileInfo(file).suffix().toLatin1();
  if (format.isEmpty()) format = "PNG";
  return output.open(QIODevice::WriteOnly) && image.save(&output, format.constData()) && output.commit();
}
void Store::addImageAsync(const QImage &image, const QString &output) {
  auto id = QUuid::createUuid().toString(QUuid::Id128);
  auto file = path("history/" + id + ".png");
  QJsonObject item{{"id", id}, {"kind", "image"}, {"path", file}, {"output", output},
      {"created", QDateTime::currentDateTime().toString(Qt::ISODate)},
      {"width", image.width()}, {"height", image.height()}};
  submitIo([image, output, file] {
    if (!output.isEmpty() && !writeImage(output, image))
      return QString("自动保存截图失败，请检查目录权限。贴图已保留。\n%1").arg(output);
    if (!writeImage(file, image))
      return QString("无法保存截图历史，请检查数据目录。贴图已保留。\n%1").arg(file);
    return QString();
  }, [this, item, output, file](const QString &error) {
    pendingOutputPaths.remove(output);
    if (!error.isEmpty()) return;
    history.prepend(item);
    if (!prune()) {
      history.removeFirst();
      submitIo([file] {
        if (QFile::exists(file) && !QFile::remove(file))
          return QString("无法清理未登记的截图缓存：%1").arg(file);
        return QString();
      });
      emit ioFailed("无法保存截图历史索引，请检查数据目录。贴图已保留。");
      return;
    }
    emit historyChanged();
  });
}
