#include "application.h"
#include "native_windows.h"
#include "startup_log.h"
#include <QtCore/QCommandLineParser>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtWidgets/QApplication>

static int runApplication(int argc, char **argv) {
  enableDpi();
  QApplication app(argc, argv);
  app.setApplicationName("picut");
  app.setApplicationDisplayName("picut 翻译试用版");
  app.setOrganizationName("picut");
  app.setApplicationVersion("0.5.25");
  // Translation is an isolated trial. The stable v0.5.21 profile is untouched.
  if (qEnvironmentVariableIsEmpty("FLOATSHOT_DATA_DIR"))
    qputenv("FLOATSHOT_DATA_DIR", (QStandardPaths::writableLocation(
                QStandardPaths::GenericDataLocation) +
                "/FloatShot/FloatShotNative-TranslationTrial").toUtf8());
  app.setQuitOnLastWindowClosed(false);
  app.setStyle("Fusion");
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(QCommandLineOption("tray", "Start in the system tray."));
  parser.addOption(QCommandLineOption("show-home", "Open the workbench explicitly."));
  parser.addOption(
      QCommandLineOption("smoke-test", "Render the home and exit."));
  parser.addOption(QCommandLineOption(
      "verify-features", "Verify synthetic capture and pin workflows."));
  parser.addOption(QCommandLineOption(
      "verify-memory",
      "Profile twenty synthetic screenshots without changing the clipboard."));
  parser.addOption(QCommandLineOption(
      "verify-ui", "Render synthetic screenshots of the native UI."));
  parser.addOption(QCommandLineOption("verify-ocr-memory", "Verify isolated offline OCR and measure memory without user screen pixels."));
  parser.addOption(
      QCommandLineOption("verify-desktop-memory",
                         "Profile twenty desktop captures without writing "
                         "screen pixels or modifying the clipboard."));
  parser.addOption(QCommandLineOption("verify-pin-latency", "Measure synthetic F3 latency with ninety history records."));
  parser.process(app);
  bool checking = parser.isSet("verify-pin-latency") || parser.isSet("verify-desktop-memory") || parser.isSet("verify-ocr-memory") ||
                  parser.isSet("verify-ui") ||
                  parser.isSet("verify-features") ||
                  parser.isSet("verify-memory") || parser.isSet("smoke-test");
  ApplicationController controller(checking);
  if (parser.isSet("verify-pin-latency"))
    return verifyPinLatency(controller, controller.store.root) ? 0 : 1;
  if (parser.isSet("verify-ocr-memory"))
    return verifyOcrMemory(controller, controller.store.root) ? 0 : 1;
  if (parser.isSet("verify-desktop-memory"))
    return verifyDesktopMemory(controller, controller.store.root) ? 0 : 1;
  if (parser.isSet("verify-ui"))
    return verifyNativeUi(controller, controller.store.root) ? 0 : 1;
  if (parser.isSet("verify-features") || parser.isSet("verify-memory"))
    return verifyNativeFeatures(controller, controller.store.root,
                                parser.isSet("verify-memory"))
               ? 0
               : 1;
  QLocalServer server;
  auto lock = "FloatShotNative-" +
              QString::fromLatin1(
                  QCryptographicHash::hash(controller.store.root.toUtf8(),
                                           QCryptographicHash::Sha256)
                      .toHex()
                      .left(16));
  if (!checking) {
    QLocalSocket socket;
    socket.connectToServer(lock);
    if (socket.waitForConnected(250)) {
      socket.write(parser.isSet("show-home") ? "home\n" : "tray\n");
      socket.waitForBytesWritten(250);
      return 0;
    }
    QLocalServer::removeServer(lock);
    if (!server.listen(lock))
      return 1;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
      auto connection = server.nextPendingConnection();
      if (connection) {
        QObject::connect(connection, &QLocalSocket::disconnected, connection, &QObject::deleteLater);
        auto receive = [connection, &controller] {
          if (!connection->canReadLine()) return;
          if (connection->readLine().trimmed() == "home") controller.showHome();
          connection->disconnectFromServer();
        };
        QObject::connect(connection, &QLocalSocket::readyRead, connection, receive);
        receive();
      }
    });
  }
  if (parser.isSet("show-home") || parser.isSet("smoke-test"))
    controller.showHome();
  if (parser.isSet("smoke-test"))
    QTimer::singleShot(500, &app, [&] {
      controller.home->grab().save(controller.store.path("home.png"));
      app.quit();
    });
  return app.exec();
}

int main(int argc, char **argv) {
  installStartupLog();
  try { return runApplication(argc, argv); }
  catch (const std::exception &error) {
    const auto message=QString::fromUtf8(error.what())+"\n启动日志："+startupLogPath();
    qCritical().noquote()<<message;
    MessageBoxW(nullptr,reinterpret_cast<LPCWSTR>(message.utf16()),L"picut 启动失败",MB_OK|MB_ICONERROR);
    return 1;
  }
}
