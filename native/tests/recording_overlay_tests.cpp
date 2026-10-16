#include "native_windows.h"
#include "recording_overlay.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <dwmapi.h>

class RecordingOverlayTests : public QObject {
  Q_OBJECT
  const QColor canvasColor = QColor(62, 136, 178);
  QString artifacts;
  QImage grabCanvas(QRect logical) {
    auto physical = physicalRect(logical);
    HDC screen = GetDC(nullptr);
    HDC target = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = physical.width();
    info.bmiHeader.biHeight = -physical.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap =
        CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
      DeleteDC(target);
      ReleaseDC(nullptr, screen);
      return {};
    }
    auto previous = SelectObject(target, bitmap);
    bool ok = BitBlt(target, 0, 0, physical.width(), physical.height(), screen,
                     physical.x(), physical.y(), SRCCOPY | CAPTUREBLT);
    QImage image;
    if (ok)
      image =
          QImage(static_cast<uchar *>(bits), physical.width(),
                 physical.height(), physical.width() * 4, QImage::Format_RGB32)
              .copy();
    SelectObject(target, previous);
    DeleteObject(bitmap);
    DeleteDC(target);
    ReleaseDC(nullptr, screen);
    return image;
  }
  bool canvasVisible(QRect region) {
    const auto image = grabCanvas(region);
    if (image.isNull())
      return false;
    // Validate the fixture before writing media: an inactive compositor can
    // return stale desktop pixels despite an exposed Qt window.
    for (int y : {1, image.height() / 2, image.height() - 2})
      for (int x : {1, image.width() / 2, image.width() - 2}) {
        auto pixel = image.pixelColor(x, y);
        if (qAbs(pixel.red() - canvasColor.red()) > 3 ||
            qAbs(pixel.green() - canvasColor.green()) > 3 ||
            qAbs(pixel.blue() - canvasColor.blue()) > 3)
          return false;
      }
    return true;
  }
  void savePreview(RecordingOverlay &overlay, const QString &name) {
    if (artifacts.isEmpty())
      return;
    QImage preview(overlay.region().size(), QImage::Format_RGB32);
    preview.fill(canvasColor);
    QPainter painter(&preview);
    painter.drawPixmap(0, 0, overlay.boundary()->grab());
    painter.drawPixmap(overlay.controls()->pos() - overlay.region().topLeft(),
                       overlay.controls()->grab());
    painter.end();
    preview.save(artifacts + "/" + name + ".png");
  }
  void recordAndCheck(QRect region, const QString &name, bool checkPause,
                      bool liveCapture) {
    QTemporaryDir dir;
    QWidget canvas(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                                Qt::WindowStaysOnTopHint);
    if (liveCapture) {
      auto palette = canvas.palette();
      palette.setColor(QPalette::Window, canvasColor);
      canvas.setPalette(palette);
      canvas.setAutoFillBackground(true);
      canvas.setGeometry(region);
      canvas.show();
      canvas.raise();
      QVERIFY(QTest::qWaitForWindowExposed(&canvas, 3000));
      DwmFlush();
      bool visible = false;
      for (int attempt = 0; attempt < 20 && !visible; ++attempt) {
        QTest::qWait(100);
        visible = canvasVisible(region);
      }
      if (!visible)
        QSKIP("The Windows desktop compositor is not showing the test canvas "
              "to GDI. Live video exclusion cannot be verified in this "
              "session; no desktop video was saved.");
    }
    Recorder recorder(dir.path() + "/recording");
    RecordingOverlay overlay(&recorder, region, false, false);
    QVERIFY(overlay.show());
    QVERIFY(overlay.captureExcluded());
    QCOMPARE(overlay.boundary()->geometry(), region);
    QVERIFY(region.contains(overlay.controls()->geometry()));
    auto frameWindow = reinterpret_cast<HWND>(overlay.boundary()->winId());
    QVERIFY(GetWindowLongPtrW(frameWindow, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    for (auto widget : {overlay.boundary(), overlay.controls()}) {
      DWORD affinity = 0;
      QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(widget->winId()),
                                       &affinity));
      QCOMPARE(affinity, DWORD(WDA_EXCLUDEFROMCAPTURE));
    }
    auto time = overlay.controls()->findChild<QLabel *>("recordingTime");
    auto state = overlay.controls()->findChild<QLabel *>("recordingState");
    auto pause = overlay.controls()->findChild<QPushButton *>("pauseRecording");
    auto stop = overlay.controls()->findChild<QPushButton *>("stopRecording");
    QVERIFY(time && state && pause && stop);
    QCOMPARE(time->text(), QString("00:00:00"));
    QVERIFY(!pause->isEnabled());
    QVERIFY(!stop->isEnabled());
    RecordOptions options;
    options.region = physicalRect(region);
    options.output = dir.path() + "/" + name + ".mp4";
    options.cursor = false;
    options.fps = 15;
    QSignalSpy started(&recorder, &Recorder::started);
    QSignalSpy finished(&recorder, &Recorder::finished);
    QSignalSpy failed(&recorder, &Recorder::failed);
    std::function<QImage()> source;
    if (!liveCapture) {
      QImage testFrame(options.region.size(), QImage::Format_RGB32);
      testFrame.fill(canvasColor);
      source = [testFrame] { return testFrame; };
    }
    recorder.start(options, source);
    QVERIFY2(started.wait(10000), "Native screen recording did not start.");
    QTRY_VERIFY_WITH_TIMEOUT(time->text() != "00:00:00", 4000);
    savePreview(overlay, name + "-controls");
    if (checkPause) {
      QTest::mouseClick(pause, Qt::LeftButton);
      QVERIFY(recorder.paused());
      QCOMPARE(pause->text(), QString("继续"));
      QCOMPARE(state->text(), QString("已暂停"));
      auto pausedTime = time->text();
      auto elapsed = recorder.elapsed();
      QTest::qWait(350);
      QCOMPARE(time->text(), pausedTime);
      QVERIFY(qAbs(recorder.elapsed() - elapsed) < .01);
      savePreview(overlay, name + "-paused");
      QTest::mouseClick(pause, Qt::LeftButton);
      QVERIFY(!recorder.paused());
      QCOMPARE(pause->text(), QString("暂停"));
      // Hotkeys invoke Recorder directly. The controls must still synchronize.
      recorder.pause();
      QTRY_COMPARE_WITH_TIMEOUT(pause->text(), QString("继续"), 500);
      recorder.pause();
      QTRY_COMPARE_WITH_TIMEOUT(pause->text(), QString("暂停"), 500);
    }
    QTest::qWait(180);
    QList<QPoint> samplePoints;
    for (int x :
         {1, region.width() / 4, region.width() / 2, region.width() - 2})
      for (int y : {1, region.height() / 2, region.height() - 2})
        samplePoints.append({x, y});
    for (auto widget :
         {static_cast<QWidget *>(time), static_cast<QWidget *>(pause),
          static_cast<QWidget *>(stop)})
      for (int x : {2, widget->width() / 2, widget->width() - 3})
        samplePoints.append(
            widget->mapToGlobal(QPoint(x, widget->height() / 2)) -
            region.topLeft());
    QTest::mouseClick(stop, Qt::LeftButton);
    QVERIFY(!stop->isEnabled());
    QVERIFY(!pause->isEnabled());
    QCOMPARE(state->text(), QString("正在保存"));
    QVERIFY2(finished.wait(15000),
             failed.isEmpty() ? "Recording did not finish"
                              : qPrintable(failed.first().first().toString()));
    QCOMPARE(failed.size(), 0);
    QVERIFY(!overlay.boundary()->isVisible());
    QVERIFY(!overlay.controls()->isVisible());
    auto result = finished.first().first().toJsonObject();
    int width = result["width"].toInt(), height = result["height"].toInt();
    QProcess decode;
    decode.start(Recorder::ffmpegPath(),
                 {"-v", "error", "-i", options.output, "-vf", "fps=1",
                  "-frames:v", "2", "-f", "rawvideo", "-pix_fmt", "rgb24",
                  "pipe:1"});
    QVERIFY(decode.waitForFinished(10000));
    QCOMPARE(decode.exitCode(), 0);
    QByteArray pixels = decode.readAllStandardOutput();
    qint64 bytes = qint64(width) * height * 3;
    QVERIFY(pixels.size() >= bytes);
    int count = int(pixels.size() / bytes);
    for (int n = 0; n < count; ++n)
      for (auto point : samplePoints) {
        int x = qBound(0, qRound(point.x() * double(width) / region.width()),
                       width - 1);
        int y = qBound(0, qRound(point.y() * double(height) / region.height()),
                       height - 1);
        auto pixel = reinterpret_cast<const uchar *>(
            pixels.constData() + n * bytes + (qint64(y) * width + x) * 3);
        QVERIFY2(qAbs(int(pixel[0]) - canvasColor.red()) <= 8 &&
                     qAbs(int(pixel[1]) - canvasColor.green()) <= 8 &&
                     qAbs(int(pixel[2]) - canvasColor.blue()) <= 8,
                 qPrintable(QString("%1 frame %2 pixel(%3,%4) was %5,%6,%7; "
                                    "unexpected video pixels")
                                .arg(name)
                                .arg(n)
                                .arg(x)
                                .arg(y)
                                .arg(pixel[0])
                                .arg(pixel[1])
                                .arg(pixel[2])));
      }
    if (!artifacts.isEmpty()) {
      QImage image(reinterpret_cast<const uchar *>(pixels.constData()), width,
                   height, width * 3, QImage::Format_RGB888);
      image.save(artifacts + "/" + name + "-decoded.png");
      result.remove("path");
      result.remove("thumbnail");
      result["decoded_frames_checked"] = count;
      result["capture_affinity_verified"] = overlay.captureExcluded();
      result["live_capture_exclusion_verified"] = liveCapture;
      result["source"] =
          liveCapture ? "test canvas on desktop" : "synthetic test frame";
      QFile report(artifacts + "/" + name + ".json");
      QVERIFY(report.open(QIODevice::WriteOnly));
      report.write(QJsonDocument(result).toJson());
    }
  }
  QRect standardRegion() {
    return {QGuiApplication::primaryScreen()->availableGeometry().topLeft() +
                QPoint(90, 100),
            QSize(640, 400)};
  }
private slots:
  void initTestCase() {
    QVERIFY2(QGuiApplication::platformName() == "windows",
             "Use the real Windows Qt platform for recording UI tests.");
    QVERIFY(QFile::exists(Recorder::ffmpegPath()));
    artifacts = qEnvironmentVariable("FLOATSHOT_RECORDING_TEST_DIR");
    if (!artifacts.isEmpty())
      QDir().mkpath(artifacts);
  }
  void regionControlsTimePauseAndSave() {
    recordAndCheck(standardRegion(), "region", true, false);
  }
  void fullscreenControlsInsideScreen() {
    recordAndCheck(QGuiApplication::primaryScreen()->geometry(), "fullscreen",
                   false, false);
  }
  void liveRegionCaptureExclusion() {
    recordAndCheck(standardRegion(), "live-region", false, true);
  }
  void liveFullscreenCaptureExclusion() {
    recordAndCheck(QGuiApplication::primaryScreen()->geometry(),
                   "live-fullscreen", false, true);
  }
  void liveSecondaryScreenCaptureExclusion() {
    auto screens = QGuiApplication::screens();
    if (screens.size() < 2)
      QSKIP("No secondary monitor is available.");
    recordAndCheck(screens[1]->geometry(), "live-secondary", false, true);
  }
  void dragControllerAndDestroyWindows() {
    QTemporaryDir dir;
    Recorder recorder(dir.path());
    auto region = standardRegion();
    QPointer<QWidget> frame, panel;
    {
      RecordingOverlay overlay(&recorder, region, true, true);
      QVERIFY(overlay.show());
      frame = overlay.boundary();
      panel = overlay.controls();
      auto grip = panel->findChild<QLabel *>("recordingGrip");
      QVERIFY(grip);
      auto old = panel->pos();
      QPointF origin = grip->mapToGlobal(grip->rect().center());
      QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier,
                        grip->rect().center());
      QMouseEvent move(QEvent::MouseMove, QPointF(grip->rect().center()),
                       origin + QPointF(35, 70), Qt::NoButton, Qt::LeftButton,
                       Qt::NoModifier);
      QApplication::sendEvent(grip, &move);
      QCOMPARE(panel->pos(), old + QPoint(35, 70));
      QVERIFY(region.contains(panel->geometry()));
      QMouseEvent farMove(QEvent::MouseMove, QPointF(),
                          origin + QPointF(10000, 10000), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(grip, &farMove);
      QTest::mouseRelease(grip, Qt::LeftButton);
      QVERIFY(region.contains(panel->geometry()));
      QVERIFY(panel->findChild<QToolButton *>("muteSystemAudio"));
      QVERIFY(panel->findChild<QToolButton *>("muteMicrophone"));
    }
    QVERIFY(frame.isNull());
    QVERIFY(panel.isNull());
  }
  void smallRegionKeepsControllerOnItsMonitor() {
    QTemporaryDir dir;
    Recorder recorder(dir.path());
    auto screen = QGuiApplication::screens().last();
    QRect region(screen->geometry().bottomRight() - QPoint(49, 49),
                 QSize(50, 50));
    RecordingOverlay overlay(&recorder, region, false, false);
    QVERIFY(overlay.show());
    QCOMPARE(overlay.boundary()->geometry(), region);
    QVERIFY(
        screen->availableGeometry().contains(overlay.controls()->geometry()));
  }
  void failureHidesController() {
    QTemporaryDir dir;
    Recorder recorder(dir.path());
    RecordingOverlay overlay(&recorder, standardRegion(), false, false);
    QVERIFY(overlay.show());
    QSignalSpy failed(&recorder, &Recorder::failed);
    RecordOptions options;
    options.region = QRect(0, 0, 160, 90);
    options.output = dir.path() + "/failure.mp4";
    recorder.start(options, [] { return QImage(); });
    QVERIFY(failed.wait(5000));
    QVERIFY(!overlay.controls()->isVisible());
    QVERIFY(!overlay.boundary()->isVisible());
  }
};

int main(int argc, char **argv) {
  enableDpi();
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  app.setQuitOnLastWindowClosed(false);
  RecordingOverlayTests tests;
  return QTest::qExec(&tests, argc, argv);
}
#include "recording_overlay_tests.moc"
