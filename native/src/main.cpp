#include "application.h"
#include "native_windows.h"
#include "startup_log.h"
#include "login_startup.h"
#include <QtCore/QCommandLineParser>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtWidgets/QApplication>
#include <cstdio>
#ifdef Q_OS_MACOS
#include "screen_capture.h"
#endif

static int runApplication(int argc, char **argv) {
  enableDpi();
  QApplication app(argc, argv);
#ifdef Q_OS_MACOS
  const bool loginLaunch = macLoginLaunch();
#endif
  app.setApplicationName("picut");
  app.setApplicationDisplayName("picut");
  app.setOrganizationName("picut");
  app.setApplicationVersion("0.5.56");
  // Retain the accepted scrolling profile so settings, history and pins carry forward.
  if (qEnvironmentVariableIsEmpty("FLOATSHOT_DATA_DIR"))
    qputenv("FLOATSHOT_DATA_DIR", (QStandardPaths::writableLocation(
                QStandardPaths::GenericDataLocation) +
                "/FloatShot/FloatShotNative-ScrollPerformanceTrial").toUtf8());
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
  parser.addOption(QCommandLineOption("verify-pin-latency", "Measure synthetic pin shortcut latency with ninety history records."));
#ifdef Q_OS_MACOS
  parser.addOption(QCommandLineOption("verify-recording", "Encode a synthetic MP4 and exit."));
  parser.addOption(QCommandLineOption("verify-scroll", "Stitch synthetic scrolling frames and exit."));
  parser.addOption(QCommandLineOption("verify-translation", "Translate synthetic screenshots and exit."));
#endif
  parser.process(app);
#ifdef Q_OS_MACOS
  if (parser.isSet("verify-recording"))
    return verifyMacRecording(qEnvironmentVariable("FLOATSHOT_DATA_DIR")) ? 0 : 1;
  if (parser.isSet("verify-scroll"))
    return verifyMacScroll(qEnvironmentVariable("FLOATSHOT_DATA_DIR")) ? 0 : 1;
#endif
  bool checking = parser.isSet("verify-pin-latency") || parser.isSet("verify-desktop-memory") || parser.isSet("verify-ocr-memory") ||
                  parser.isSet("verify-ui") ||
                  parser.isSet("verify-features") ||
                  parser.isSet("verify-memory") || parser.isSet("smoke-test");
#ifdef Q_OS_MACOS
  checking = checking || parser.isSet("verify-translation");
#endif
  ApplicationController controller(checking);
#ifdef Q_OS_MACOS
  if (parser.isSet("verify-translation"))
    return verifyMacTranslation(&controller.store) ? 0 : 1;
#endif
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
      bool show = parser.isSet("show-home");
#ifdef Q_OS_MACOS
      show = show || (!parser.isSet("tray") && !loginLaunch);
#endif
      socket.write(show ? "home\n" : "tray\n");
      socket.waitForBytesWritten(250);
      return 0;
    }
    QLocalServer::removeServer(lock);
    if (!server.listen(lock))
      return 1;
    // Only the normal primary instance owns login registration. Verification
    // commands and secondary launches must not change the user's startup list.
    if (!controller.store.saveConfig())
      qWarning() << "Could not persist startup preference.";
    QString startupError;
    if (!setLoginStartupEnabled(controller.store.config["startup"].toBool(true),
                               QCoreApplication::applicationFilePath(), &startupError))
      qWarning().noquote() << startupError;
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
  bool show = parser.isSet("show-home") || parser.isSet("smoke-test");
#ifdef Q_OS_MACOS
  show = show || (!parser.isSet("tray") && !loginLaunch);
#endif
  if (show)
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
#ifdef Q_OS_MACOS
    std::fprintf(stderr, "%s\n", qPrintable(message));
#else
    MessageBoxW(nullptr,reinterpret_cast<LPCWSTR>(message.utf16()),L"picut 启动失败",MB_OK|MB_ICONERROR);
#endif
    return 1;
  }
}
