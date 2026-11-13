#include "native_windows.h"
#include "recording_overlay.h"
#include "recording_capture.h"
#include "recording_clicks.h"
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
    if (overlay.artwork() && overlay.artwork()->isVisible()) painter.drawPixmap(0,0,overlay.artwork()->grab());
    painter.drawPixmap(overlay.controls()->pos() - overlay.region().topLeft(),
                       overlay.controls()->grab());
    painter.end();
    preview.save(artifacts + "/" + name + ".png");
  }
  void recordAndCheck(QRect region, const QString &name, bool checkPause,
                      bool liveCapture, bool compatibility = false,bool carryArtwork = false) {
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
    RecordOptions options;
    options.region = physicalRect(region);
    options.output = dir.path() + "/" + name + ".mp4";
    options.cursor = false;
    options.fps = 15;
    QImage expectedFrame(options.region.size(),QImage::Format_RGB32); expectedFrame.fill(canvasColor);
    QImage artwork;
    if (carryArtwork) {
      const QRect mark(options.region.width()/3,options.region.height()/3,options.region.width()/3,options.region.height()/3);
      options.painter=[mark](QImage &image) { QPainter p(&image); p.fillRect(mark,QColor(255,0,4,100)); };
      artwork=QImage(options.region.size(),QImage::Format_ARGB32_Premultiplied); artwork.fill(Qt::transparent);
      options.painter(artwork); options.painter(expectedFrame);
    }
    RecordingOverlay overlay(&recorder, region, false, false, nullptr,
        compatibility ? std::function<bool(QWidget *)>([](QWidget *){return false;}) : std::function<bool(QWidget *)>());
    overlay.setArtwork(artwork);
    QVERIFY(overlay.show());
    if (carryArtwork) {
      QVERIFY(overlay.artwork()); QCOMPARE(overlay.artwork()->isVisible(),!compatibility);
      QVERIFY(GetWindowLongPtrW(reinterpret_cast<HWND>(overlay.artwork()->winId()),GWL_EXSTYLE)&WS_EX_TRANSPARENT);
    }
    if (compatibility) {
      QVERIFY(overlay.compatibilityMode()); QVERIFY(!overlay.captureExcluded());
      if (overlay.controls()->isVisible()) QVERIFY(!region.intersects(overlay.controls()->geometry()));
    } else {
      QVERIFY(overlay.captureExcluded()); QCOMPARE(overlay.boundary()->geometry(), region);
      bool visible=false;
      for (auto screen:QGuiApplication::screens()) visible|=screen->availableGeometry().contains(overlay.controls()->geometry());
      QVERIFY(visible);
      if (region==standardRegion()) QVERIFY(!region.intersects(overlay.controls()->geometry()));
    }
    auto frameWindow = reinterpret_cast<HWND>(overlay.boundary()->winId());
    QVERIFY(GetWindowLongPtrW(frameWindow, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    for (auto widget : {overlay.boundary(), overlay.controls()}) {
      DWORD affinity = 0;
      QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(widget->winId()),
                                       &affinity));
      QCOMPARE(affinity, DWORD(compatibility ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE));
    }
    auto time = overlay.controls()->findChild<QLabel *>("recordingTime");
    auto state = overlay.controls()->findChild<QLabel *>("recordingState");
    auto pause = overlay.controls()->findChild<QToolButton *>("pauseRecording");
    auto stop = overlay.controls()->findChild<QToolButton *>("stopRecording");
    QVERIFY(time && state && pause && stop);
    QCOMPARE(time->text(), QString("00:00:00"));
    QVERIFY(!pause->isEnabled());
    QVERIFY(!stop->isEnabled());
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
      QCOMPARE(pause->toolTip(), QString("继续录屏 · Ctrl+Alt+P"));
      QCOMPARE(state->text(), QString("已暂停"));
      auto pausedTime = time->text();
      auto elapsed = recorder.elapsed();
      QTest::qWait(350);
      QCOMPARE(time->text(), pausedTime);
      QVERIFY(qAbs(recorder.elapsed() - elapsed) < .01);
      savePreview(overlay, name + "-paused");
      QTest::mouseClick(pause, Qt::LeftButton);
      QVERIFY(!recorder.paused());
      QCOMPARE(pause->toolTip(), QString("暂停录屏 · Ctrl+Alt+P"));
      // Hotkeys invoke Recorder directly. The controls must still synchronize.
      recorder.pause();
      QTRY_COMPARE_WITH_TIMEOUT(pause->toolTip(), QString("继续录屏 · Ctrl+Alt+P"), 500);
      recorder.pause();
      QTRY_COMPARE_WITH_TIMEOUT(pause->toolTip(), QString("暂停录屏 · Ctrl+Alt+P"), 500);
    }
    QTest::qWait(180);
    QList<QPoint> samplePoints;
    if (carryArtwork) samplePoints.append({region.width()/2,region.height()/2});
    for (int x :
         {1, region.width() / 4, region.width() / 2, region.width() - 2})
      for (int y : {1, region.height() / 2, region.height() - 2})
        samplePoints.append({x, y});
    for (auto widget :
         {static_cast<QWidget *>(time), static_cast<QWidget *>(pause),
          static_cast<QWidget *>(stop)})
      for (int x : {2, widget->width() / 2, widget->width() - 3})
        if (!compatibility) {
          const auto point=widget->mapToGlobal(QPoint(x,widget->height()/2));
          if (region.contains(point)) samplePoints.append(point-region.topLeft());
        }
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
        const auto expected=expectedFrame.pixelColor(
            qBound(0,qRound(point.x()*double(expectedFrame.width())/region.width()),expectedFrame.width()-1),
            qBound(0,qRound(point.y()*double(expectedFrame.height())/region.height()),expectedFrame.height()-1));
        QVERIFY2(qAbs(int(pixel[0]) - expected.red()) <= 8 &&
                     qAbs(int(pixel[1]) - expected.green()) <= 8 &&
                     qAbs(int(pixel[2]) - expected.blue()) <= 8,
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
  QList<QImage> decodeFrames(const QString &path,int width,int height) {
    QProcess decode;
    decode.start(Recorder::ffmpegPath(),{"-v","error","-i",path,"-f","rawvideo","-pix_fmt","rgb24","pipe:1"});
    if (!decode.waitForFinished(15000) || decode.exitCode()!=0) return {};
    const auto bytes=decode.readAllStandardOutput();
    const qint64 frameBytes=qint64(width)*height*3;
    QList<QImage> result;
    for (qint64 offset=0;offset+frameBytes<=bytes.size();offset+=frameBytes)
      result.append(QImage(reinterpret_cast<const uchar *>(bytes.constData()+offset),width,height,width*3,QImage::Format_RGB888).copy());
    return result;
  }
private slots:
  void closingTargetWindowSavesCompletedVideo() {
    QTemporaryDir dir;
    auto target=std::make_unique<QWidget>(nullptr,Qt::Tool|Qt::FramelessWindowHint);
    target->setGeometry(QRect(QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(100,100),QSize(320,240)));
    target->setAutoFillBackground(true); target->show(); QVERIFY(QTest::qWaitForWindowExposed(target.get()));
    RecordOptions options; options.window=quintptr(target->winId()); options.region=recordingWindowInfo(options.window).physical;
    options.output=dir.path()+"/closed.mp4"; options.fps=12;
    Recorder recorder(dir.path()); QSignalSpy started(&recorder,&Recorder::started),finished(&recorder,&Recorder::finished),failed(&recorder,&Recorder::failed);
    recorder.start(options); QVERIFY(started.wait(7000)); QTest::qWait(250); target.reset();
    QVERIFY2(finished.wait(15000),failed.isEmpty()?"Closing a target did not save the recording":qPrintable(failed.first().first().toString()));
    QVERIFY(failed.isEmpty()); QVERIFY(QFile::exists(options.output));
  }
  void movingRegionAndLiveMarksAreEncodedImmediately() {
    QTemporaryDir dir;
    const auto available=QGuiApplication::primaryScreen()->availableGeometry();
    const QRect region(available.topLeft()+QPoint(80,100),QSize(320,240));
    QWidget canvas(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    canvas.setGeometry(region.adjusted(0,0,400,100));
    canvas.setAutoFillBackground(true);
    auto palette=canvas.palette(); palette.setColor(QPalette::Window,QColor(220,40,40)); canvas.setPalette(palette);
    canvas.show(); QVERIFY(QTest::qWaitForWindowExposed(&canvas)); QTest::qWait(150);
    Recorder recorder(dir.path());
    RecordOptions options; options.region=physicalRect(region); options.output=dir.path()+"/live.mp4"; options.fps=12; options.cursor=false;
    options.live=std::make_shared<RecordingLiveState>(options.region);
    RecordingOverlay overlay(&recorder,region,false,false); overlay.configureLive(options.live); QVERIFY(overlay.show());
    QSignalSpy started(&recorder,&Recorder::started),finished(&recorder,&Recorder::finished),failed(&recorder,&Recorder::failed);
    recorder.start(options); QVERIFY(started.wait(7000)); QTest::qWait(350);
    // A draft's partial line, then a longer line, then deletion, all reach encoded frames.
    const auto canvasSize=options.region.size();
    auto line=[canvasSize](int length) {return [canvasSize,length](QImage &frame) {
      QPainter painter(&frame); painter.fillRect(canvasSize.width()/8,canvasSize.height()*3/4,length,8,QColor(20,30,240));
    };};
    options.live->setPainter(line(canvasSize.width()/4)); QTest::qWait(350);
    options.live->setPainter(line(canvasSize.width()/2)); QTest::qWait(350);
    options.live->setPainter({}); QTest::qWait(350);
    palette.setColor(QPalette::Window,QColor(30,190,60)); canvas.setPalette(palette); canvas.update();
    auto handle=overlay.controls()->findChild<QToolButton *>("moveRecordingRegion"); QVERIFY(handle); QVERIFY(handle->isVisible());
    const auto origin=handle->mapToGlobal(handle->rect().center());
    QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,handle->rect().center());
    QMouseEvent move(QEvent::MouseMove,handle->rect().center(),origin+QPoint(200,30),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(handle,&move); QTest::mouseRelease(handle,Qt::LeftButton);
    QCOMPARE(overlay.region().topLeft(),region.topLeft()+QPoint(200,30));
    QCOMPARE(options.live->snapshot().region.topLeft(),physicalRect(overlay.region()).topLeft());
    QTest::qWait(450); recorder.stop(); QVERIFY(finished.wait(15000)); QVERIFY(failed.isEmpty());
    const auto result=finished.first().first().toJsonObject();
    const auto frames=decodeFrames(options.output,result["width"].toInt(),result["height"].toInt());
    QVERIFY(frames.size()>12);
    bool partial=false,full=false,deleted=false,green=false;
    for (const auto &frame:frames) {
      const auto a=frame.pixelColor(frame.width()/4,frame.height()*3/4+3);
      const auto b=frame.pixelColor(frame.width()/2,frame.height()*3/4+3);
      if (a.blue()>180 && b.red()>150) partial=true;
      if (a.blue()>180 && b.blue()>180) full=true;
      if (full && a.red()>150 && a.blue()<100) deleted=true;
      if (a.green()>130 && a.red()<100) green=true;
    }
    QVERIFY(partial); QVERIFY(full); QVERIFY(deleted); QVERIFY(green);
    if (!artifacts.isEmpty()) QFile::copy(options.output,artifacts+"/live-marks-and-moved-region.mp4");
  }
  void windowCaptureFollowsResizeWithoutBordersOrOccluders() {
    QTemporaryDir dir;
    const auto position=QGuiApplication::primaryScreen()->availableGeometry().topLeft()+QPoint(90,100);
    QWidget target(nullptr,Qt::Tool|Qt::FramelessWindowHint);
    target.setGeometry(QRect(position,QSize(400,240))); target.setAutoFillBackground(true);
    auto palette=target.palette(); palette.setColor(QPalette::Window,QColor(220,40,40)); target.setPalette(palette);
    target.show(); QVERIFY(QTest::qWaitForWindowExposed(&target)); QTest::qWait(150);
    const auto id=quintptr(target.winId()); const auto info=recordingWindowInfo(id); QVERIFY(info.valid);
    Recorder recorder(dir.path()); RecordOptions options;
    options.region=info.physical; options.window=id; options.output=dir.path()+"/window.mp4"; options.fps=12; options.cursor=false;
    options.live=std::make_shared<RecordingLiveState>(options.region);
    RecordingOverlay overlay(&recorder,info.logical,false,false); overlay.configureLive(options.live,id); QVERIFY(overlay.show());
    QSignalSpy started(&recorder,&Recorder::started),finished(&recorder,&Recorder::finished),failed(&recorder,&Recorder::failed);
    recorder.start(options); QVERIFY2(started.wait(7000),failed.isEmpty()?"Window capture did not start":qPrintable(failed.first().first().toString()));
    QTest::qWait(400);
    target.move(position+QPoint(120,100)); target.resize(230,360);
    palette.setColor(QPalette::Window,QColor(30,190,60)); target.setPalette(palette); target.update();
    QTRY_COMPARE(overlay.region(),recordingWindowInfo(id).logical);
    QWidget cover(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    cover.setGeometry(target.geometry()); cover.setAutoFillBackground(true);
    auto coverPalette=cover.palette(); coverPalette.setColor(QPalette::Window,Qt::yellow); cover.setPalette(coverPalette); cover.show(); cover.raise();
    QTest::qWait(600);
    target.showMinimized(); QTRY_VERIFY(recorder.paused());
    const double elapsed=recorder.elapsed(); QTest::qWait(250); QVERIFY(qAbs(recorder.elapsed()-elapsed)<0.1);
    target.showNormal(); QTRY_VERIFY(!recorder.paused()); QTest::qWait(350);
    // A manual pause must survive minimizing and restoring the target window.
    recorder.pause(); QVERIFY(recorder.paused()); target.showMinimized(); QTest::qWait(80);
    target.showNormal(); QTest::qWait(80); QVERIFY(recorder.paused()); recorder.pause(); QVERIFY(!recorder.paused());
    recorder.stop(); QVERIFY2(finished.wait(15000),failed.isEmpty()?"Window recording did not finish":qPrintable(failed.first().first().toString()));
    QVERIFY(failed.isEmpty()); const auto result=finished.first().first().toJsonObject();
    QCOMPARE(result["width"].toInt(),options.region.width()/2*2); QCOMPARE(result["height"].toInt(),options.region.height()/2*2);
    const auto frames=decodeFrames(options.output,result["width"].toInt(),result["height"].toInt());
    bool red=false,green=false;
    for (const auto &frame:frames) {
      const auto center=frame.pixelColor(frame.width()/2,frame.height()/2);
      red=red || (center.red()>180 && center.green()<100);
      if (center.green()>130 && center.red()<100) {
        green=true;
        // Changed aspect ratio is stretched to every edge of the fixed canvas.
        for (auto point:{QPoint(4,4),QPoint(frame.width()-5,4),QPoint(4,frame.height()-5),QPoint(frame.width()-5,frame.height()-5)}) {
          const auto corner=frame.pixelColor(point); QVERIFY(corner.green()>130 && corner.red()<100);
        }
      }
      QVERIFY(!(center.red()>180 && center.green()>180));
    }
    QVERIFY(red); QVERIFY(green);
    if (!artifacts.isEmpty()) QFile::copy(options.output,artifacts+"/window-move-resize-occlusion.mp4");
  }
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
  void compatibilityRegionRecordsWithoutOverlay() {
    recordAndCheck(standardRegion(), "compatibility-region", true, true, true);
  }
  void compatibilityFullscreenRecordsWithoutOverlay() {
    recordAndCheck(QGuiApplication::primaryScreen()->geometry(), "compatibility-fullscreen", false, true, true);
  }
  void countdownShowsBoundaryAndSupportsCancel() {
    QTemporaryDir dir; Recorder recorder(dir.path());
    RecordingOverlay overlay(&recorder,standardRegion(),false,false);
    overlay.setCountdown(3); QSignalSpy canceled(&overlay,&RecordingOverlay::countdownCanceled);
    QVERIFY(overlay.show()); QVERIFY(overlay.boundary()->isVisible()); QVERIFY(overlay.captureExcluded());
    const auto panel = overlay.controls()->grab().toImage();
    const auto dpr = panel.devicePixelRatio();
    QCOMPARE(panel.pixelColor(qRound(4*dpr), qRound(overlay.controls()->height()/2.0*dpr)),
             QColor("#fbfcfe"));
    QCOMPARE(overlay.boundary()->geometry(),standardRegion());
    QCOMPARE(overlay.controls()->findChild<QLabel *>("recordingState")->text(),QString("倒计时"));
    QCOMPARE(overlay.controls()->findChild<QLabel *>("recordingTime")->text(),QString("3 秒"));
    auto stop=overlay.controls()->findChild<QToolButton *>("stopRecording");
    QVERIFY(stop->isEnabled()); QVERIFY(!stop->icon().isNull());
    QVERIFY(!overlay.controls()->findChild<QToolButton *>("pauseRecording")->isVisible());
    savePreview(overlay,"countdown-controls");
    QTest::mouseClick(stop,Qt::LeftButton); QCOMPARE(canceled.size(),1); QVERIFY(!recorder.active());
    overlay.hide(); QVERIFY(!overlay.boundary()->isVisible());
  }
  void fullscreenControlsInsideScreen() {
    recordAndCheck(QGuiApplication::primaryScreen()->geometry(), "fullscreen",
                   false, false);
  }
  void liveRegionCaptureExclusion() {
    recordAndCheck(standardRegion(), "live-region", false, true);
  }
  void liveArtworkIsVisibleAndEncodedOnce() {
    recordAndCheck(standardRegion(),"live-artwork",true,true,false,true);
  }
  void compatibilityArtworkStillEncodesWithoutDesktopLayer() {
    recordAndCheck(standardRegion(),"compatibility-artwork",false,true,true,true);
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
      QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().contains(panel->geometry()));
      QMouseEvent farMove(QEvent::MouseMove, QPointF(),
                          origin + QPointF(10000, 10000), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(grip, &farMove);
      QTest::mouseRelease(grip, Qt::LeftButton);
      QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().contains(panel->geometry()));
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
    QVERIFY(!region.intersects(overlay.controls()->geometry()));
  }
  void exteriorControlsFollowRegionAndFallbackForFullscreen() {
    QTemporaryDir dir; Recorder recorder(dir.path());
    const auto available=QGuiApplication::primaryScreen()->availableGeometry();
    QRect region(available.topLeft()+QPoint(120,150),QSize(640,400));
    RecordingOverlay overlay(&recorder,region,false,false);
    auto live=std::make_shared<RecordingLiveState>(physicalRect(region));
    overlay.configureLive(live); overlay.setCountdown(3); QVERIFY(overlay.show());
    QVERIFY(!region.intersects(overlay.controls()->geometry()));
    QVERIFY(available.contains(overlay.controls()->geometry()));
    QCOMPARE(live->snapshot().controls,physicalRect(overlay.controls()->geometry()));
    // A narrow band can fit the controller across the full screen width. Region
    // subtraction into small rectangles must not incorrectly reject it.
    region=QRect(available.left()+5,available.top()+overlay.controls()->height()+10,
                 available.width()-10,available.height()-overlay.controls()->height()-15);
    overlay.setRegion(region); overlay.refresh();
    QVERIFY(!region.intersects(overlay.controls()->geometry()));
    QVERIFY(available.contains(overlay.controls()->geometry()));
    overlay.setRegion(QGuiApplication::primaryScreen()->geometry()); overlay.refresh();
    bool outsideFits=false;
    for (auto screen:QGuiApplication::screens())
      if (!screen->geometry().intersects(overlay.region()) && screen->availableGeometry().width()>=overlay.controls()->width() && screen->availableGeometry().height()>=overlay.controls()->height()) outsideFits=true;
    if (!outsideFits) QVERIFY(overlay.region().contains(overlay.controls()->geometry()));
  }
  void clickAnimationMapsPixelsExpiresAndIgnoresExcludedInput() {
    RecordingClicks clicks;
    const auto now=RecordingClicks::Clock::now();
    const RecordingClicks::Target target{{-400,200,400,200},{-380,220,100,40},true};
    clicks.add({-200,300},target,now);
    clicks.add({0,300},target,now); // outside
    clicks.add({-350,230},target,now); // controls
    clicks.add({-200,300},{{-400,200,400,200},{},false},now); // paused/off
    QCOMPARE(clicks.count(),quint64(1));
    QImage blank(800,200,QImage::Format_RGB32); blank.fill(canvasColor);
    auto frame=blank.copy(); clicks.paint(frame,now+std::chrono::milliseconds(100));
    QVERIFY(frame.pixelColor(400,100)!=canvasColor);
    QCOMPARE(frame.pixelColor(100,100),canvasColor);
    auto expired=blank.copy(); clicks.paint(expired,now+std::chrono::milliseconds(RecordingClicks::durationMs+1));
    QCOMPARE(expired,blank);
  }
  void nativeMouseClicksAreEncodedAndSettingOffLeavesFramesUnchanged() {
    QTemporaryDir dir;
    const auto region=standardRegion();
    QWidget target(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    target.setGeometry(region); target.show(); target.raise();
    QVERIFY(QTest::qWaitForWindowExposed(&target));
    POINT original{}; QVERIFY(GetCursorPos(&original));
    const auto restore=qScopeGuard([&] { SetCursorPos(original.x,original.y); });
    const auto location=physicalRect(region).center();
    QVERIFY(SetCursorPos(location.x(),location.y()));
    for (bool enabled:{false,true}) {
      Recorder recorder(dir.path()+"/recording");
      RecordOptions options; options.region=physicalRect(region); options.cursor=false;
      options.clickFeedback=enabled; options.fps=30;
      options.output=dir.path()+(enabled?"/clicks-on.mp4":"/clicks-off.mp4");
      QImage input(options.region.size(),QImage::Format_RGB32); input.fill(canvasColor);
      QSignalSpy started(&recorder,&Recorder::started),finished(&recorder,&Recorder::finished),failed(&recorder,&Recorder::failed);
      recorder.start(options,[input,calls=std::make_shared<std::atomic_int>(0)] {
        // Simulate a capture/encoder stall spanning the entire wall-clock pulse.
        // Feedback must still occur at the correct position in the video timeline.
        if (++*calls==2) std::this_thread::sleep_for(std::chrono::milliseconds(600));
        return input;
      });
      QVERIFY(started.wait(10000));
      QTest::qWait(70);
      // Only send native input onto this test's own visible canvas.
      const auto window=WindowFromPoint({location.x(),location.y()});
      QVERIFY(window==reinterpret_cast<HWND>(target.winId()));
      // Position and click in the same native input batch: the pointer may have
      // moved since the previous recording finished.
      INPUT events[3]{};
      for (auto &event:events) event.type=INPUT_MOUSE;
      events[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
      events[0].mi.dx=MulDiv(location.x()-GetSystemMetrics(SM_XVIRTUALSCREEN),65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
      events[0].mi.dy=MulDiv(location.y()-GetSystemMetrics(SM_YVIRTUALSCREEN),65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
      events[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN; events[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
      QCOMPARE(SendInput(3,events,sizeof(INPUT)),UINT(3));
      QTest::qWait(650);
      recorder.pause(); QVERIFY(recorder.paused());
      QCOMPARE(SendInput(3,events,sizeof(INPUT)),UINT(3)); // ignored while paused
      QTest::qWait(50); recorder.pause();
      QTest::qWait(100); recorder.stop();
      QVERIFY2(finished.wait(15000),failed.isEmpty()?"Recording failed":qPrintable(failed.first()[0].toString()));
      const auto result=finished.first()[0].toJsonObject();
      QCOMPARE(result["click_feedback_events"].toInteger(),qint64(enabled?1:0));
      if (enabled && !artifacts.isEmpty()) {
        QFile::remove(artifacts+"/click-feedback.mp4");
        QVERIFY(QFile::copy(options.output,artifacts+"/click-feedback.mp4"));
      }
      const auto frames=decodeFrames(options.output,result["width"].toInt(),result["height"].toInt());
      QVERIFY(frames.size()>=10);
      int animationFrames=0;
      for (const auto &frame:frames) {
        const auto pixel=frame.pixelColor(frame.width()/2-1,frame.height()/2-1);
        if (qAbs(pixel.blue()-canvasColor.blue())>20 || qAbs(pixel.red()-canvasColor.red())>20) {
          ++animationFrames;
          if (enabled && animationFrames==1 && !artifacts.isEmpty()) QVERIFY(frame.save(artifacts+"/click-feedback-decoded.png"));
        }
      }
      if (enabled) { QVERIFY(animationFrames>0); QVERIFY(animationFrames<frames.size()-2); }
      else QCOMPARE(animationFrames,0);
    }
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
