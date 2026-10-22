#include "scroll_capture.h"
#include "scroll_grabber.h"
#include "native_windows.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QScopeGuard>
#include <QtGui/QPainter>
#include <QtGui/QFontDatabase>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <dwmapi.h>

class ScrollV2Tests : public QObject {
  Q_OBJECT
  QString artifacts;
  static QImage document(QString kind,int width=720,int height=6500) {
    QImage image(width,height,QImage::Format_RGB32);
    const bool code=kind=="code";
    image.fill(code?QColor("#242734"):QColor("#fafafa"));
    QPainter p(&image); p.setFont(QFont(code?"Consolas":"Microsoft YaHei UI",11));
    const QStringList text={
      "The screenshot records the visible document at its original size.",
      "连续内容应完整保留，回到已截位置不能重复，也不能覆盖。",
      "A useful design makes failure visible and keeps confirmed content.",
      "This paragraph discusses the practical limits of screen acquisition.",
      "验证不仅检查接缝，还需要核对导出结果与原始文档。",
      "When the page changes its layout, old coordinates need verification."};
    const QStringList codeLines={"class CaptureSession {", "public:", "    void append(const Frame &frame) {",
      "        if (!frame.valid()) return;", "        history.push_back(frame);", "    }", "};", "",
      "auto result = matcher.locate(current);", "if (result.confirmed()) canvas.extend(result);"};
    for(int row=0,y=25;y<height;++row,y+=25) {
      if(kind=="whitespace" && ((y>1000 && y<1130)||(y>2800 && y<2930))) continue;
      if(!code && row%9==8) continue;
      if(code) {
        p.setPen(QColor("#7d879a")); p.drawText(12,y,QString::number(row+1));
        p.setPen(QColor("#d4d8ed"));
        p.drawText(65,y,row%10==0?QString("// Session state %1, commit %2").arg(row/10).arg(row*31):codeLines[row%codeLines.size()]);
      } else if(kind=="chat") {
        const int x=row%2?120:25;
        p.fillRect(x,y-18,550,22,row%2?QColor("#bdedaa"):QColor("#e8eef7"));
        p.setPen(QColor("#242424"));
        p.drawText(x+6,y,QString("%1: %2").arg(row).arg(text[row%text.size()]));
      } else {
        p.setPen(QColor("#252525"));
        p.drawText(20,y,QString("%1. %2").arg(row+1).arg(text[row%text.size()]));
      }
    }
    return image;
  }
  static bool good(ScrollStitcher::Result r) {
    return r==ScrollStitcher::Started || r==ScrollStitcher::Appended || r==ScrollStitcher::Revisited || r==ScrollStitcher::Unchanged;
  }
private slots:
  void initTestCase() {
    QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/consola.ttf")>=0);
    QVERIFY(QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc")>=0);
    artifacts=qEnvironmentVariable("PICUT_SCROLL_V2_TEST_DIR");
    QVERIFY2(!artifacts.isEmpty(),"Set PICUT_SCROLL_V2_TEST_DIR to an isolated artifact folder");
    QVERIFY(QDir().mkpath(artifacts));
  }
  void staticDocuments_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<int>("step");
    for(auto kind:{"paragraphs","code","whitespace","chat"}) for(int step:{7,97,397,501})
      QTest::newRow(qPrintable(QString("%1-step-%2").arg(kind).arg(step)))<<QString(kind)<<step;
  }
  void staticDocuments() {
    QFETCH(QString,kind); QFETCH(int,step);
    const auto doc=document(kind); ScrollStitcherV2 engine;
    constexpr int viewport=720,end=5200;
    QElapsedTimer timer; timer.start(); int accepted=0;
    for(int at=0;at<=end;at=qMin(end,at+step)) {
      const auto result=engine.accept(doc.copy(0,at,doc.width(),viewport),1);
      QVERIFY2(good(result),qPrintable(QString("%1 at %2 rejected with %3").arg(kind).arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at); ++accepted;
      if(at==end) break;
    }
    QCOMPARE(engine.image(),doc.copy(0,0,doc.width(),end+viewport));
    const auto committed=engine.image();
    for(int at:{2100,450,3600,0,5200}) {
      const auto result=engine.accept(doc.copy(0,at,doc.width(),viewport),-1);
      QVERIFY2(good(result),qPrintable(QString("revisit %1 returned %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at); QCOMPARE(engine.image(),committed);
    }
    qInfo()<<kind<<"step"<<step<<"accepted"<<accepted<<"elapsed_ms"<<timer.elapsed()<<"exact_original_pixels";
    if(step==501) QVERIFY(engine.image().save(artifacts+"/"+kind+"-long.png"));
  }
  void prependAndRelocate() {
    const auto doc=document("paragraphs"); ScrollStitcherV2 engine;
    for(int at:{2200,1900,1500,1000,600,100,0,3200,2400,2800,3200,3700}) {
      const auto result=engine.accept(doc.copy(0,at,720,720));
      if(at==3200 && engine.size().height()==2920) { // First jump has a real gap.
        QVERIFY(!good(result)); QCOMPARE(engine.size().height(),2920); continue;
      }
      QVERIFY2(good(result),qPrintable(QString("position %1 result %2").arg(at).arg(int(result))));
    }
    QCOMPARE(engine.image(),doc.copy(0,0,720,4420));
  }
  void gapAndRollback() {
    const auto doc=document("paragraphs"); ScrollStitcherV2 engine;
    QVERIFY(good(engine.accept(doc.copy(0,0,720,720))));
    QVERIFY(good(engine.accept(doc.copy(0,400,720,720))));
    const auto before=engine.image(); const auto revision=engine.contentRevision();
    for(int at:{2100,2400,1800}) {
      QVERIFY(!good(engine.accept(doc.copy(0,at,720,720),1)));
      QCOMPARE(engine.image(),before); QCOMPARE(engine.contentRevision(),revision);
    }
    for(int at:{500,900,1300,1700,2100,2500}) QVERIFY(good(engine.accept(doc.copy(0,at,720,720),1)));
    QCOMPARE(engine.image(),doc.copy(0,0,720,3220));
  }
  void ambiguityBlankAndMutation() {
    QImage blank(720,720,QImage::Format_RGB32); blank.fill(Qt::white);
    ScrollStitcherV2 empty; QCOMPARE(empty.accept(blank),ScrollStitcher::Started);
    QCOMPARE(empty.accept(blank),ScrollStitcher::Unchanged);
    auto text=document("code");
    QVERIFY(!good(empty.accept(text.copy(0,0,720,720)))); QCOMPARE(empty.image(),blank);
    QImage repeated(720,3000,QImage::Format_RGB32); repeated.fill(Qt::white);
    QPainter p(&repeated); p.setFont(QFont("Consolas",12)); p.setPen(Qt::black);
    for(int y=25;y<3000;y+=40) p.drawText(20,y,"Identical repeated line with no position evidence"); p.end();
    ScrollStitcherV2 ambiguous; ambiguous.accept(repeated.copy(0,0,720,720));
    QVERIFY(!good(ambiguous.accept(repeated.copy(0,137,720,720),1)));
    QCOMPARE(ambiguous.size().height(),720);
    ScrollStitcherV2 stable; stable.accept(text.copy(0,0,720,720));
    auto mutated=text.copy(0,180,720,720); QPainter change(&mutated);
    change.fillRect(0,40,720,90,Qt::red); change.end();
    QVERIFY(!good(stable.accept(mutated))); QCOMPARE(stable.image(),text.copy(0,0,720,720));
    QVERIFY(good(stable.accept(text.copy(0,180,720,720))));
  }
  void fixedHeaderAndLocalRepaint() {
    auto doc=document("paragraphs");
    auto frame=[&](int at) { auto image=doc.copy(0,at,720,720); QPainter p(&image); p.fillRect(0,0,720,55,QColor("#333344")); p.setPen(Qt::white); p.drawText(15,30,"Fixed toolbar"); return image; };
    ScrollStitcherV2 fixed; fixed.accept(frame(0)); QVERIFY(!good(fixed.accept(frame(200))));
    QCOMPARE(fixed.image(),frame(0));
    ScrollStitcherV2 stationary; auto original=doc.copy(0,0,720,720); stationary.accept(original);
    auto blink=original; QPainter p(&blink); p.fillRect(660,280,3,20,Qt::black); p.end();
    QVERIFY(good(stationary.accept(blink))); QCOMPARE(stationary.image(),original);
  }
  void smallRasterChangeAndScrollbar() {
    const auto doc=document("paragraphs"); ScrollStitcherV2 engine;
    const auto first=doc.copy(0,0,720,720); engine.accept(first);
    auto shifted=doc.copy(0,200,720,720);
    for(int y=0;y<shifted.height();++y) for(int x=0;x<shifted.width();++x) {
      const auto c=shifted.pixel(x,y); const int delta=(x+y)%3?1:2;
      shifted.setPixel(x,y,qRgb(qMin(255,qRed(c)+delta),qMin(255,qGreen(c)+delta),qMin(255,qBlue(c)+delta)));
    }
    QCOMPARE(engine.accept(shifted),ScrollStitcher::Appended);
    QCOMPARE(engine.image().copy(0,0,720,720),first);
    QCOMPARE(engine.image().copy(0,720,720,200),shifted.copy(0,520,720,200));
    auto withBar=[&](int at) { auto image=doc.copy(0,at,720,720); QPainter p(&image);
      p.fillRect(708,0,12,720,QColor("#dddddd")); p.fillRect(710,at/8,8,90,QColor("#777777")); return image; };
    ScrollStitcherV2 bar; bar.accept(withBar(0)); const auto frozen=bar.image();
    QVERIFY(!good(bar.accept(withBar(200)))); QCOMPARE(bar.image(),frozen);
    // A large raster/layout change must preserve the trustworthy prefix.
    auto scaled=doc.copy(0,300,720,720).scaled(721,720).copy(0,0,720,720);
    const auto committed=engine.image(); QVERIFY(!good(engine.accept(scaled))); QCOMPARE(engine.image(),committed);
    QVERIFY(good(engine.accept(shifted)));
  }
  void previewAndWorker() {
    auto doc=document("paragraphs"); ScrollFramePump pump({0,0,720,720}); ScrollStitcherV2 engine;
    ScrollMatchPump matcher(&pump,&engine); matcher.start();
    quint64 serial=0;
    for(int at:{0,300,600,900,1200,300,1500}) {
      pump.enqueue(doc.copy(0,at,720,720));
      std::optional<ScrollMatchPump::Update> update;
      QTRY_VERIFY_WITH_TIMEOUT((update=matcher.takeUpdate(serial)).has_value(),3000);
      serial=update->serial; QVERIFY(good(update->result));
      QCOMPARE(update->preview.viewportY,at);
    }
    pump.stop(); matcher.stop(true);
    QCOMPARE(engine.image(),doc.copy(0,0,720,2220));
    QImage rendered(180,555,QImage::Format_RGB32); rendered.fill(Qt::magenta);
    QPainter p(&rendered); engine.preview().paint(p,rendered.rect()); p.end();
    QVERIFY(rendered.save(artifacts+"/preview.png"));
  }
  void browserRenderedFrames() {
    const auto folder=qEnvironmentVariable("PICUT_SCROLL_V2_BROWSER_DIR");
    if(folder.isEmpty()) QSKIP("Set PICUT_SCROLL_V2_BROWSER_DIR to independently rendered Chromium fixtures");
    QImage reference(folder+"/reference.png"); QVERIFY(!reference.isNull());
    reference=reference.convertToFormat(QImage::Format_RGB32);
    ScrollStitcherV2 engine;
    for(int at:{0,173,487,909,1331,1753,173,1331}) {
      QImage frame(folder+QString("/frame-%1.png").arg(at)); QVERIFY(!frame.isNull());
      frame=frame.convertToFormat(QImage::Format_RGB32);
      QCOMPARE(frame,reference.copy(0,at,720,720));
      QVERIFY2(good(engine.accept(frame)),qPrintable(QString("browser position %1").arg(at)));
      QCOMPARE(engine.viewportPosition(),at);
    }
    QCOMPARE(engine.image(),reference.copy(0,0,720,2473));
    QVERIFY(engine.image().save(artifacts+"/browser-long.png"));
  }
  void desktopDxgi() {
    if(QGuiApplication::platformName()!="windows") QSKIP("Real Windows desktop required");
    wchar_t desktopClass[256]{}; GetClassNameW(WindowFromPoint({300,270}),desktopClass,256);
    if(QString::fromWCharArray(desktopClass).startsWith("LockScreen")) {
      QFile report(artifacts+"/capture-backend.json"); QVERIFY(report.open(QIODevice::WriteOnly));
      report.write(QJsonDocument(QJsonObject{{"status","skipped"},{"reason","Windows desktop is locked"},
        {"observedWindowClass",QString::fromWCharArray(desktopClass)},{"exactPixelComparison",false}}).toJson());
      QSKIP("Windows desktop is locked; DXGI/GDI fixture pixels cannot be verified");
    }
    const auto foreground=GetForegroundWindow();
    const QRect area=physicalRect(QRect(80,100,640,460));
    struct Context { QImage image; int at=0,height=0; } context{document("paragraphs",area.width(),area.height()+3000),0,area.height()};
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"PicutScrollV2Fixture";
    cls.lpfnWndProc=[](HWND window,UINT message,WPARAM w,LPARAM l)->LRESULT {
      if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams));
      auto c=reinterpret_cast<Context *>(GetWindowLongPtrW(window,GWLP_USERDATA));
      if(message==WM_PAINT && c) {
        PAINTSTRUCT paint{}; auto dc=BeginPaint(window,&paint); BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=c->image.width();
        info.bmiHeader.biHeight=-c->height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        SetDIBitsToDevice(dc,0,0,UINT(c->image.width()),UINT(c->height),0,0,0,UINT(c->height),c->image.constScanLine(c->at),&info,DIB_RGB_COLORS);
        EndPaint(window,&paint); return 0;
      }
      return DefWindowProcW(window,message,w,l);
    };
    QVERIFY(RegisterClassW(&cls));
    auto window=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"picut V2 capture validation",WS_POPUP|WS_VISIBLE,
      area.x(),area.y(),area.width(),area.height(),nullptr,nullptr,cls.hInstance,&context);
    QVERIFY(window);
    auto cleanup=qScopeGuard([&] { DestroyWindow(window); UnregisterClassW(cls.lpszClassName,cls.hInstance); if(IsWindow(foreground)) SetForegroundWindow(foreground); });
    SetForegroundWindow(window); UpdateWindow(window); DwmFlush(); QTest::qWait(100);
    ScrollFramePump pump(area); pump.start();
    auto stop=qScopeGuard([&] { pump.stop(); });
    QImage initial;
    QTRY_VERIFY_WITH_TIMEOUT(!(initial=pump.take()).isNull(),5000);
    if(initial!=context.image.copy(0,0,area.width(),area.height())) {
      initial.save(artifacts+"/desktop-observed.png");
      context.image.copy(0,0,area.width(),area.height()).save(artifacts+"/desktop-expected.png");
      wchar_t className[256]{}; GetClassNameW(WindowFromPoint({area.center().x(),area.center().y()}),className,256);
      qInfo()<<"Desktop foreground class"<<QString::fromWCharArray(className)<<"fixture_visible"<<IsWindowVisible(window)
             <<"observed"<<initial.size()<<"expected"<<area.size();
    }
    QCOMPARE(initial,context.image.copy(0,0,area.width(),area.height()));
    ScrollStitcherV2 engine; engine.accept(initial);
    for(int at:{100,260,500,760,920,440,1020,1300}) {
      context.at=at; InvalidateRect(window,nullptr,FALSE); UpdateWindow(window); DwmFlush();
      QImage captured; const auto expected=context.image.copy(0,at,area.width(),area.height());
      QTRY_VERIFY_WITH_TIMEOUT((captured=pump.take())==expected,3000);
      QVERIFY2(good(engine.accept(captured)),qPrintable(QString("real desktop at %1").arg(at)));
    }
    QCOMPARE(engine.image(),context.image.copy(0,0,area.width(),1300+area.height()));
    qInfo()<<"ACTUAL_CAPTURE_BACKEND"<<pump.backendName();
    QFile report(artifacts+"/capture-backend.json"); QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument(QJsonObject{{"backend",pump.backendName()},{"exactPixelComparison",true},{"frames",pump.frameCount()},
      {"samplePollHz",pump.captureRate()},{"changedFrames",pump.changedFrameCount()},{"maximumPollIntervalMs",pump.maximumCaptureIntervalMs()}}).toJson());
    QVERIFY(engine.image().save(artifacts+"/desktop-long.png"));
  }
};

int main(int argc,char **argv) {
  enableDpi(); QApplication app(argc,argv); ScrollV2Tests tests; return QTest::qExec(&tests,argc,argv);
}
#include "scroll_v2_tests.moc"
