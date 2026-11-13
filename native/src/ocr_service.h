#pragma once
#include "recognition.h"
#include "ocr_shared_image.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <deque>
#include <algorithm>
#include <memory>
#include <optional>

// One serial service serves screenshots, OCR pins and translation. Models stay
// in one child process; UI objects own requests, never the model's lifetime.
class OcrService : public QObject {
  struct Request {
    QPointer<PaddleOcrJob> owner;
    quint64 token = 0, id = 0;
    QImage image;
    QString profile, gpuProgram, acceleration, fallbackError;
    int rotation = 0;
    bool gpu = false, sent = false, warmAtSubmit = false;
    QElapsedTimer elapsed;
    std::unique_ptr<OcrSharedImage> shared;
  };
  QProcess *process = nullptr;
  QTimer idle, watchdog;
  QByteArray buffer;
  QString workerKey;
  std::deque<Request> queue;
  std::optional<Request> current;
  quint64 serial = 0;
  qint64 loadMs = 0;
  bool ready = false, used = false;
  int idleMs = 30000;

  explicit OcrService(QObject *parent) : QObject(parent) {
    // A short value is useful to verify real worker release in isolated tests.
    bool custom = false;
    const auto configured = qEnvironmentVariableIntValue("PICUT_OCR_IDLE_MS", &custom);
    if (custom) idleMs = qBound(100, configured, 30000);
    idle.setSingleShot(true); watchdog.setSingleShot(true);
    connect(&idle, &QTimer::timeout, this, [this] { if (!current && queue.empty()) stopWorker(); });
    connect(&watchdog, &QTimer::timeout, this, [this] { failure("识别超过 3 分钟，已停止。请缩小截图区域后重试。"); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      queue.clear(); stopWorker(); current.reset();
    });
  }
  void stopWorker() {
    idle.stop(); watchdog.stop(); ready = used = false;
    workerKey.clear(); buffer.clear();
    if (auto old = process) {
      process = nullptr;
      old->disconnect(this);
      // No wait on the UI thread. QProcess drains/reaps the killed child.
      // Cancellation can arrive while CreateProcess is still in flight.
      connect(old, &QProcess::started, old, [old] { old->kill(); });
      connect(old, &QProcess::finished, old, &QObject::deleteLater);
      connect(old, &QProcess::errorOccurred, old, &QObject::deleteLater);
      old->kill();
      if (old->state() == QProcess::NotRunning) old->deleteLater();
    }
  }
  void later() { QTimer::singleShot(0, this, [this] { advance(); }); }
  void finish(QJsonObject result) {
    if (!current) return;
    auto request = std::move(*current); current.reset();
    result["client_total_ms"] = double(request.elapsed.elapsed());
    result["shared_memory"] = true;
    if (!request.fallbackError.isEmpty()) result["acceleration_fallback"] = true;
    if (queue.empty()) idle.start(idleMs);
    if (request.owner) request.owner->complete(request.token, result);
    later();
  }
  void failure(const QString &message) {
    stopWorker();
    if (current && current->gpu && current->acceleration == "auto") {
      current->gpu = false; current->sent = false; current->shared.reset();
      current->warmAtSubmit = false;
      current->fallbackError = message;
      if (current->owner) emit current->owner->progress("fallback", 0, 0);
      later(); return;
    }
    if (current) finish({{"error", message}});
  }
  void receive() {
    if (!process) return;
    buffer += process->readAllStandardOutput();
    if (buffer.size() > 64 * 1024 * 1024) { failure("OCR 返回数据超过限制。"); return; }
    while (true) {
      const auto end = buffer.indexOf('\n');
      if (end < 0) return;
      const auto line = buffer.left(end); buffer.remove(0, end + 1);
      QJsonParseError error;
      const auto doc = QJsonDocument::fromJson(line, &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()) { failure("OCR 通信数据无效。"); return; }
      const auto item = doc.object(); const auto event = item["event"].toString();
      if (event == "ready") {
        ready = true; loadMs = item["load_ms"].toInteger(); watchdog.stop();
        if (!current) idle.start(idleMs);
        later();
      } else if (event == "error") {
        failure(item["error"].toString()); return;
      } else if (current && item["id"].toInteger() == qint64(current->id)) {
        if (event == "progress" && current->owner)
          emit current->owner->progress(item["phase"].toString(), item["done"].toInt(), item["total"].toInt());
        else if (event == "result") {
          auto result = item["result"].toObject();
          if (result.isEmpty()) { failure("OCR 结果为空。"); return; }
          if (result.contains("error") && current->gpu && current->acceleration == "auto") {
            failure(result["error"].toString()); return;
          }
          result["worker_pid"] = process->processId();
          result["model_reused"] = used || current->warmAtSubmit;
          result["model_load_ms"] = used || current->warmAtSubmit ? 0 : loadMs;
          used = true; watchdog.stop(); finish(result);
        }
      }
      if (!process) return;
    }
  }
  void ensureWorker(const QString &profile, bool gpu, const QString &gpuProgram) {
    const auto program = gpu ? gpuProgram : QCoreApplication::applicationDirPath() + "/FloatShotPaddleOcr.exe";
    const auto key = program + "|" + profile + (gpu ? "|gpu" : "|cpu");
    if (process && workerKey == key) return;
    stopWorker(); workerKey = key;
    auto child = new QProcess(this); process = child;
    child->setProcessChannelMode(QProcess::SeparateChannels);
    child->setWorkingDirectory(QCoreApplication::applicationDirPath());
    child->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    connect(child, &QProcess::readyReadStandardOutput, this, [this, child] { if (process == child) receive(); });
    connect(child, &QProcess::readyReadStandardError, this, [child] { child->readAllStandardError(); });
    connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError error) {
      if (process == child && error == QProcess::FailedToStart) failure("无法启动文字识别组件：" + child->errorString());
    });
    connect(child, &QProcess::finished, this, [this, child](int, QProcess::ExitStatus) {
      if (process != child) return;
      // Drain a final reply before treating a premature exit as failure.
      receive();
      if (process == child) failure("识别组件意外退出，请重试。");
    });
    QStringList arguments{"--serve", "--models", PaddleOcrJob::modelsPath(), "--profile", profile,
                          "--parent-pid", QString::number(QCoreApplication::applicationPid())};
    if (gpu) arguments.append("--gpu");
    watchdog.start(180000); child->start(program, arguments);
  }
  void advance() {
    if (!current) {
      while (!queue.empty()) {
        auto next = std::move(queue.front()); queue.pop_front();
        if (next.owner && next.owner->active && next.owner->generation == next.token) {
          current.emplace(std::move(next)); break;
        }
      }
    }
    if (!current) return;
    idle.stop();
    ensureWorker(current->profile, current->gpu, current->gpuProgram);
    if (!ready || current->sent) return;
    try {
      current->shared = std::make_unique<OcrSharedImage>();
      auto request = current->shared->create(current->image);
      request["id"] = qint64(current->id); request["rotation"] = current->rotation;
      const auto data = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
      if (process->write(data) != data.size()) throw std::runtime_error("无法发送 OCR 请求。");
      current->sent = true; watchdog.start(180000);
    } catch (const std::exception &error) { finish({{"error", QString::fromUtf8(error.what())}}); }
  }
public:
  ~OcrService() override { stopWorker(); }
  void releaseIdleModel() { if (!current && queue.empty()) stopWorker(); }
  static OcrService *instance(bool create = true) {
    static QPointer<OcrService> service;
    if (!service && create) service = new OcrService(QCoreApplication::instance());
    return service;
  }
  void submit(PaddleOcrJob *owner, const QImage &image, const QString &profile, int rotation, const QString &acceleration) {
    Request request;
    request.owner = owner; request.token = owner->generation; request.id = ++serial;
    request.image = image; request.profile = profile; request.rotation = rotation;
    request.acceleration = acceleration; request.gpuProgram = owner->gpuProgram;
    // A warm CPU avoids DirectML graph compilation for normal screenshots.
    // Keep explicit GPU support and use it automatically for very large pages.
    request.gpu = acceleration == "gpu" || (acceleration == "auto" &&
        qint64(image.width()) * image.height() > 4000000 && QFileInfo::exists(request.gpuProgram));
    const auto program=request.gpu?request.gpuProgram:QCoreApplication::applicationDirPath()+"/FloatShotPaddleOcr.exe";
    request.warmAtSubmit=ready && workerKey==program+"|"+profile+(request.gpu?"|gpu":"|cpu");
    request.elapsed.start(); queue.push_back(std::move(request)); idle.stop(); later();
  }
  void cancel(PaddleOcrJob *owner, quint64 token) {
    std::erase_if(queue, [=](const Request &request) { return request.owner == owner && request.token == token; });
    if (current && current->owner == owner && current->token == token) {
      stopWorker(); current.reset(); later();
    }
  }
  void prewarm(const QString &profile, const QString &acceleration) {
    if (current || !queue.empty() || !PaddleOcrJob::profiles().contains(profile)) return;
    idle.stop();
    ensureWorker(profile, acceleration == "gpu", QCoreApplication::applicationDirPath() + "/gpu/FloatShotPaddleOcr.exe");
    if (ready) idle.start(idleMs);
  }
};
