#include <future>
#include <thread>
#include <chrono>
#include <mutex>
#include "annotation.h"
#include "application.h"
#include "capture.h"
#include "pin.h"
#include "recognition.h"
#include "recorder.h"
#include "recording_settings.h"
#include "recording_markup.h"
#include "recording_editor.h"
#include "recording_capture.h"
#include "scroll_capture.h"
#include "scroll_grabber.h"
#include "native_windows.h"
#include "hotkey.h"
#include "login_startup.h"
#include <BitMatrix.h>
#include <MultiFormatWriter.h>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QLibrary>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QScopeGuard>
#include <QtCore/QTimer>
#include <QtCore/QVariantAnimation>
#include <QtCore/QUuid>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QFontDatabase>
#include <QtGui/QImageReader>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QInputMethodEvent>
#include <QtGui/QPaintEvent>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QScrollBar>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <winrt/base.h>

// An advertised image can be temporarily unavailable (e.g. delayed rendering).
// Used only with Qt's private offscreen clipboard, never the user's clipboard.
class DelayedClipboardImage : public QMimeData {
public:
  explicit DelayedClipboardImage(const QImage &image, int failures)
      : failures(failures) { setImageData(image); }
  mutable int failures, reads = 0;
protected:
  QVariant retrieveData(const QString &mimeType, QMetaType type) const override {
    if (mimeType == "application/x-qt-image") {
      ++reads;
      if (failures > 0) { --failures; return {}; }
    }
    return QMimeData::retrieveData(mimeType, type);
  }
};

class NativeTests : public QObject {
  Q_OBJECT
  static QImage scrollDocument(int width=460,int height=3600) {
    QImage image(width,height,QImage::Format_RGB32); image.fill(QColor("#f6f6f6"));
    QPainter p(&image); p.setFont(QFont("Consolas",11));
    for (int y=0;y<height;y+=19) {
      p.setPen(QColor::fromHsv((y*17)%360,190,170));
      p.drawText(7,y+14,QString("%1 : item_%2 = function(%3);").arg(y).arg(y*53).arg(y*97));
      p.fillRect(width-27,y,25,17,QColor::fromHsv((y*31)%360,120+(y%120),120+(y%130)));
    }
    return image;
  }
  static QImage codeDocument(int width=1080,int height=4300) {
    QImage doc(width,height,QImage::Format_RGB32); doc.fill(QColor("#282a36"));
    QPainter p(&doc); p.setFont(QFont("Consolas",11));
    const QStringList lines={"", "    }", "", "    get config() {", "        return this._config;", "    }",
      "", "    updateState(value: TaskData) {", "        if (!value) return;",
      "        this._items.forEach(item => {", "            item.update(value);", "        });", "    }"};
    for(int row=0,y=17;y<doc.height();++row,y+=20) {
      p.setPen(QColor("#6272a4")); p.drawText(5,y,QString::number(row+58));
      p.setPen(QColor("#bd93f9"));
      auto line=lines[row%lines.size()];
      if(row%lines.size()==3) line=QString("    get config%1() {").arg(row/lines.size());
      p.drawText(65,y,line);
    }
    return doc;
  }
  static QImage codeFrame(const QImage &doc,int offset,int viewport) {
    auto image=doc.copy(0,offset,doc.width(),viewport);
    QPainter p(&image); const int right=image.width()-12;
    p.fillRect(right,0,12,viewport,QColor("#24252f"));
    p.fillRect(right+2,offset*viewport/doc.height(),8,viewport*viewport/doc.height(),QColor("#666879"));
    return image;
  }
  static QList<QRect> chatStickers() {
    return {{65,85,180,150},{450,355,178,170},{70,760,160,180},
            {440,1130,170,150},{65,1510,180,170},{445,1980,170,180}};
  }
  static QImage chatDocument() {
    QImage image(720,3200,QImage::Format_RGB32); image.fill(QColor("#ededed"));
    QPainter p(&image); p.setFont(QFont("Microsoft YaHei UI",11));
    for(int row=0,y=32;y<image.height();++row,y+=67) {
      const int x=row%2?280:65;
      QRect bubble(x,y,340,43);
      bool sticker=false; for(auto r:chatStickers()) if(r.adjusted(-10,-10,10,10).intersects(bubble)) sticker=true;
      if(sticker) continue;
      p.setPen(Qt::NoPen); p.setBrush(row%2?QColor("#95ec69"):Qt::white); p.drawRoundedRect(bubble,6,6);
      p.setPen(QColor("#242424"));
      p.drawText(bubble.adjusted(12,0,-4,0),Qt::AlignVCenter,QString("消息 %1 · 金额 %2 / Chat %3").arg(row).arg(row*173+27).arg(row*97));
    }
    return image;
  }
  static QImage chatFrame(const QImage &doc,int offset,int phase) {
    auto image=doc.copy(0,offset,720,640);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    for(auto sticker:chatStickers()) {
      sticker.translate(0,-offset);
      if(!sticker.intersects(image.rect())) continue;
      p.save(); p.setClipRect(sticker);
      p.fillRect(sticker,QColor::fromHsv((phase*71+sticker.x())%360,170,230));
      p.setPen(QPen(QColor::fromHsv((phase*53+180)%360,190,140),7));
      for(int n=-6;n<=8;++n)
        p.drawLine(sticker.topLeft()+QPoint(n*25+phase*9%25,0),sticker.bottomLeft()+QPoint(n*25+phase*9%25+65,0));
      p.setPen(Qt::NoPen); p.setBrush(Qt::white);
      const auto center=sticker.center()+QPoint((phase%3-1)*12,(phase%4-2)*8);
      p.drawEllipse(center,29,24); p.setBrush(Qt::black); p.drawEllipse(center+QPoint(8,-4),6,7);
      p.restore();
    }
    return image;
  }
  static QList<QRect> repeatedStickers() {
    return {{62,90,150,150},{458,420,150,150},{62,750,150,150},
            {62,1080,150,150},{458,1410,150,150},{62,1740,150,150},
            {458,2070,150,150},{62,2400,150,150},{62,2730,150,150}};
  }
  static QImage sparseChatDocument(bool unique=true) {
    QImage image(680,3400,QImage::Format_RGB32); image.fill(QColor("#fafafa"));
    QPainter p(&image); p.setFont(QFont("Microsoft YaHei UI",10));
    for(int n=0;n<9;++n) {
      const int y=90+n*330;
      p.fillRect(18,y,32,32,QColor("#5c8fbc"));
      p.setPen(Qt::white); p.drawText(QRect(18,y,32,32),Qt::AlignCenter,"A");
      p.fillRect(434,y+215,178,30,QColor("#95ec69"));
      p.setPen(QColor("#222222"));
      p.drawText(440,y+236,unique?QString("消息 %1 / %2").arg(n).arg(7919*n+31):QString("相同的消息"));
    }
    return image;
  }
  static QImage sparseChatFrame(const QImage &doc,int offset,int phase) {
    auto image=doc.copy(0,offset,680,640); QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for(auto r:repeatedStickers()) {
      r.translate(0,-offset); if(!r.intersects(image.rect())) continue;
      p.save(); p.setClipRect(r); p.translate(r.topLeft());
      p.setPen(Qt::NoPen); p.setBrush(Qt::black);
      p.drawEllipse(QPoint(33,25),21,21); p.drawEllipse(QPoint(115,25),21,21);
      p.setBrush(Qt::white); p.setPen(QPen(Qt::black,3));
      p.drawEllipse(QRect(22,12,106,96)); p.drawEllipse(QRect(39,97,73,75));
      p.setBrush(Qt::black); p.drawEllipse(QRect(39,39,25,26)); p.drawEllipse(QRect(88,39,25,26));
      p.setBrush(Qt::white); p.setPen(Qt::NoPen);
      p.drawEllipse(QPoint(54+phase%3,50+phase%2),5,5); p.drawEllipse(QPoint(98+phase%3,50+phase%2),5,5);
      p.setPen(QPen(Qt::black,3));
      p.drawLine(65,78,88,78+phase%4*2);
      // A moving hand repaints only a small part of an otherwise repeated panda.
      p.setBrush(Qt::black); p.drawEllipse(QPoint(23+phase%4*4,103-phase%3*3),15,15);
      p.restore();
    }
    return image;
  }
  static CaptureOverlay *editingFixture(Store &store, const QString &tool) {
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setPen(QColor("#333333"));
    p.setFont(QFont("Microsoft YaHei UI", 12));
    p.drawText(130, 140, "马克笔直线高亮：只使用起点和终点");
    p.drawText(130, 195, "普通画笔：绘制完成后固定轨迹");
    p.drawText(130, 250, "Ctrl+1～8 选择工具，Ctrl+Z 撤销");
    p.end();
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({100, 80, 480, 300});
    overlay->chooseTool(tool);
    overlay->show();
    return overlay;
  }
  static void dragMove(QWidget *widget, QPoint point,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent move(QEvent::MouseMove, QPointF(point),
                     QPointF(widget->mapToGlobal(point)), Qt::NoButton,
                     Qt::LeftButton, modifiers);
    QApplication::sendEvent(widget, &move);
  }
  static void wheel(QWidget *widget, QPoint point, int steps) {
    QWheelEvent event(QPointF(point), QPointF(widget->mapToGlobal(point)), {},
                      {0, steps * 120}, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(widget, &event);
  }
  static QMenu *namedMenu(const QString &name) {
    for (auto widget : QApplication::topLevelWidgets())
      if (auto menu = qobject_cast<QMenu *>(widget);
          menu && menu->objectName() == name)
        return menu;
    return nullptr;
  }
  static bool savePreview(QWidget *widget, const QString &name) {
    auto folder = qEnvironmentVariable("FLOATSHOT_ANNOTATION_TEST_DIR");
    return folder.isEmpty() ||
           (QDir().mkpath(folder) && widget->grab().save(folder + "/" + name));
  }
  static QJsonObject imageTextFixture() {
    auto quad = [](double left, double top, double right, double bottom) {
      return QJsonArray{QJsonArray{left, top}, QJsonArray{right, top},
                        QJsonArray{right, bottom}, QJsonArray{left, bottom}};
    };
    auto line = [&](QString text, double x, double y, QList<double> widths) {
      QJsonArray chars;
      double left = x;
      for (int n = 0; n < text.size(); ++n) {
        const double right = left + widths.value(n, 20.);
        chars.append(QJsonObject{{"text", text.mid(n, 1)}, {"quad", quad(left,y,right,y+28)}});
        left = right;
      }
      return QJsonObject{{"text", text}, {"quad", quad(x,y,left,y+28)}, {"characters", chars}};
    };
    // Deliberately unsorted rows, columns and unequal character widths.
    return {{"lines", QJsonArray{
        line("2026/10/03",220,130,{18,18,18,18,10,18,18,10,18,18}),
        line("记账日期",30,130,{28,28,28,28}),
        line("金额",30,70,{28,28}),
        line("10.49",220,70,{14,18,8,18,18})}}};
  }
private slots:
  void liveRecordingMarksPublishBeforeReleaseAndRemainEditable() {
    QTemporaryDir dir; Store store(dir.path());
    const QRect region(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(80,80),QSize(400,260));
    auto live=std::make_shared<RecordingLiveState>(physicalRect(region));
    RecordingEditor editor(&store,region,region.size(),nullptr,live);
    auto image=[&] {
      QImage frame(region.size(),QImage::Format_RGB32); frame.fill(Qt::white);
      const auto snapshot=live->snapshot(); if (snapshot.painter) snapshot.painter(frame); return frame;
    };
    auto local=[&](QPoint point) { return editor.contentRect().topLeft()+point; };
    editor.chooseTool("arrow"); editor.setPreference("width",8);
    const auto begin=local({60,150}),end=local({240,150});
    QTest::mousePress(&editor,Qt::LeftButton,Qt::NoModifier,begin);
    QMouseEvent move(QEvent::MouseMove,end,editor.mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&editor,&move);
    QVERIFY(editor.annotation->draft); QVERIFY(editor.annotation->marks.isEmpty());
    QVERIFY(image().pixelColor(140,150).red()>220); QVERIFY(image().pixelColor(140,150).green()<40);
    QTest::mouseRelease(&editor,Qt::LeftButton,Qt::NoModifier,end);
    QCOMPARE(editor.annotation->marks.size(),1);
    editor.chooseTool("rect");
    QTest::mousePress(&editor,Qt::LeftButton,Qt::NoModifier,local({280,140}));
    QTest::mouseRelease(&editor,Qt::LeftButton,Qt::NoModifier,local({350,220}));
    QCOMPARE(editor.annotation->marks.size(),2);
    // Re-select an older confirmed arrow and record every intermediate drag position.
    QTest::mousePress(&editor,Qt::LeftButton,Qt::NoModifier,local({140,150}));
    QCOMPARE(editor.annotation->selected,0);
    const auto moved=local({140,190});
    QMouseEvent drag(QEvent::MouseMove,moved,editor.mapToGlobal(moved),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&editor,&drag);
    QCOMPARE(image().pixelColor(140,150),QColor(Qt::white));
    QVERIFY(image().pixelColor(140,190).green()<40);
    QTest::mouseRelease(&editor,Qt::LeftButton,Qt::NoModifier,moved);
    QTest::keyClick(&editor,Qt::Key_Delete);
    QCOMPARE(editor.annotation->marks.size(),1);
    QCOMPARE(image().pixelColor(140,190),QColor(Qt::white));
    QTest::keyClick(&editor,Qt::Key_Z,Qt::ControlModifier);
    QCOMPARE(editor.annotation->marks.size(),2); QVERIFY(image().pixelColor(140,190).green()<40);
    QVERIFY(editor.complete("clear")); QCOMPARE(editor.annotation->marks.size(),0);
    QCOMPARE(image().pixelColor(140,190),QColor(Qt::white));
  }
  void liveRecordingEditorClosesWithoutRemovingMarks() {
    QTemporaryDir dir; Store store(dir.path());
    QRect region(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(80,80),QSize(400,260));
    auto live=std::make_shared<RecordingLiveState>(physicalRect(region));
    RecordingEditor editor(&store,region,region.size(),nullptr,live);
    editor.chooseTool("rect");
    Mark mark; mark.kind="rect"; mark.points={{40,140},{200,220}}; editor.annotation->add(mark); editor.publish();
    QSignalSpy closed(&editor,&RecordingEditor::editingFinished);
    editor.begin(); QVERIFY(editor.isVisible()); QVERIFY(editor.toolbar->isVisible());
    const auto previewDirectory=qEnvironmentVariable("FLOATSHOT_ANNOTATION_TEST_DIR");
    if (!previewDirectory.isEmpty()) {
      QDir().mkpath(previewDirectory);
      const auto area=editor.contentRect().united(editor.toolbar->geometry()).adjusted(-8,-8,8,8).intersected(editor.rect());
      QImage preview(area.size(),QImage::Format_RGB32); preview.fill(QColor("#f2f5f9"));
      QPainter painter(&preview); painter.drawPixmap(0,0,editor.grab(area)); painter.end();
      QVERIFY(preview.save(previewDirectory+"/live-recording-editor.png"));
    }
    QTest::mouseClick(editor.toolbar->findChild<QToolButton *>("action_cancel"),Qt::LeftButton);
    QVERIFY(!editor.isVisible()); QCOMPARE(closed.size(),1); QCOMPARE(editor.annotation->marks.size(),1);
    QImage frame(region.size(),QImage::Format_RGB32); frame.fill(Qt::white); live->snapshot().painter(frame);
    QVERIFY(frame.pixelColor(40,180).green()<40);
    const auto original=editor.annotation->marks.first().json();
    region.setSize({600,180}); region.translate(25,30); editor.setRegion(region);
    QCOMPARE(editor.contentRect().size(),QSize(600,180)); QCOMPARE(editor.annotation->marks.first().json(),original);
  }
  void recordingWindowModeCanReturnToRegionAndScreenshot() {
    QTemporaryDir dir; Store store(dir.path());
    DesktopSnapshot desktop; desktop.bounds=QGuiApplication::primaryScreen()->availableGeometry();
    QImage image(desktop.bounds.size(),QImage::Format_RGB32); image.fill(Qt::white); desktop.frames={{desktop.bounds,image}};
    CaptureOverlay overlay(&store,true,desktop); overlay.setAttribute(Qt::WA_DeleteOnClose,false);
    overlay.show(); overlay.selectRegion({30,40,420,240});
    auto window=overlay.findChild<QToolButton *>("record_window"); auto area=overlay.findChild<QToolButton *>("record_area");
    auto start=overlay.findChild<QToolButton *>("record_record");
    QTest::mouseClick(window,Qt::LeftButton); QVERIFY(window->isChecked()); QVERIFY(!start->isEnabled());
    QTest::keyClick(&overlay,Qt::Key_Escape); QVERIFY(area->isChecked()); QVERIFY(start->isEnabled()); QVERIFY(overlay.recording);
    QWidget target(nullptr,Qt::Tool|Qt::FramelessWindowHint); target.setGeometry(QRect(desktop.bounds.topLeft()+QPoint(60,70),QSize(380,260))); target.show();
    QVERIFY(overlay.bindRecordingWindow(quintptr(target.winId()))); QCOMPARE(overlay.recordingWindow(),quintptr(target.winId()));
    QCOMPARE(overlay.globalSelection(),recordingWindowInfo(quintptr(target.winId())).logical);
    QTest::mouseClick(area,Qt::LeftButton); QCOMPARE(overlay.recordingWindow(),quintptr(0)); QVERIFY(area->isChecked());
    QTest::mouseClick(overlay.findChild<QToolButton *>("record_back"),Qt::LeftButton); QVERIFY(!overlay.recording);
    overlay.setRecordingMode(true); QVERIFY(area->isChecked()); QVERIFY(!window->isChecked()); QVERIFY(start->isEnabled());
  }
  void scrollDesktopIndependentProducer_data() {
    QTest::addColumn<bool>("code");
    QTest::addColumn<bool>("blockGui");
    QTest::newRow("paragraphs")<<false<<true;
    QTest::newRow("code-editor-scrollbar")<<true<<true;
    QTest::newRow("paragraphs-smooth")<<false<<false;
  }
  void scrollDesktopIndependentProducer() {
    QFETCH(bool,code);
    QFETCH(bool,blockGui);
    if(QGuiApplication::platformName()!="windows") QSKIP("Requires the Windows desktop");
    wchar_t desktopClass[256]{}; GetClassNameW(WindowFromPoint({300,270}),desktopClass,256);
    if(QString::fromWCharArray(desktopClass).startsWith("LockScreen")) QSKIP("Windows desktop is locked");
    const auto oldForeground=GetForegroundWindow();
    const QRect logical(70,90,760,500),area=physicalRect(logical);
    struct Context {
      QImage document;
      std::atomic<HWND> window{nullptr};
      std::atomic<int> offset{0};
      std::mutex eventMutex;
      QHash<int,qint64> eventTimes;
      int height=0;
      bool code=false;
    } context;
    context.code=code;
    context.height=area.height();
    context.document=QImage(area.width(),area.height()+3000,QImage::Format_RGB32);
    context.document.fill(Qt::white);
    QPainter painter(&context.document); painter.setFont(QFont("Microsoft YaHei UI",11));
    painter.setPen(QColor(25,25,25));
    for(int row=0,y=25;y<context.document.height();++row,y+=31) if(row%8!=6 && row%8!=7) {
      painter.drawText(40,y,QString("%1. 实际屏幕像素 / Plain paragraph with white margins").arg(row*113+17));
      painter.drawText(60,y+15,QString("Amount %1.%2; original content, independent render thread.").arg(row*31).arg(row%99));
    }
    painter.end();
    if(code) context.document=codeDocument(area.width(),area.height()+3000);
    std::thread producer([&] {
      auto dpi=reinterpret_cast<DPI_AWARENESS_CONTEXT(WINAPI *)(DPI_AWARENESS_CONTEXT)>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext"));
      if(dpi) dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
      WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"PicutScrollIndependentFixture";
      cls.lpfnWndProc=[](HWND hwnd,UINT message,WPARAM w,LPARAM l)->LRESULT {
        if(message==WM_NCCREATE) SetWindowLongPtrW(hwnd,GWLP_USERDATA,LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams));
        auto c=reinterpret_cast<Context *>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_APP+1) { SetTimer(hwnd,1,20,nullptr); return 0; }
        if(message==WM_TIMER && c) {
          c->offset=qMin(1880,c->offset.load()+47);
          {
            std::lock_guard lock(c->eventMutex);
            c->eventTimes.insert(c->offset.load(),std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
          }
          InvalidateRect(hwnd,nullptr,FALSE); UpdateWindow(hwnd);
          if(c->offset>=1880) KillTimer(hwnd,1);
          return 0;
        }
        if(message==WM_PAINT && c) {
          PAINTSTRUCT paint{}; auto dc=BeginPaint(hwnd,&paint);
          BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
          info.bmiHeader.biWidth=c->document.width(); info.bmiHeader.biHeight=-c->height;
          info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
          SetDIBitsToDevice(dc,0,0,DWORD(c->document.width()),DWORD(c->height),0,0,0,UINT(c->height),
                           c->document.constScanLine(c->offset.load()),&info,DIB_RGB_COLORS);
          if(c->code) {
            auto brush=CreateSolidBrush(RGB(36,37,47));
            RECT rail{c->document.width()-12,0,c->document.width(),c->height}; FillRect(dc,&rail,brush); DeleteObject(brush);
            brush=CreateSolidBrush(RGB(102,104,121));
            const int y=c->offset*c->height/c->document.height();
            RECT thumb{rail.left+2,y,rail.right-2,y+c->height*c->height/c->document.height()};
            FillRect(dc,&thumb,brush); DeleteObject(brush);
          }
          EndPaint(hwnd,&paint); return 0;
        }
        if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd,message,w,l);
      };
      RegisterClassW(&cls);
      auto hwnd=CreateWindowExW(WS_EX_TOPMOST,cls.lpszClassName,L"picut scrolling fixture",WS_POPUP|WS_VISIBLE,
          area.x(),area.y(),area.width(),area.height(),nullptr,nullptr,cls.hInstance,&context);
      if(!hwnd) return;
      UpdateWindow(hwnd); context.window=hwnd;
      MSG message{}; while(GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
      UnregisterClassW(cls.lpszClassName,cls.hInstance);
    });
    auto cleanup=qScopeGuard([&] {
      if(auto hwnd=context.window.load()) PostMessageW(hwnd,WM_CLOSE,0,0);
      producer.join(); if(IsWindow(oldForeground)) SetForegroundWindow(oldForeground);
    });
    QTRY_VERIFY_WITH_TIMEOUT(context.window.load()!=nullptr,2000);
    SetForegroundWindow(context.window.load()); QTest::qWait(150);
    QTemporaryDir dir; Store store(dir.path());
    const auto selected=logical.adjusted(0,0,code?-qCeil(12.*logical.width()/area.width()):0,0);
    const auto capturedArea=physicalRect(selected);
    const double baselineWorkingSet=memoryUsage().value("working_set_mib").toDouble();
    double peakWorkingSet=baselineWorkingSet;
    auto session=std::make_unique<ScrollCapture>(&store,selected,reinterpret_cast<quintptr>(context.window.load()));
    session->setAttribute(Qt::WA_DeleteOnClose,false); session->begin();
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize(),capturedArea.size(),3000);
    struct PaintObserver : QObject {
      std::function<void()> painted;
      bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Paint && painted) painted();
        return false;
      }
    } observer;
    QList<qint64> paintLatencyUs;
    int lastPaintedAt=0;
    observer.painted=[&] {
      peakWorkingSet=qMax(peakWorkingSet,memoryUsage().value("working_set_mib").toDouble());
      const int at=session->imageSize().height()-capturedArea.height();
      if(at<=lastPaintedAt) return;
      qint64 occurred=0;
      {
        std::lock_guard lock(context.eventMutex);
        occurred=context.eventTimes.value(at);
      }
      if(!occurred) return;
      lastPaintedAt=at;
      const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      paintLatencyUs.append((now-occurred)/1000);
    };
    auto previewWidget=session->findChild<QWidget *>("scrollPreview");
    QVERIFY(previewWidget); previewWidget->installEventFilter(&observer);
    auto processCpuNs=[] {
      FILETIME created{},exited{},kernel{},user{};
      if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)) return qint64(0);
      ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
      u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
      return qint64(k.QuadPart+u.QuadPart)*100;
    };
    const auto baselineCpuNs=processCpuNs();
    QElapsedTimer producerDuration; producerDuration.start();
    PostMessageW(context.window.load(),WM_APP+1,0,0);
    // The source continues rendering on its own thread during GUI stalls.
    if(blockGui) for(int n=0;n<9;++n) { QThread::msleep(90); QTest::qWait(45); }
    else QTest::qWait(1200);
    QTRY_COMPARE_WITH_TIMEOUT(context.offset.load(),1880,3000);
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize().height(),area.height()+1880,4000);
    QImage captured;
    session->output=[&](const QImage &image,const QString &) { captured=image; return true; };
    session->finish("copy");
    QTRY_VERIFY_WITH_TIMEOUT(!captured.isNull(),5000);
    QCOMPARE(captured,
             context.document.copy(0,0,capturedArea.width(),area.height()+1880));
    std::sort(paintLatencyUs.begin(),paintLatencyUs.end());
    const auto percentile=[&](int p) {
      return paintLatencyUs.isEmpty()?0.:paintLatencyUs[qMin(paintLatencyUs.size()-1,
          qMax(qsizetype(0),(paintLatencyUs.size()*p+99)/100-1))]/1000.;
    };
    const double wallMs=producerDuration.nsecsElapsed()/1000000.;
    const double cpuMs=(processCpuNs()-baselineCpuNs)/1000000.;
    const QJsonObject report{{"scenario",QString::fromLatin1(QTest::currentDataTag())},
      {"wallMs",wallMs},{"processCpuMs",cpuMs},{"processCpuOverWall",wallMs>0?cpuMs/wallMs:0.},
      {"workingSetIncreaseMiB",qMax(0.,peakWorkingSet-baselineWorkingSet)},
      {"resourceMeasurementIncludesWin32Fixture",true},
      {"producerChanges",40},{"paintedExtendedPositions",paintLatencyUs.size()},
      {"previewPaintStartP95Ms",percentile(95)},{"previewPaintStartP99Ms",percentile(99)},
      {"guiDeliberatelyBlocked90Ms",blockGui},{"exactOutput",true},
      {"measurement","independent Win32 repaint request to actual preview paint event; compositor presentation not measured"}};
    const auto artifactDir=qEnvironmentVariable("PICUT_SCROLL_UI_TEST_DIR");
    if(!artifactDir.isEmpty()) {
      QDir().mkpath(artifactDir);
      QFile reportFile(artifactDir+"/"+QString::fromLatin1(QTest::currentDataTag())+".json");
      QVERIFY(reportFile.open(QIODevice::WriteOnly));
      reportFile.write(QJsonDocument(report).toJson());
    }
    qInfo()<<"SCROLL_UI_LATENCY"<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    QVERIFY(!paintLatencyUs.isEmpty());
  }
  void scrollDesktopManualAndAutomatic() {
    if (QGuiApplication::platformName() != "windows") QSKIP("Requires the Windows desktop");
    wchar_t desktopClass[256]{};
    GetClassNameW(WindowFromPoint(POINT{300,270}),desktopClass,256);
    if (QString::fromWCharArray(desktopClass).startsWith("LockScreen"))
      QSKIP("Windows desktop is locked; real capture and wheel input cannot be verified");
    const auto oldForeground=GetForegroundWindow();
    POINT oldCursor{}; GetCursorPos(&oldCursor);
    auto restoreDesktop=qScopeGuard([&] {
      SetCursorPos(oldCursor.x,oldCursor.y);
      if (IsWindow(oldForeground)) SetForegroundWindow(oldForeground);
    });
    struct Page : QWidget {
      QImage document;
      int offset=0, wheelCount=0;
      Page() {
        document=QImage(460,1000,QImage::Format_RGB32);
        document.fill(QColor("#262834"));
        QPainter p(&document); p.setFont(QFont("Consolas",12));
        for (int y=0;y<document.height();y+=20) {
          p.setPen(QColor::fromHsv((y*19)%360,140,240));
          p.drawText(7,y+16,QString("Row_%1 : value_%2 = %3;").arg(y).arg(y*53).arg(y*97));
        }
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setFocusPolicy(Qt::StrongFocus);
      }
      void paintEvent(QPaintEvent *) override { QPainter p(this); p.drawImage(0,-offset,document); }
      void wheelEvent(QWheelEvent *event) override {
        ++wheelCount; offset=qBound(0,offset-event->angleDelta().y()/120*80,document.height()-height());
        update(); event->accept();
      }
      void keyPressEvent(QKeyEvent *event) override {
        int delta=0;
        if (event->key()==Qt::Key_Down) delta=40;
        if (event->key()==Qt::Key_Up) delta=-40;
        if (event->key()==Qt::Key_PageDown) delta=height()-50;
        if (event->key()==Qt::Key_PageUp) delta=50-height();
        offset=qBound(0,offset+delta,document.height()-height());
        update(); event->accept();
      }
    } page;
    page.setGeometry(70,90,460,360); page.show(); page.activateWindow(); page.setFocus();
    QTest::qWait(200);
    auto hwnd=reinterpret_cast<HWND>(page.winId());
    SetForegroundWindow(hwnd);
    QTemporaryDir dir; Store store(dir.path());
    DesktopSnapshot snapshot;
    for (auto screen:QGuiApplication::screens()) snapshot.bounds=snapshot.bounds.united(screen->geometry());
    QImage background(snapshot.bounds.size(),QImage::Format_RGB32); background.fill(QColor("#dddddd"));
    snapshot.frames={{snapshot.bounds,background}};
    snapshot.foreground=reinterpret_cast<quintptr>(hwnd);
    auto source=std::make_unique<CaptureOverlay>(&store,false,std::move(snapshot));
    source->setAttribute(Qt::WA_DeleteOnClose,false);
    source->selectRegion(page.geometry().translated(-source->pos()));
    source->show(); QTest::qWait(100);
    source->toolbar->findChild<QToolButton *>("action_scroll")->click();
    QVERIFY(source->scrollCapture);
    QVERIFY(!source->isVisible());
    auto session=source->scrollCapture.data();
    auto cleanup=qScopeGuard([&] { if (session) { session->setAttribute(Qt::WA_DeleteOnClose,false); delete session; } });
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize().width(),captureDesktopRegion(page.geometry()).width(),2000);
    const int initialHeight=session->imageSize().height();
    page.offset=123; page.update();
    QTRY_VERIFY_WITH_TIMEOUT(session->imageSize().height()>initialHeight,3000);
    const auto physical=physicalRect(page.geometry());
    QCOMPARE(session->imageSize().height(),qRound(483.*physical.height()/page.height()));
    auto cursorTarget=physicalRect(page.geometry()).center();
    SetCursorPos(cursorTarget.x(),cursorTarget.y());
    for (int n=0;n<4;++n) {
      INPUT input{}; input.type=INPUT_MOUSE; input.mi.dwFlags=MOUSEEVENTF_WHEEL;
      input.mi.mouseData=DWORD(-WHEEL_DELTA);
      QCOMPARE(SendInput(1,&input,sizeof(INPUT)),UINT(1));
      QTest::qWait(30);
    }
    QTRY_COMPARE_WITH_TIMEOUT(page.offset,443,2000);
    // Direction is a short-lived input hint (600 ms), independent of image work.
    QCOMPARE(session->scrollDirection(),1);
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize().height(),qRound(803.*physical.height()/page.height()),2000);
    page.activateWindow(); page.setFocus();
    // Windows may deny SetForegroundWindow to a background test process.
    // A real click on our own unobstructed fixture models the user's focus.
    QCOMPARE(GetAncestor(WindowFromPoint(POINT{cursorTarget.x(),cursorTarget.y()}),GA_ROOT),hwnd);
    INPUT focusClick[2]{};
    focusClick[0].type=focusClick[1].type=INPUT_MOUSE;
    focusClick[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    focusClick[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    QCOMPARE(SendInput(2,focusClick,sizeof(INPUT)),UINT(2));
    QTest::qWait(50);
    SetForegroundWindow(hwnd); SetFocus(hwnd);
    QTest::qWait(80);
    QCOMPARE(GetForegroundWindow(),hwnd);
    QCOMPARE(QApplication::focusWidget(),static_cast<QWidget *>(&page));
    auto key=[&](WORD code) {
      INPUT inputs[2]{};
      inputs[0].type=inputs[1].type=INPUT_KEYBOARD;
      inputs[0].ki.wVk=inputs[1].ki.wVk=code;
      inputs[0].ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
      inputs[1].ki.dwFlags=KEYEVENTF_EXTENDEDKEY|KEYEVENTF_KEYUP;
      return SendInput(2,inputs,sizeof(INPUT));
    };
    QCOMPARE(key(VK_UP),UINT(2));
    QTRY_COMPARE_WITH_TIMEOUT(page.offset,403,2000);
    QCOMPARE(session->scrollDirection(),-1);
    QTest::qWait(60);
    QCOMPARE(key(VK_NEXT),UINT(2));
    QTRY_COMPARE_WITH_TIMEOUT(page.offset,640,2000);
    QCOMPARE(session->scrollDirection(),1);
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize().height(),qRound(1000.*physical.height()/page.height()),2000);
    auto automatic=session->findChild<QToolButton *>("scroll_automatic");
    QVERIFY(automatic && automatic->isHidden());
    QCOMPARE(page.offset,page.document.height()-page.height());
    QCOMPARE(session->imageSize().height(),qRound(1000.*physical.height()/page.height()));
    QVERIFY(savePreview(session,"scroll-desktop-live.png"));
    QImage captured;
    bool completed=false;
    source->output=[&](const QImage &image,const QRect &,const QString &action) {
      completed=action=="copy";
      captured=image;
      return true;
    };
    session->setAttribute(Qt::WA_DeleteOnClose,false);
    session->finish("copy");
    QTRY_VERIFY_WITH_TIMEOUT(completed,5000);
    if (physical.width()==460)
      QCOMPARE(captured,page.document.copy(0,0,460,captured.height()));
  }
  void scrollDesktopAnimatedChat() {
    if(QGuiApplication::platformName()!="windows") QSKIP("Requires the Windows desktop");
    wchar_t desktopClass[256]{}; GetClassNameW(WindowFromPoint({300,270}),desktopClass,256);
    if(QString::fromWCharArray(desktopClass).startsWith("LockScreen")) QSKIP("Windows desktop is locked");
    const auto oldForeground=GetForegroundWindow();
    POINT oldCursor{}; GetCursorPos(&oldCursor);
    auto restore=qScopeGuard([&] {
      SetCursorPos(oldCursor.x,oldCursor.y);
      if(IsWindow(oldForeground)) SetForegroundWindow(oldForeground);
    });
    struct ChatPage : QWidget {
      QImage frame;
      int offset=0,phase=0,wheels=0;
      void paintEvent(QPaintEvent *) override { QPainter p(this); p.drawImage(0,0,frame); }
      void wheelEvent(QWheelEvent *event) override {
        ++wheels; offset=qBound(0,offset-event->angleDelta().y()/120*137,1926);
        event->accept();
      }
    } page;
    const auto doc=chatDocument();
    auto render=[&] { page.frame=chatFrame(doc,page.offset,page.phase++); page.update(); };
    page.setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
    page.setGeometry(70,60,720,640); render(); page.show(); page.activateWindow();
    QTest::qWait(150);
    const auto hwnd=reinterpret_cast<HWND>(page.winId()); SetForegroundWindow(hwnd);
    const auto area=physicalRect(page.geometry());
    QTemporaryDir dir; Store store(dir.path());
    auto session=std::make_unique<ScrollCapture>(&store,page.geometry(),reinterpret_cast<quintptr>(hwnd));
    session->setAttribute(Qt::WA_DeleteOnClose,false); session->begin();
    QTRY_COMPARE_WITH_TIMEOUT(session->imageSize(),area.size(),3000);
    QTimer animation; animation.setInterval(40);
    connect(&animation,&QTimer::timeout,&page,render); animation.start();
    auto status=session->findChild<QLabel *>("scrollStatus"); QVERIFY(status);
    QTest::qWait(500);
    QCOMPARE(session->imageSize(),area.size());
    QVERIFY(status->text().contains("已截内容保留") || status->text().contains("已确认"));
    animation.stop();
    QImage captured; session->output=[&](const QImage &image,const QString &) { captured=image; return true; };
    session->finish("copy");
    QTRY_COMPARE_WITH_TIMEOUT(captured.size(),area.size(),5000);
    qInfo()<<"V2 correctly pauses on large animated patches and keeps initial content";
  }
  void scrollContinuousFramesWithoutSettling() {
    QTemporaryDir dir; Store store(dir.path());
    auto session=std::make_unique<ScrollCapture>(&store,QRect(40,60,460,400),0);
    session->setAttribute(Qt::WA_DeleteOnClose,false); session->begin(false);
    const auto document=scrollDocument();
    int offset=0;
    session->frameProvider=[&] { return document.copy(0,offset,460,400); };
    for (;offset<=1800;offset+=90) {
      session->noteScrollInput(1); session->sample();
      QCOMPARE(session->imageSize(),QSize(460,offset+400));
    }
    offset=1720; session->noteScrollInput(-1); session->sample();
    QCOMPARE(session->imageSize(),QSize(460,2200));
    offset=1920; session->noteScrollInput(1); session->sample();
    QCOMPARE(session->imageSize(),QSize(460,2320));
    bool correct=false;
    session->output=[&](const QImage &image,const QString &action) {
      correct=action=="copy" && image==document.copy(0,0,460,2320); return true;
    };
    session->finish("copy"); QVERIFY(correct);
  }
  void scrollFastJumpsDirectionAndMissingOverlap() {
    const auto document=scrollDocument();
    ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(document.copy(0,0,460,400)),ScrollStitcher::Started);
    for (int y : {350,700,340,675,1025}) {
      const int hint=y==340 ? -1 : 1;
      const auto result=stitcher.accept(document.copy(0,y,460,400),hint);
      QVERIFY(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited);
    }
    QCOMPARE(stitcher.image(),document.copy(0,0,460,1425));
    // A direction is not a distance: a gap must never be invented from input.
    const auto result=stitcher.accept(document.copy(0,2400,460,400),1);
    QVERIFY(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch);
    QCOMPARE(stitcher.image(),document.copy(0,0,460,1425));
    QCOMPARE(stitcher.accept(document.copy(0,1180,460,400),-1),ScrollStitcher::Appended);
    QCOMPARE(stitcher.image(),document.copy(0,0,460,1580)); // Pixels beat a stale direction hint.
    ScrollStitcher shortOverlap;
    QCOMPARE(shortOverlap.accept(document.copy(0,0,460,400)),ScrollStitcher::Started);
    QCOMPARE(shortOverlap.accept(document.copy(0,378,460,400),1),ScrollStitcher::Appended);
    QCOMPARE(shortOverlap.image(),document.copy(0,0,460,778));
  }
  void scrollFastMatcherTiming() {
    const auto document=scrollDocument(1280,7000);
    ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(document.copy(0,0,1280,900)),ScrollStitcher::Started);
    QList<double> times;
    for (int offset=127;offset<=5080;offset+=127) {
      const auto frame=document.copy(0,offset,1280,900);
      QElapsedTimer clock; clock.start();
      QCOMPARE(stitcher.accept(frame,1),ScrollStitcher::Appended);
      times.append(clock.nsecsElapsed()/1000000.);
    }
    QCOMPARE(stitcher.image(),document.copy(0,0,1280,5980));
    std::sort(times.begin(),times.end());
    qInfo() << "Continuous scroll match+cache ms, median:" << times[times.size()/2]
            << "p95:" << times[qMin(times.size()-1,qsizetype(times.size()*.95))];
  }
  void scrollDarkCodePages() {
    auto supplied=qEnvironmentVariable("PICUT_SCROLL_REGRESSION_IMAGE");
    QImage doc;
    if(!supplied.isEmpty()) doc=QImage(supplied).convertToFormat(QImage::Format_RGB32);
    if(doc.isNull()) doc=codeDocument();
    int failures=0;
    for(int mode:{0,1,2}) for(int viewport:{400,500,640,900}) for(int step:{31,47,97,213,viewport-70}) {
      if(viewport>=doc.height()) continue;
      auto frame=[&](int offset) {
        auto image=doc.copy(0,offset,doc.width(),viewport);
        if(mode==1) {
          image=codeFrame(doc,offset,viewport);
        }
        if(mode==2) {
          // Scrolling Chromium/Electron text is rasterized again at different
          // fractional vertical positions before it settles on an integer row.
          image.fill(QColor("#282a36")); QPainter p(&image);
          p.setRenderHint(QPainter::SmoothPixmapTransform);
          p.drawImage(QPointF(0,-offset+(offset%3)*.33),doc);
        }
        return image;
      };
      ScrollStitcher stitcher;
      QCOMPARE(stitcher.accept(frame(0)),ScrollStitcher::Started);
      int lastOffset=0;
      for(int offset=step;offset+viewport<=qMin(2300,doc.height());offset+=step) {
        auto result=stitcher.accept(frame(offset),1);
        // With only 70px left, a crop containing just braces/line numbers can
        // legitimately lack independent anchors. The acquisition queue must
        // preserve intermediate frames to bridge it, rather than guess a gap.
        if(result==ScrollStitcher::NoOverlap && viewport-step<96) {
          QCOMPARE(stitcher.size().height(),lastOffset+viewport);
          const int bridge=offset-step+step/2;
          QCOMPARE(stitcher.accept(frame(bridge),1),ScrollStitcher::Appended);
          result=stitcher.accept(frame(offset),1);
        }
        const int actualOffset=mode==2?offset-qRound((offset%3)*.33):offset;
        if(result!=ScrollStitcher::Appended || stitcher.size().height()!=actualOffset+viewport) {
          qWarning()<<"Static code match failed:"<<"mode"<<mode<<"viewport"<<viewport<<"step"<<step<<"offset"<<offset<<"result"<<result<<"height"<<stitcher.size().height();
          ++failures; break;
        }
        lastOffset=actualOffset;
      }
      if(mode<2) QCOMPARE(stitcher.image().copy(0,0,doc.width()-(mode?12:0),lastOffset+viewport),
                          doc.copy(0,0,doc.width()-(mode?12:0),lastOffset+viewport));
    }
    QCOMPARE(failures,0);
  }
  void scrollCodeScrollbarUpwardAndReverse() {
    const auto doc=codeDocument();
    ScrollStitcher stitcher; int top=1600,bottom=2240,previous=1600;
    QCOMPARE(stitcher.accept(codeFrame(doc,1600,640)),ScrollStitcher::Started);
    for(int y:{1569,1538,1441,1228,1015,802,589,376,163,0,213,426,639,852,1065,1278,1491,1704,1917,2130,2343}) {
      const auto result=stitcher.accept(codeFrame(doc,y,640),y<previous?-1:1);
      QVERIFY2(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited,qPrintable(QString("offset %1 result %2").arg(y).arg(result)));
      top=qMin(top,y); bottom=qMax(bottom,y+640); previous=y;
      QCOMPARE(stitcher.size().height(),bottom-top);
      QCOMPARE(stitcher.image().copy(0,0,doc.width()-12,bottom-top),doc.copy(0,top,doc.width()-12,bottom-top));
    }
  }
  void scrollMatcherRunsWithoutGuiAndPublishesSafePreview() {
    const auto doc=codeDocument(1080,5000);
    ScrollStitcher stitcher; QCOMPARE(stitcher.accept(codeFrame(doc,0,640)),ScrollStitcher::Started);
    std::atomic<int> offset{0};
    // Verify GUI independence and snapshot lifetime with a 30 fps source.
    // An unpaced synthetic producer can overflow the legacy engine's queue on
    // a busy CPU, which tests its load limit instead of the behavior here.
    auto nextFrame = std::chrono::steady_clock::now();
    ScrollFramePump source({},[&] {
      std::this_thread::sleep_until(nextFrame);
      nextFrame = std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
      const int y=qMin(2160,offset.fetch_add(40)); return codeFrame(doc,y,640);
    });
    source.noteDirection(1);
    ScrollMatchPump matcher(&source,&stitcher); source.start(); matcher.start();
    auto cleanup=qScopeGuard([&] { source.stop(); matcher.stop(); });
    // Neither acquisition nor stitching may depend on GUI event delivery.
    QThread::msleep(300);
    auto early=matcher.takeUpdate(0); QVERIFY(early); QVERIFY(early->preview.size.height()>640);
    const auto frozen=early->preview;
    int ticks=0; QTimer heartbeat; heartbeat.setInterval(8);
    connect(&heartbeat,&QTimer::timeout,this,[&] { ++ticks; }); heartbeat.start();
    QTRY_VERIFY_WITH_TIMEOUT(offset.load()>=2200,5000);
    QTRY_VERIFY_WITH_TIMEOUT(matcher.takeUpdate(0)->preview.size.height()==2800,5000);
    QVERIFY(ticks>=10);
    source.stop(); matcher.stop(true); heartbeat.stop();
    QCOMPARE(stitcher.image().copy(0,0,1068,2800),doc.copy(0,0,1068,2800));
    // Snapshots outlive the worker's cache/thumbnail updates and are safe to paint.
    QImage preview(200,500,QImage::Format_RGB32); preview.fill(Qt::magenta);
    QPainter p(&preview); frozen.paint(p,preview.rect()); p.end();
    QVERIFY(preview.pixelColor(100,100)!=QColor(Qt::magenta));
    QVERIFY(source.captureRate()>20.);
  }
  void scrollRealisticPages_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("plain-paragraphs-blank-edges") << 0;
    QTest::newRow("text-with-local-animation") << 1;
    QTest::newRow("antialias-repaint") << 2;
    QTest::newRow("repeated-table-unique-values") << 3;
    QTest::newRow("smooth-scroll-fractional-repaint") << 4;
  }
  void scrollRealisticPages() {
    QFETCH(int,kind);
    constexpr int w=960,h=640;
    QImage doc(w,7200,QImage::Format_RGB32); doc.fill(Qt::white);
    QPainter p(&doc); p.setFont(QFont("Microsoft YaHei UI",11)); p.setPen(QColor(35,35,35));
    const QStringList sentences{
      "滚动截图需要根据实际画面中的共同内容确定接缝，保留原始像素。",
      "A plain paragraph has white space, punctuation, and different line lengths.",
      "这里是网页正文。图片旁边的动画和鼠标经过产生的变化不应该干扰拼接。",
      "Keep the overlap while moving through a document with many paragraphs."};
    for(int row=0,y=28;y<doc.height()-30;++row,y+=32) {
      if(kind==3) {
        p.setPen(QColor(218,218,218)); p.drawLine(30,y+7,900,y+7);
        p.setPen(QColor(35,35,35));
        p.drawText(40,y,"待处理项目 / Pending");
        p.drawText(560,y,QString("%1.%2").arg(row*71+19).arg((row*13)%100,2,10,QChar('0')));
      } else if(row%7!=5 && row%7!=6) {
        p.drawText(74,y,sentences[row%sentences.size()]);
        p.drawText(765,y,QString::number(row*173+31));
      }
    }
    p.end();
    auto frame=[&](int offset,int serial) {
      auto image=doc.copy(0,offset,w,h);
      if(kind==4) {
        image.fill(Qt::white); QPainter smooth(&image);
        smooth.setRenderHint(QPainter::SmoothPixmapTransform);
        smooth.drawImage(QPointF(0,-offset+(serial%3)*.25),doc); smooth.end();
      }
      if(kind==1) { QPainter a(&image); a.fillRect(610,170,100,48,QColor::fromHsv(serial*43%360,180,210)); }
      if(kind==2) for(int y=0;y<h;++y) {
        auto line=reinterpret_cast<QRgb *>(image.scanLine(y));
        for(int x=0;x<w;++x) if(qGray(line[x])<245) {
          const int value=qBound(0,qGray(line[x])+(serial%3-1)*3,255); line[x]=qRgb(value,value,value);
        }
      }
      return image;
    };
    ScrollStitcher stitcher; QCOMPARE(stitcher.accept(frame(0,0)),ScrollStitcher::Started);
    int serial=0,maximum=0;
    for(int offset : {37,138,269,508,593,901,1208,1507,1816,1743,1867,2221,2528}) {
      const auto result=stitcher.accept(frame(offset,++serial),offset==1743?-1:1);
      QVERIFY2(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited,
               qPrintable(QString("offset %1, result %2, captured %3").arg(offset).arg(result).arg(stitcher.size().height())));
      maximum=qMax(maximum,offset); QCOMPARE(stitcher.size(),QSize(w,maximum+h));
    }
    if(kind==0 || kind==3) QCOMPARE(stitcher.image(),doc.copy(0,0,w,maximum+h));
  }
  void scrollUpwardPrependAndBothDirections() {
    const auto doc=scrollDocument(460,3000);
    ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(doc.copy(0,800,460,300)),ScrollStitcher::Started);
    // 300px viewport, 100px overlap: the 200 new pixels belong above the image.
    QCOMPARE(stitcher.accept(doc.copy(0,600,460,300),-1),ScrollStitcher::Appended);
    QCOMPARE(stitcher.image(),doc.copy(0,600,460,500));
    for(int offset:{430,230,470,690,860,1040,1200,980,730,490,250,50}) {
      const auto result=stitcher.accept(doc.copy(0,offset,460,300));
      QVERIFY(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited);
    }
    QCOMPARE(stitcher.image(),doc.copy(0,50,460,1450));
  }
  void scrollAcquisitionSurvivesBusyGui() {
    const auto doc=scrollDocument(460,3000);
    std::atomic<int> offset{0};
    ScrollFramePump pump({},[&] { int y=qMin(1400,offset.fetch_add(70)); return doc.copy(0,y,460,400); });
    pump.start();
    // No event processing: the matcher/GUI is occupied, but capture must run.
    QThread::msleep(230);
    pump.stop();
    QVERIFY(pump.frameCount()>=6);
    ScrollStitcherV2 stitcher;
    for(;;) {
      const auto sample=pump.waitNext();
      if(sample.image.isNull()) break;
      const auto result=stitcher.accept(sample.image,1);
      QVERIFY(result==ScrollStitcher::Started || result==ScrollStitcher::Appended || result==ScrollStitcher::Unchanged);
    }
    // stop() may discard a provider call still in flight. Only enqueued
    // captures contribute to the expected document, not provider attempts.
    const int lastOffset=qMin(1400,int(pump.frameCount()-1)*70);
    QCOMPARE(stitcher.image(),doc.copy(0,0,460,lastOffset+400));
    qInfo()<<"Frames captured while GUI blocked:"<<pump.frameCount()<<"coalesced:"<<pump.droppedCount();
  }
  void scrollRejectsUnrelatedAndAmbiguousImages() {
    const auto doc=scrollDocument(460,5000);
    ScrollStitcher stitcher;
    auto first=doc.copy(0,0,460,400);
    QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
    for(int y:{1200,1777,2890,4013}) {
      const auto result=stitcher.accept(doc.copy(0,y,460,400),1);
      QVERIFY(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch);
      QCOMPARE(stitcher.image(),first);
    }
    QImage repeated(460,1600,QImage::Format_RGB32); repeated.fill(Qt::white);
    QPainter p(&repeated); p.setPen(Qt::black); p.setFont(QFont("Consolas",12));
    for(int y=0;y<1600;y+=30) p.drawText(10,y+22,"The exact same repeated row / 0000000");
    p.end();
    ScrollStitcher ambiguous; first=repeated.copy(0,0,460,400);
    QCOMPARE(ambiguous.accept(first),ScrollStitcher::Started);
    auto result=ambiguous.accept(repeated.copy(0,97,460,400),1);
    QVERIFY(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch);
    QCOMPARE(ambiguous.image(),first);
    auto animated=first; QPainter animation(&animated); animation.fillRect(240,140,45,35,Qt::red); animation.end();
    // A local change by itself never creates a longer screenshot.
    result=ambiguous.accept(animated,1);
    QVERIFY(result!=ScrollStitcher::Appended);
    QCOMPARE(ambiguous.size(),first.size());
  }
  void scrollIdlePageWithAnimationIsUnchanged() {
    const auto doc=scrollDocument(760,900);
    auto first=doc.copy(0,0,760,500);
    ScrollStitcher stitcher; QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
    QImage frame;
    for(int n=1;n<=12;++n) {
      frame=first; QPainter p(&frame);
      p.fillRect(510,120,60,32,QColor::fromHsv(n*27%360,180,220)); p.end();
      QCOMPARE(stitcher.accept(frame,1),ScrollStitcher::Unchanged);
    }
    QCOMPARE(stitcher.image(),frame);
  }
  void scrollChatMultipleAnimatedStickersIdle() {
    const auto doc=chatDocument(); auto first=chatFrame(doc,0,0);
    ScrollStitcher stitcher; QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
    for(int phase=1;phase<=10;++phase) {
      const auto frame=chatFrame(doc,0,phase);
      QCOMPARE(stitcher.accept(frame),ScrollStitcher::Unchanged);
      QCOMPARE(stitcher.size(),first.size());
      QCOMPARE(stitcher.image(),frame);
    }
  }
  void scrollChatRefreshesCapturedContentOnRevisit() {
    const auto doc=chatDocument(); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(chatFrame(doc,0,0)),ScrollStitcher::Started);
    QCOMPARE(stitcher.accept(chatFrame(doc,0,1)),ScrollStitcher::Unchanged);
    QCOMPARE(stitcher.accept(chatFrame(doc,300,2),1),ScrollStitcher::Appended);
    QCOMPARE(stitcher.accept(chatFrame(doc,600,3),1),ScrollStitcher::Appended);
    const auto before=stitcher.image();
    auto frame=chatFrame(doc,600,4);
    QCOMPARE(stitcher.accept(frame),ScrollStitcher::Unchanged);
    QCOMPARE(stitcher.size(),before.size());
    auto after=stitcher.image();
    const auto third=chatStickers()[2];
    QCOMPARE(after.copy(third),frame.copy(third.translated(0,-600)));
    QCOMPARE(after.copy(0,0,720,600),before.copy(0,0,720,600));
    frame=chatFrame(doc,300,5);
    QCOMPARE(stitcher.accept(frame,-1),ScrollStitcher::Revisited);
    const auto second=chatStickers()[1];
    QCOMPARE(stitcher.image().copy(second),frame.copy(second.translated(0,-300)));
    QCOMPARE(stitcher.size(),before.size());
  }
  void scrollChatRefreshKeepsPartlyVisibleStickerWhole() {
    const auto doc=chatDocument(); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(chatFrame(doc,0,0)),ScrollStitcher::Started);
    auto frame=chatFrame(doc,0,1);
    QCOMPARE(stitcher.accept(frame),ScrollStitcher::Unchanged);
    const auto sticker=chatStickers().first();
    const auto whole=frame.copy(sticker);
    QCOMPARE(stitcher.accept(chatFrame(doc,160,2),1),ScrollStitcher::Appended);
    QCOMPARE(stitcher.image().copy(sticker),whole);
    QCOMPARE(stitcher.accept(chatFrame(doc,160,3)),ScrollStitcher::Unchanged);
    QCOMPARE(stitcher.image().copy(sticker),whole);
    frame=chatFrame(doc,0,4);
    QCOMPARE(stitcher.accept(frame,-1),ScrollStitcher::Revisited);
    QCOMPARE(stitcher.image().copy(sticker),frame.copy(sticker));
  }
  void scrollSmallCanvasRefreshOwnsPreviewPixels() {
    const auto doc=scrollDocument(200,800);
    auto frame=doc.copy(0,0,200,400); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(frame),ScrollStitcher::Started);
    for(int phase=1;phase<=60;++phase) {
      frame=doc.copy(0,0,200,400);
      QPainter p(&frame); p.fillRect(95,145,50,44,QColor::fromHsv(phase*43%360,190,210)); p.end();
      QCOMPARE(stitcher.accept(frame),ScrollStitcher::Unchanged);
      QCOMPARE(stitcher.image(),frame);
      QCOMPARE(stitcher.stripCount(),1);
      QImage preview(200,400,QImage::Format_RGB32); preview.fill(Qt::magenta);
      QPainter painter(&preview); stitcher.paint(painter,preview.rect()); painter.end();
      QCOMPARE(preview,frame); // Includes the no-scale thumbnail after the file mapping was closed.
    }
    const auto revision=stitcher.contentRevision();
    QCOMPARE(stitcher.accept(frame),ScrollStitcher::Unchanged);
    QCOMPARE(stitcher.contentRevision(),revision);
  }
  void scrollSessionRepaintsPreviewWithoutGrowth() {
    QTemporaryDir dir; Store store(dir.path());
    auto session=std::make_unique<ScrollCapture>(&store,QRect(40,40,720,640),0);
    session->setAttribute(Qt::WA_DeleteOnClose,false); session->begin(false);
    const auto doc=chatDocument(); int phase=0;
    session->frameProvider=[&] { return chatFrame(doc,0,phase); };
    session->sample(); phase=1; session->sample();
    QCOMPARE(session->imageSize(),QSize(720,640));
    QTest::qWait(200); session->sample();
    auto status=session->findChild<QLabel *>("scrollStatus"); QVERIFY(status);
    QVERIFY(status->text().contains("已截内容保留") || status->text().contains("已确认"));
    QImage output;
    session->output=[&](const QImage &image,const QString &) { output=image; return true; };
    session->finish("copy"); QCOMPARE(output,chatFrame(doc,0,0));
  }
  void scrollChatAnimatedStickersWhileMoving() {
    const auto doc=chatDocument();
    ScrollStitcher stitcher; auto first=chatFrame(doc,0,0);
    QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
    QList<QPair<int,QImage>> observed{{0,first}};
    int phase=0,maximum=0;
    for(int offset:{73,198,369,521,741,983,1186,1420,1331,1497,1733,1926}) {
      auto frame=chatFrame(doc,offset,++phase); observed.append({offset,frame});
      const auto result=stitcher.accept(frame,offset==1331?-1:1);
      QVERIFY2(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited,
          qPrintable(QString("offset %1; result %2").arg(offset).arg(result)));
      maximum=qMax(maximum,offset); QCOMPARE(stitcher.size(),QSize(720,maximum+640));
      frame=chatFrame(doc,offset,++phase); observed.append({offset,frame});
      const auto idle=stitcher.accept(frame);
      QVERIFY2(idle==ScrollStitcher::Unchanged,qPrintable(QString("idle offset %1 phase %2 result %3").arg(offset).arg(phase).arg(idle)));
    }
    const auto joined=stitcher.image();
    if(auto folder=qEnvironmentVariable("FLOATSHOT_ANNOTATION_TEST_DIR");!folder.isEmpty()) {
      QDir().mkpath(folder); joined.save(folder+"/chat-animated-long.png"); first.save(folder+"/chat-original-first.png");
    }
    // Static bubbles, text and all original pixels outside the animated objects
    // must remain exact. No recognition or synthetic redraw of chat content.
    for(int y=0;y<joined.height();++y) for(int x=0;x<joined.width();++x) {
      bool sticker=false; for(auto r:chatStickers()) if(r.contains(x,y)) sticker=true;
      if(!sticker) QCOMPARE(joined.pixel(x,y),doc.pixel(x,y));
    }
    for(auto r:chatStickers()) if(joined.rect().contains(r)) {
      bool wholeFrame=false;
      for(const auto &[top,frame]:observed) {
        const auto local=r.translated(0,-top);
        if(frame.rect().contains(local) && joined.copy(r)==frame.copy(local)) wholeFrame=true;
      }
      QVERIFY2(wholeFrame,qPrintable(QString("Sticker at y=%1 combines multiple animation frames").arg(r.y())));
    }
    QVERIFY(stitcher.stripCount()>1);
    if(auto folder=qEnvironmentVariable("FLOATSHOT_ANNOTATION_TEST_DIR");!folder.isEmpty()) {
      QDir().mkpath(folder); joined.save(folder+"/chat-animated-long.png");
    }
  }
  void scrollChatRejectsMissingOverlap() {
    const auto doc=chatDocument(); auto first=chatFrame(doc,0,0);
    for(int offset:{700,807,940,1141,1470,1791,2200,2475}) {
      ScrollStitcher stitcher; QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
      QCOMPARE(stitcher.accept(chatFrame(doc,0,1)),ScrollStitcher::Unchanged);
      const auto before=stitcher.image();
      const auto result=stitcher.accept(chatFrame(doc,offset,2),1);
      QVERIFY2(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch,
          qPrintable(QString("chat gap %1 result %2, height %3").arg(offset).arg(result).arg(stitcher.size().height())));
      QCOMPARE(stitcher.image(),before);
    }
  }
  void scrollSparseRepeatedStickerRegression() {
    const auto doc=sparseChatDocument(); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(sparseChatFrame(doc,77,0)),ScrollStitcher::Started);
    stitcher.accept(sparseChatFrame(doc,77,1));
    QCOMPARE(stitcher.accept(sparseChatFrame(doc,150,2),1),ScrollStitcher::Appended);
    QCOMPARE(stitcher.size(),QSize(680,713));
  }
  void scrollThinStripsPreviewHasNoBlankSeams() {
    const auto doc=scrollDocument(680,1400); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(doc.copy(0,0,680,640)),ScrollStitcher::Started);
    int offset=0;
    for(int step=0;step<90;++step) {
      offset+=step%3+1;
      QCOMPARE(stitcher.accept(doc.copy(0,offset,680,640),1),ScrollStitcher::Appended);
    }
    QCOMPARE(stitcher.image(),doc.copy(0,0,680,640+offset));
    for(int targetWidth:{123,280,680}) {
      QImage preview(targetWidth,qRound((640+offset)*double(targetWidth)/680),QImage::Format_RGB32);
      preview.fill(Qt::magenta); QPainter painter(&preview); stitcher.paint(painter,preview.rect()); painter.end();
      // The left blank margin has a constant color. Fractional strip boundaries
      // and one-pixel strips must cover every preview row without gaps.
      for(int y=0;y<preview.height();++y) QCOMPARE(preview.pixel(0,y),doc.pixel(0,0));
    }
  }
  void scrollSparseRepeatedStickersNeverLoseMessages() {
    QStringList failures; int accepted=0;
    for(bool unique:{false,true}) {
      const auto doc=sparseChatDocument(unique);
      for(int start:{0,77,185,330,467,660,851,1100}) for(int distance:{17,73,161,233,330,477,590,670,790}) {
        // Fully periodic messages with tiny/no overlap can be pixel-identical
        // to a different move. Those gap checks use distinguishable messages.
        if(!unique && distance>=512) continue;
        ScrollStitcher stitcher;
        QCOMPARE(stitcher.accept(sparseChatFrame(doc,start,0)),ScrollStitcher::Started);
        stitcher.accept(sparseChatFrame(doc,start,1));
        const auto before=stitcher.image();
        const auto result=stitcher.accept(sparseChatFrame(doc,start+distance,2),1);
        const auto joined=stitcher.image();
        const auto label=QString("unique %1 start %2 distance %3 result %4 height %5")
            .arg(unique).arg(start).arg(distance).arg(result).arg(joined.height());
        if(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch) {
          if(joined!=before) failures.append(label+" rejected frame changed canvas");
          continue;
        }
        ++accepted;
        if(distance>=640 || result!=ScrollStitcher::Appended || joined.height()!=640+distance) {
          failures.append(label+" incorrect displacement"); continue;
        }
        bool equal=true;
        for(int y=0;y<joined.height() && equal;++y) for(int x=0;x<joined.width();++x) {
          bool sticker=false; for(auto r:repeatedStickers()) if(r.contains(x,y+start)) sticker=true;
          if(!sticker && joined.pixel(x,y)!=doc.pixel(x,y+start)) { equal=false; break; }
        }
        if(!equal) failures.append(label+" overwrote intervening messages");
      }
    }
    QVERIFY2(failures.isEmpty(),qPrintable(failures.join('\n')));
    QVERIFY(accepted>25);
  }
  void scrollSparseRepeatedStickersBacktracking() {
    const auto doc=sparseChatDocument(); ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(sparseChatFrame(doc,660,0)),ScrollStitcher::Started);
    stitcher.accept(sparseChatFrame(doc,660,1));
    int top=660,bottom=1300,previous=660,phase=1,accepted=0;
    QList<int> offsets;
    for(int offset=623;offset>0;offset-=37) offsets.append(offset);
    offsets.append(0);
    for(int offset=37;offset<1500;offset+=37) offsets.append(offset);
    for(int offset:offsets) {
      const auto before=stitcher.image();
      const auto result=stitcher.accept(sparseChatFrame(doc,offset,++phase),offset>previous?1:-1);
      previous=offset;
      if(result==ScrollStitcher::NoOverlap || result==ScrollStitcher::BoundaryMismatch) {
        QCOMPARE(stitcher.image(),before); continue;
      }
      QVERIFY2(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited,
          qPrintable(QString("offset %1 result %2").arg(offset).arg(result)));
      ++accepted; top=qMin(top,offset); bottom=qMax(bottom,offset+640);
      QCOMPARE(stitcher.size(),QSize(680,bottom-top));
      stitcher.accept(sparseChatFrame(doc,offset,++phase));
      const auto joined=stitcher.image();
      for(int y=0;y<joined.height();++y) for(int x=0;x<joined.width();++x) {
        bool sticker=false; for(auto r:repeatedStickers()) if(r.contains(x,y+top)) sticker=true;
        if(!sticker) QCOMPARE(joined.pixel(x,y),doc.pixel(x,y+top));
      }
    }
    QVERIFY2(accepted>40,qPrintable(QString("Only %1 / %2 moving frames accepted").arg(accepted).arg(offsets.size())));
  }
  void scrollChatAnimatedStickersUpward() {
    const auto doc=chatDocument();
    ScrollStitcher stitcher;
    auto first=chatFrame(doc,1200,0);
    QCOMPARE(stitcher.accept(first),ScrollStitcher::Started);
    QList<QPair<int,QImage>> observed{{1200,first}};
    int previous=1200,phase=0,top=1200,bottom=1840;
    for(int offset:{1060,850,620,440,210,0,360,690,960,1350,1670}) {
      const auto frame=chatFrame(doc,offset,++phase); observed.append({offset,frame});
      const auto result=stitcher.accept(frame,offset>previous?1:-1);
      QVERIFY2(result==ScrollStitcher::Appended || result==ScrollStitcher::Revisited,
          qPrintable(QString("upward offset %1 result %2").arg(offset).arg(result)));
      top=qMin(top,offset); bottom=qMax(bottom,offset+640); previous=offset;
      QCOMPARE(stitcher.size(),QSize(720,bottom-top));
    }
    const auto joined=stitcher.image();
    for(int y=0;y<joined.height();++y) for(int x=0;x<joined.width();++x) {
      bool sticker=false; for(auto r:chatStickers()) if(r.contains(x,y+top)) sticker=true;
      if(!sticker) QCOMPARE(joined.pixel(x,y),doc.pixel(x,y+top));
    }
    for(auto r:chatStickers()) if(joined.rect().contains(r.translated(0,-top))) {
      bool whole=false;
      for(const auto &[offset,frame]:observed) {
        const auto local=r.translated(0,-offset);
        if(frame.rect().contains(local) && joined.copy(r.translated(0,-top))==frame.copy(local)) whole=true;
      }
      QVERIFY2(whole,qPrintable(QString("upward sticker y=%1 combines frames").arg(r.y())));
    }
  }
  void scrollChatSessionIgnoresIdleAnimation() {
    QTemporaryDir dir; Store store(dir.path());
    auto session=std::make_unique<ScrollCapture>(&store,QRect(40,40,720,640),0);
    session->setAttribute(Qt::WA_DeleteOnClose,false); session->begin(false);
    const auto doc=chatDocument(); int offset=0,phase=0;
    session->frameProvider=[&] { return chatFrame(doc,offset,phase++); };
    for(int n=0;n<8;++n) session->sample();
    QCOMPARE(session->imageSize(),QSize(720,640));
    offset=198; session->noteScrollInput(1); session->sample();
    QCOMPARE(session->imageSize(),QSize(720,640));
    QTest::qWait(200); session->sample();
    auto status=session->findChild<QLabel *>("scrollStatus"); QVERIFY(status);
    QVERIFY(status->text().contains("已截内容保留") || status->text().contains("已确认"));
    offset=0; phase=0; session->sample();
    QVERIFY(status->text().contains("720 × 640"));
    session->finish("cancel");
  }
  void scrollStitchingAndBacktracking() {
    QImage document(420,3600,QImage::Format_RGB32);
    document.fill(QColor("#262834"));
    QPainter p(&document); p.setFont(QFont("Consolas",11));
    for (int y=0; y<document.height(); y+=19) {
      p.setPen(QColor::fromHsv((y*17)%360,140,240));
      p.drawText(7,y+14,QString("%1  value_%2 = function(%3);").arg(y).arg(y*53).arg(y*97));
    }
    p.end();
    ScrollStitcher stitcher;
    QCOMPARE(stitcher.accept(document.copy(0,0,420,380)),ScrollStitcher::Started);
    QCOMPARE(stitcher.accept(document.copy(0,103,420,380)),ScrollStitcher::Appended);
    QCOMPARE(stitcher.size(),QSize(420,483));
    QCOMPARE(stitcher.accept(document.copy(0,103,420,380)),ScrollStitcher::Unchanged);
    QImage blinkingCaret=document.copy(0,103,420,380);
    QPainter caret(&blinkingCaret); caret.fillRect(201,80,2,25,Qt::white); caret.end();
    QCOMPARE(stitcher.accept(blinkingCaret),ScrollStitcher::Unchanged);
    QCOMPARE(stitcher.accept(document.copy(0,48,420,380)),ScrollStitcher::Revisited);
    QCOMPARE(stitcher.accept(document.copy(0,130,420,380)),ScrollStitcher::Appended);
    for (int y=240; y<=2000; y+=110)
      QCOMPARE(stitcher.accept(document.copy(0,y,420,380)),ScrollStitcher::Appended);
    QCOMPARE(stitcher.image(),document.copy(0,0,420,stitcher.size().height()));
    QVERIFY(stitcher.stripCount()>10);
  }
  void scrollRejectsInteriorViewportAndFixedEdges() {
    QImage document(380,1400,QImage::Format_RGB32); document.fill(Qt::white);
    QPainter p(&document); p.setFont(QFont("Consolas",11));
    for (int y=0; y<1400; y+=17) {
      p.setPen(QColor::fromHsv((y*23)%360,210,170));
      p.drawText(3,y+13,QString("Row %1 - item %2 : %3").arg(y).arg(y*91).arg(y*31));
    }
    p.end();
    auto frame=[&](int offset,int padding) {
      QImage image(380,420,QImage::Format_RGB32); image.fill(QColor("#dfdfdf"));
      QPainter painter(&image);
      painter.setPen(Qt::black);
      painter.drawText(5,17,"Stationary application header");
      painter.drawText(5,410,"Stationary application footer");
      painter.drawImage(0,padding,document.copy(0,offset,380,420-padding*2));
      return image;
    };
    // Rejection depends on the boundaries, even when nearly the whole interior scrolls.
    for (int padding : {24,100,150}) {
      ScrollStitcher stitcher;
      auto original=frame(0,padding);
      QCOMPARE(stitcher.accept(original),ScrollStitcher::Started);
      auto result=stitcher.accept(frame(71,padding));
      QVERIFY2(result==ScrollStitcher::BoundaryMismatch || result==ScrollStitcher::NoOverlap,
          qPrintable(QString("padding %1 result %2").arg(padding).arg(result)));
      QCOMPARE(stitcher.image(),original);
      QCOMPARE(stitcher.size(),original.size());
    }
    // The same small viewport works as soon as its actual edges are selected.
    ScrollStitcher correctlySelected;
    QCOMPARE(correctlySelected.accept(document.copy(0,0,380,180)),ScrollStitcher::Started);
    QCOMPARE(correctlySelected.accept(document.copy(0,61,380,180)),ScrollStitcher::Appended);
    QCOMPARE(correctlySelected.image(),document.copy(0,0,380,241));
    // A stationary sidebar is allowed. Only vertical boundaries constrain
    // the scrolling columns; even a narrow scrolling pane can reach both.
    for (int fixedLeft : {80,290}) {
      auto sideFrame=[&](int offset) {
        QImage result(380,420,QImage::Format_RGB32); result.fill(QColor("#dddddd"));
        QPainter painter(&result); painter.setPen(Qt::black);
        for (int y=15;y<420;y+=25) painter.drawText(3,y,QString("Fixed %1").arg(y));
        painter.drawImage(fixedLeft,0,document.copy(0,offset,380-fixedLeft,420));
        return result;
      };
      auto first=sideFrame(0),second=sideFrame(71);
      ScrollStitcher sidebar;
      QCOMPARE(sidebar.accept(first),ScrollStitcher::Started);
      QCOMPARE(sidebar.accept(second),ScrollStitcher::Appended);
      QCOMPARE(sidebar.size(),QSize(380,491));
      QImage expected(380,491,QImage::Format_RGB32);
      QPainter joined(&expected); joined.drawImage(0,0,first);
      joined.drawImage(0,420,second.copy(0,349,380,71)); joined.end();
      QCOMPARE(sidebar.image(),expected);
    }
  }
  void scrollPreviewShrinksAndKeepsOriginalPixels() {
    QCOMPARE(ScrollStitcher::previewSize({800,500},{280,800}),QSize(280,175));
    QCOMPARE(ScrollStitcher::previewSize({800,4000},{280,800}),QSize(160,800));
    QCOMPARE(ScrollStitcher::previewSize({800,10000},{280,800}),QSize(64,800));
    QTemporaryDir dir; Store store(dir.path());
    auto session=new ScrollCapture(&store,{80,90,360,300},0);
    QSignalSpy closed(session,&ScrollCapture::finished);
    session->begin(false);
    QImage frame(360,300,QImage::Format_RGB32); frame.fill(QColor("#1379bd"));
    QCOMPARE(session->acceptFrame(frame),ScrollStitcher::Started);
    auto screen=QGuiApplication::screenAt(session->previewGeometry().center());
    QVERIFY(screen);
    QVERIFY(screen->availableGeometry().contains(session->previewGeometry()));
    bool copied=false;
    session->output=[&](const QImage &image,const QString &action) {
      copied=action=="copy" && image==frame; return true;
    };
    QVERIFY(savePreview(session,"scroll-workspace.png"));
    session->finish("copy");
    QVERIFY(copied); QCOMPARE(closed.count(),1); QVERIFY(closed.first().first().toBool());
  }
  void scrollF3PinsCompleteImage_data() {
    QTest::addColumn<QString>("entry");
    QTest::addColumn<bool>("extend");
    QTest::newRow("global-f3-long") << QString("global") << true;
    QTest::newRow("local-f3-long") << QString("local") << true;
    QTest::newRow("toolbar-long") << QString("button") << true;
    QTest::newRow("global-f3-first-frame") << QString("global") << false;
  }
  void scrollF3PinsCompleteImage() {
    QFETCH(QString, entry);
    QFETCH(bool, extend);
    QTemporaryDir dir;
    const auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([&] {
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    ApplicationController controller(true);
    auto source = editingFixture(controller.store, "rect");
    controller.overlay = source;
    source->output = [&](const QImage &image, const QRect &region, const QString &action) {
      return controller.imageOutput(image, region, action);
    };
    const auto anchor = source->globalSelection().topLeft();
    QVERIFY(source->complete("scroll"));
    auto session = source->scrollCapture.data();
    QVERIFY(session);
    const auto document = scrollDocument(480, 1200);
    QCOMPARE(session->acceptFrame(document.copy(0, 0, 480, 300)), ScrollStitcher::Started);
    if (extend)
      QCOMPARE(session->acceptFrame(document.copy(0, 120, 480, 300), 1), ScrollStitcher::Appended);
    QSignalSpy finished(session, &ScrollCapture::finished);
    if (entry == "global") {
      MSG message{}; message.message = WM_HOTKEY; message.wParam = 2;
      QVERIFY(controller.nativeEventFilter({}, &message, nullptr));
    } else if (entry == "local") {
      QTest::keyClick(session, Qt::Key_F3);
    } else {
      auto button = session->findChild<QToolButton *>("scroll_pin");
      QVERIFY(button); QVERIFY(button->toolTip().contains("F3"));
      QTest::mouseClick(button, Qt::LeftButton);
    }
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().first().toBool());
    QVERIFY(!source->isVisible());
    QVERIFY(!source->scrollCapture);
    QCOMPARE(controller.pins.windows.size(), 1);
    auto pin = controller.pins.windows.first();
    QVERIFY(pin);
    QCOMPARE(pin->image, document.copy(0, 0, 480, extend ? 420 : 300));
    QCOMPARE(pin->contentGeometry().topLeft(), anchor);
    auto screen = QGuiApplication::screenAt(anchor);
    QVERIFY(screen); QVERIFY(screen->availableGeometry().contains(pin->contentGeometry()));
    const auto displayed = pin->contentRect().size();
    QVERIFY(qAbs(displayed.width() * double(pin->image.height()) / pin->image.width() - displayed.height()) <= 1.);
  }
  void scrollToolbarEntryAndCancellation() {
    QTemporaryDir dir; Store store(dir.path());
    auto overlay=editingFixture(store,"rect");
    auto ocr=overlay->toolbar->findChild<QToolButton *>("action_select_text");
    auto scroll=overlay->toolbar->findChild<QToolButton *>("action_scroll");
    QVERIFY(scroll && !scroll->isHidden());
    QVERIFY(scroll->x()>ocr->x());
    QCOMPARE(scroll->toolTip(),QString("滚动截屏"));
    QTest::mouseClick(scroll,Qt::LeftButton);
    QVERIFY(overlay->scrollCapture);
    QVERIFY(!overlay->isVisible());
    auto session=overlay->scrollCapture.data();
    QSignalSpy finished(session,&ScrollCapture::finished);
    session->finish("cancel");
    QCOMPARE(finished.count(),1); QVERIFY(!finished.first().first().toBool());
    QVERIFY(overlay->isVisible()); QVERIFY(!overlay->scrollCapture);
    overlay->close();
    Toolbar pinToolbar;
    QVERIFY(pinToolbar.findChild<QToolButton *>("action_scroll")->isHidden());
  }
  void loginStartupDefaultsUpgradeAndPersistedOptOut() {
    QTemporaryDir dir;
    Store fresh(dir.path()); QVERIFY(fresh.config["startup"].toBool());
    Store::writeJson(dir.path()+"/settings.json",QJsonObject{{"startup",false}});
    Store upgraded(dir.path()); QVERIFY(upgraded.config["startup"].toBool());
    QVERIFY(upgraded.config["startup_setting_v1"].toBool());
    upgraded.config["startup"]=false; QVERIFY(upgraded.saveConfig());
    Store disabled(dir.path()); QVERIFY(!disabled.config["startup"].toBool());
    disabled.config["startup"]=true; QVERIFY(disabled.saveConfig());
    Store enabled(dir.path()); QVERIFY(enabled.config["startup"].toBool());
  }
  void loginStartupRegistrationUsesQuotedPathAndTray() {
    const auto subkey="Software\\picut\\Tests\\LoginStartup-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto key="HKEY_CURRENT_USER\\"+subkey;
    const auto cleanup=qScopeGuard([&]{RegDeleteTreeW(HKEY_CURRENT_USER,reinterpret_cast<LPCWSTR>(subkey.utf16()));});
    QSettings systemStartup("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);
    const auto original=systemStartup.value("picut");
    const QString executable="C:/用户/截图 工具/picut.exe";
    QString error;
    QVERIFY2(setLoginStartupEnabled(true,executable,&error,key),qPrintable(error));
    HKEY nativeKey=nullptr;
    QCOMPARE(RegOpenKeyExW(HKEY_CURRENT_USER,reinterpret_cast<LPCWSTR>(subkey.utf16()),0,KEY_READ,&nativeKey),LONG(ERROR_SUCCESS));
    const auto close=qScopeGuard([&]{RegCloseKey(nativeKey);});
    wchar_t command[1024]{}; DWORD type=0,bytes=sizeof(command);
    QCOMPARE(RegQueryValueExW(nativeKey,L"picut",nullptr,&type,reinterpret_cast<LPBYTE>(command),&bytes),LONG(ERROR_SUCCESS));
    QCOMPARE(type,DWORD(REG_SZ));
    QCOMPARE(QString::fromWCharArray(command),QString("\"C:\\用户\\截图 工具\\picut.exe\" --tray"));
    const QString updated="D:/Another path/picut.exe";
    QVERIFY(setLoginStartupEnabled(true,updated,&error,key));
    { QSettings readBack(key,QSettings::NativeFormat); QCOMPARE(readBack.value("picut").toString(),loginStartupCommand(updated)); }
    QVERIFY(setLoginStartupEnabled(false,updated,&error,key));
    { QSettings readBack(key,QSettings::NativeFormat); QVERIFY(!readBack.contains("picut")); }
    QVERIFY(setLoginStartupEnabled(false,updated,&error,key));
    systemStartup.sync(); QCOMPARE(systemStartup.value("picut"),original);
  }
  void loginStartupSettingsSaveAndCancel() {
    QTemporaryDir dir; const auto previous=qgetenv("FLOATSHOT_DATA_DIR");
    const auto restore=qScopeGuard([&]{if(previous.isNull())qunsetenv("FLOATSHOT_DATA_DIR");else qputenv("FLOATSHOT_DATA_DIR",previous);});
    qputenv("FLOATSHOT_DATA_DIR",dir.path().toUtf8()); ApplicationController controller(true);
    auto change=[&](bool expected,bool next,bool save) {
      bool seen=false;
      QTimer::singleShot(0,[&]{
        auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog)return;
        const auto reject=qScopeGuard([&]{if(dialog->isVisible())dialog->reject();});
        auto check=dialog->findChild<QCheckBox *>("setting_startup"); QVERIFY(check);
        seen=true; QCOMPARE(check->isChecked(),expected);
        QVERIFY(savePreview(dialog,"settings-startup.png"));
        check->setChecked(next);
        auto buttons=dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QTest::mouseClick(buttons->button(save?QDialogButtonBox::Save:QDialogButtonBox::Cancel),Qt::LeftButton);
      });
      controller.openSettings(); QVERIFY(seen);
    };
    change(true,false,true);
    Store disabled(dir.path()); QVERIFY(!disabled.config["startup"].toBool());
    change(false,true,false);
    Store canceled(dir.path()); QVERIFY(!canceled.config["startup"].toBool());
    change(false,true,true);
    Store enabled(dir.path()); QVERIFY(enabled.config["startup"].toBool());
  }
  void recordingSettingsPersistenceAndCancel() {
    QTemporaryDir dir; Store store(dir.path());
    QVERIFY(!store.config["click_feedback"].toBool());
    QVERIFY(!savedRecordingOptions(store.config,{30,40,500,300},"movie.mp4").clickFeedback);
    RecordingSettingsDialog dialog(&store,{30,40,500,300},false);
    QVERIFY(!dialog.findChild<QCheckBox *>("recordClickFeedback")->isChecked());
    dialog.findChild<QCheckBox *>("recordClickFeedback")->setChecked(true);
    dialog.findChild<QCheckBox *>("recordSystemAudio")->setChecked(true);
    dialog.findChild<QCheckBox *>("recordMicrophone")->setChecked(true);
    dialog.findChild<QCheckBox *>("recordCursor")->setChecked(false);
    dialog.findChild<QComboBox *>("recordFps")->setCurrentIndex(1);
    dialog.findChild<QComboBox *>("recordQuality")->setCurrentIndex(1);
    dialog.findChild<QSpinBox *>("recordCountdown")->setValue(5);
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    QCOMPARE(dialog.result(),int(QDialog::Accepted));
    Store reopened(dir.path());
    auto options=savedRecordingOptions(reopened.config,{60,80,1000,600},"movie.mp4");
    QVERIFY(options.systemAudio && options.microphone && !options.cursor);
    QVERIFY(options.clickFeedback);
    QCOMPARE(options.fps,15); QCOMPARE(options.quality,18);
    QCOMPARE(options.region,QRect(60,80,1000,600));
    QCOMPARE(reopened.config["countdown"].toInt(),5);
    QVERIFY(recordingSummary(reopened.config).contains("15 fps"));
    QVERIFY(recordingSummary(reopened.config).contains("点击反馈开"));
    auto saved=reopened.config;
    RecordingSettingsDialog canceled(&reopened,{30,40,500,300},false);
    canceled.findChild<QSpinBox *>("recordCountdown")->setValue(1);
    canceled.findChild<QCheckBox *>("recordClickFeedback")->setChecked(false);
    canceled.reject(); QCOMPARE(reopened.config,saved);
  }
  void retiredRecordingShortcutReleasesOldBindings() {
    QTemporaryDir dir; Store defaults(dir.path());
    QVERIFY(!defaults.config["hotkeys"].toObject().contains("record"));
    for (auto old:{"Shift+F1","F2","Ctrl+Alt+R","Alt+F8"}) {
      Store::writeJson(dir.path()+"/settings.json",QJsonObject{{"record_shortcut_v3",true},
        {"hotkeys",QJsonObject{{"capture","Ctrl+F1"},{"pin","Alt+F3"},{"record",old}}}});
      Store upgraded(dir.path()); const auto keys=upgraded.config["hotkeys"].toObject();
      QVERIFY(!keys.contains("record")); QCOMPARE(keys["capture"].toString(),QString("Ctrl+F1"));
      QCOMPARE(keys["pin"].toString(),QString("Alt+F3"));
      upgraded.saveConfig(); Store reopened(dir.path()); QCOMPARE(reopened.config["hotkeys"].toObject(),keys);
    }
  }
  void hotkeyPressCaptureAndNativeMapping() {
    HotkeyEdit edit("F1"); edit.show(); edit.setFocus();
    QTest::keyClick(&edit,Qt::Key_F1,Qt::ShiftModifier);
    QCOMPARE(edit.keySequence(),QKeySequence("Shift+F1"));
    QTest::keyClick(&edit,Qt::Key_F8,Qt::ControlModifier|Qt::AltModifier);
    QCOMPARE(edit.keySequence(),QKeySequence("Ctrl+Alt+F8"));
    UINT mods=0,vk=0;
    QVERIFY(nativeHotkey(edit.keySequence(),mods,vk)); QCOMPARE(vk,UINT(VK_F8));
    QCOMPARE(mods,UINT(MOD_NOREPEAT|MOD_CONTROL|MOD_ALT));
    QVERIFY(nativeHotkey(hotkeySequence("Win+Right"),mods,vk)); QCOMPARE(vk,UINT(VK_RIGHT)); QVERIFY(mods&MOD_WIN);
    QVERIFY(nativeHotkey(hotkeySequence("Ctrl+Delete"),mods,vk)); QCOMPARE(vk,UINT(VK_DELETE));
    QVERIFY(!nativeHotkey(QKeySequence("Ctrl+K, Ctrl+C"),mods,vk));
    edit.clear(); QVERIFY(edit.keySequence().isEmpty());
  }
  void settingsRecordKeysValidateCancelAndLightPanels() {
    QTemporaryDir dir; auto previous=qgetenv("FLOATSHOT_DATA_DIR");
    auto restore=qScopeGuard([previous]{ if(previous.isNull())qunsetenv("FLOATSHOT_DATA_DIR");else qputenv("FLOATSHOT_DATA_DIR",previous); });
    qputenv("FLOATSHOT_DATA_DIR",dir.path().toUtf8());
    ApplicationController controller(true); QVERIFY(!controller.home);
    controller.showHome(); QVERIFY(controller.home->palette().color(QPalette::Window).lightness()>220);
    QVERIFY(savePreview(controller.home,"workbench-light.png"));
    bool accepted=false,duplicateBlocked=false,wasSuspended=false;
    QTimer::singleShot(0,[&]{
      auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog)return;
      auto capture=dialog->findChild<QKeySequenceEdit *>("hotkey_capture");
      QVERIFY(!dialog->findChild<QKeySequenceEdit *>("hotkey_record"));
      auto record=dialog->findChild<QKeySequenceEdit *>("hotkey_stop");
      auto buttons=dialog->findChild<QDialogButtonBox *>();
      if (!capture || !record || !buttons) { dialog->reject(); return; }
      MSG message{}; message.message=WM_HOTKEY; message.wParam=1;
      controller.nativeEventFilter({},&message,nullptr); wasSuspended=!controller.overlay;
      capture->setKeySequence(QKeySequence("F3")); buttons->button(QDialogButtonBox::Save)->click();
      duplicateBlocked=dialog->isVisible() && dialog->findChild<QLabel *>("hotkeyError")->isVisible();
      capture->setKeySequence(QKeySequence("F1")); record->setFocus();
      QTest::keyClick(record,Qt::Key_F8,Qt::ControlModifier|Qt::ShiftModifier);
      dialog->findChild<QTabWidget *>()->setCurrentIndex(1);
      savePreview(dialog,"settings-hotkeys-light.png");
      dialog->findChild<QTabWidget *>()->setCurrentIndex(0);
      savePreview(dialog,"settings-general-light.png");
      buttons->button(QDialogButtonBox::Save)->click(); accepted=dialog->result()==QDialog::Accepted;
      if(dialog->isVisible())dialog->reject();
    });
    controller.openSettings(); QVERIFY(accepted); QVERIFY(duplicateBlocked); QVERIFY(wasSuspended);
    QCOMPARE(controller.store.config["hotkeys"].toObject()["stop"].toString(),QString("Ctrl+Shift+F8"));
    Store reopened(dir.path()); QCOMPARE(reopened.config["hotkeys"].toObject(),controller.store.config["hotkeys"].toObject());
    const auto before=controller.store.config;
    QTimer::singleShot(0,[&]{auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(dialog){dialog->findChild<QKeySequenceEdit *>("hotkey_stop")->setKeySequence(QKeySequence("Alt+F7"));dialog->reject();}});
    controller.openSettings(); QCOMPARE(controller.store.config,before);
    controller.home->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
  void recordingCompatibilityPlacesControlsOutsideOrHidesForFullscreen() {
    QTemporaryDir dir; Recorder recorder(dir.path());
    const auto screen=QGuiApplication::primaryScreen()->geometry();
    QRect region(screen.topLeft()+QPoint(80,120),QSize(500,300));
    RecordingOverlay overlay(&recorder,region,false,false,nullptr,[](QWidget *){return false;});
    overlay.setCountdown(3); QVERIFY(overlay.show()); QVERIFY(overlay.compatibilityMode()); QVERIFY(!overlay.captureExcluded());
    QVERIFY(overlay.controls()->isVisible()); overlay.setCountdown(-1);
    QVERIFY(overlay.controls()->isVisible()); QVERIFY(!overlay.controls()->geometry().intersects(region));
    QVERIFY(overlay.boundary()->geometry().contains(region));
    auto border=overlay.boundary()->grab().toImage(); auto inside=region.topLeft()-overlay.boundary()->pos();
    QCOMPARE(border.pixelColor(inside+QPoint(1,1)).alpha(),0);
    overlay.show(); QVERIFY(!overlay.captureExcluded()); overlay.hide();
    RecordingOverlay full(&recorder,screen,false,false,nullptr,[](QWidget *){return false;});
    full.setCountdown(3); QVERIFY(full.show()); QVERIFY(full.controls()->isVisible());
    full.setCountdown(-1);
    if(full.controls()->isVisible()) QVERIFY(!full.controls()->geometry().intersects(screen));
    full.hide();
  }
  void recordingSelectionSummaryToolbarAndNoMagnifier() {
    QTemporaryDir dir; Store store(dir.path()); DesktopSnapshot desktop;
    desktop.bounds=QGuiApplication::primaryScreen()->availableGeometry();
    QImage image(desktop.bounds.size(),QImage::Format_RGB32); image.fill(Qt::white);
    desktop.frames={{desktop.bounds,image}};
    auto overlay=new CaptureOverlay(&store,true,desktop);
    auto cleanup=qScopeGuard([&]{delete overlay;}); overlay->setAttribute(Qt::WA_DeleteOnClose,false);
    overlay->show(); overlay->selectRegion({30,40,420,220});
    auto bar=overlay->findChild<QFrame *>("recordToolbar");
    auto summary=overlay->findChild<QLabel *>("recordSummary");
    QVERIFY(bar->isVisible()); QVERIFY(summary->isVisible());
    QVERIFY(summary->y()>overlay->selection.bottom());
    for (auto name:{"record_settings","record_record","record_back"}) {
      auto button=bar->findChild<QToolButton *>(name); QVERIFY(button); QVERIFY(!button->icon().isNull());
      QCOMPARE(button->size(),QSize(32,32));
    }
    auto bottom=overlay->rect().bottom();
    overlay->selectRegion({30,bottom-220,420,220});
    QVERIFY(summary->y()<overlay->selection.bottom());
    QVERIFY(overlay->rect().contains(summary->geometry()));
    QTest::mouseMove(overlay,{10,10}); auto before=overlay->grab().toImage();
    QTest::keyPress(overlay,Qt::Key_Alt); QCOMPARE(overlay->grab().toImage(),before);
    QTest::keyRelease(overlay,Qt::Key_Alt);
    QVERIFY(savePreview(overlay,"recording-selection.png"));
    QSignalSpy selected(overlay,&CaptureOverlay::recordRegion);
    QTest::mouseClick(bar->findChild<QToolButton *>("record_record"),Qt::LeftButton);
    QCOMPARE(selected.size(),1); QVERIFY(!overlay->isVisible());
  }
  void regionStartsCountdownWithoutSettingsAndFullscreenPrompts() {
    QTemporaryDir dir; auto previous=qgetenv("FLOATSHOT_DATA_DIR");
    auto restore=qScopeGuard([&]{if(previous.isNull())qunsetenv("FLOATSHOT_DATA_DIR");else qputenv("FLOATSHOT_DATA_DIR",previous);});
    qputenv("FLOATSHOT_DATA_DIR",dir.path().toUtf8()); ApplicationController controller(true);
    const auto screen=QGuiApplication::primaryScreen()->geometry();
    controller.store.config["countdown"]=3;
    controller.prepareRecord(QRect(screen.topLeft()+QPoint(20,20),QSize(400,250)),false);
    QVERIFY(!QApplication::activeModalWidget()); QVERIFY(controller.recordOverlay);
    QVERIFY(controller.recordOverlay->boundary()->isVisible());
    QCOMPARE(controller.recordOverlay->region(),QRect(screen.topLeft()+QPoint(20,20),QSize(400,250)));
    auto state=controller.recordOverlay->controls()->findChild<QLabel *>("recordingState");
    QCOMPARE(state->text(),QString("准备中"));
    QVERIFY(!controller.recordEditor);
    QLabel *counter=nullptr;
    for (auto widget:QApplication::topLevelWidgets())
      if (widget->objectName()=="recordCountdownWindow") counter=qobject_cast<QLabel *>(widget);
    QVERIFY(counter);
    QVERIFY(counter->isVisible()); QCOMPARE(counter->text(),QString("准备录屏…"));
    QTRY_COMPARE(state->text(),QString("倒计时"));
    QVERIFY(controller.recordEditor); QVERIFY(!controller.recordEditor->toolbar);
    QTest::mouseClick(controller.recordOverlay->controls()->findChild<QToolButton *>("stopRecording"),Qt::LeftButton);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    QVERIFY(!controller.recordOverlay); QVERIFY(!controller.recording());
    bool prompted=false;
    QTimer::singleShot(0,[&]{
      auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (dialog) { prompted=dialog->objectName()=="recordingSettings"; dialog->reject(); }
    });
    controller.prepareRecord(screen,true); QVERIFY(prompted); QVERIFY(!controller.recordOverlay);
  }
  void recordingPreparationCanCancelBeforeEditorAndRestart() {
    QTemporaryDir dir; const auto previous=qgetenv("FLOATSHOT_DATA_DIR");
    auto restore=qScopeGuard([previous] { if(previous.isNull())qunsetenv("FLOATSHOT_DATA_DIR");else qputenv("FLOATSHOT_DATA_DIR",previous); });
    qputenv("FLOATSHOT_DATA_DIR",dir.path().toUtf8()); ApplicationController controller(true);
    controller.store.config["countdown"]=3;
    const QRect region(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(30,40),QSize(420,260));
    for (int attempt=0;attempt<2;++attempt) {
      controller.prepareRecord(region,false);
      QVERIFY(controller.recordOverlay); QVERIFY(controller.recordOverlay->boundary()->isVisible());
      QVERIFY(!controller.recordEditor);
      auto controls=controller.recordOverlay->controls();
      QTest::mouseClick(controls->findChild<QToolButton *>("stopRecording"),Qt::LeftButton);
      QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
      QTest::qWait(20);
      QVERIFY(!controller.recordOverlay); QVERIFY(!controller.recordEditor); QVERIFY(!controller.recording());
    }
  }
  void deferredRecordingEditorRetainsMarksAndBuildsToolsOnDemand() {
    QTemporaryDir dir; Store store(dir.path());
    const QRect region(QGuiApplication::primaryScreen()->geometry().topLeft()+QPoint(80,80),QSize(400,260));
    QImage input(region.size(),QImage::Format_RGB32); input.fill(Qt::white);
    Annotation initial(input,&store); Mark mark; mark.kind="rect"; mark.points={{40,140},{200,220}};
    initial.add(mark);
    auto live=std::make_shared<RecordingLiveState>(physicalRect(region));
    RecordingEditor editor(&store,region,region.size(),&initial,live,true);
    QVERIFY(!editor.toolbar); QCOMPARE(editor.annotation->marks.size(),1);
    QImage frame=input.copy(); live->snapshot().painter(frame);
    QVERIFY(frame.pixelColor(40,180).green()<40);
    editor.begin(); QVERIFY(editor.isVisible()); QVERIFY(editor.toolbar && editor.toolbar->isVisible());
    QVERIFY(editor.toolbar->findChild<QToolButton *>("recordingClearMarks"));
    QVERIFY(!editor.toolbar->findChild<QToolButton *>("action_pin")->isVisible());
    editor.finish(); editor.begin();
    QCOMPARE(editor.toolbar->findChildren<QToolButton *>("recordingClearMarks").size(),1);
    QTest::mouseClick(editor.toolbar->findChild<QToolButton *>("recordingClearMarks"),Qt::LeftButton);
    QVERIFY(editor.annotation->marks.isEmpty()); editor.finish();
  }
  void captureRecordingRoundTripKeepsArtworkAndSelection() {
    QTemporaryDir dir; Store store(dir.path()); auto overlay=editingFixture(store,"pen");
    auto cleanup=qScopeGuard([&]{delete overlay;}); overlay->setAttribute(Qt::WA_DeleteOnClose,false);
    QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,{180,160});
    dragMove(overlay,{230,185}); QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,{260,170});
    QCOMPARE(overlay->annotation->marks.size(),1);
    const auto mark=overlay->annotation->marks.first().json();
    const auto region=overlay->selection;
    auto record=overlay->toolbar->findChild<QToolButton *>("action_record");
    QVERIFY(record->isVisible()); QVERIFY(!record->icon().isNull());
    QTest::mouseClick(record,Qt::LeftButton);
    QVERIFY(overlay->recording); QVERIFY(!overlay->toolbar->isVisible());
    auto bar=overlay->findChild<QFrame *>("recordToolbar"); QVERIFY(bar->isVisible());
    QTest::keyClick(overlay,Qt::Key_1,Qt::ControlModifier);
    QCOMPARE(overlay->annotation->tool,QString("pen"));
    QTest::keyClick(overlay,Qt::Key_Escape);
    QVERIFY(overlay->isVisible()); QVERIFY(!overlay->recording); QVERIFY(overlay->toolbar->isVisible());
    QCOMPARE(overlay->selection,region); QCOMPARE(overlay->annotation->marks.first().json(),mark);
    QTest::mouseClick(record,Qt::LeftButton);
    QTest::mouseClick(bar->findChild<QToolButton *>("record_back"),Qt::LeftButton);
    QVERIFY(!overlay->recording); QCOMPARE(overlay->annotation->marks.first().json(),mark);
    QVERIFY(savePreview(overlay,"capture-with-record-entry.png"));
    QTest::mouseClick(record,Qt::LeftButton);
    QVERIFY(savePreview(overlay,"record-from-capture.png"));
  }
  void recordingCountdownCancelRestoresCaptureArtwork() {
    QTemporaryDir dir; const auto previous=qgetenv("FLOATSHOT_DATA_DIR");
    auto restore=qScopeGuard([previous] { if(previous.isNull())qunsetenv("FLOATSHOT_DATA_DIR");else qputenv("FLOATSHOT_DATA_DIR",previous); });
    qputenv("FLOATSHOT_DATA_DIR",dir.path().toUtf8()); ApplicationController controller(true);
    controller.store.config["countdown"]=3; controller.beginCapture();
    QTRY_VERIFY(controller.overlay); auto source=controller.overlay;
    source->selectRegion(QRect(QPoint(30,40),QSize(400,250)));
    Mark mark; mark.kind="rect"; mark.points={{30,50},{190,150}};
    source->annotation->add(mark); source->showToolbar();
    QTest::mouseClick(source->toolbar->findChild<QToolButton *>("action_record"),Qt::LeftButton);
    QTest::mouseClick(source->findChild<QToolButton *>("record_record"),Qt::LeftButton);
    QVERIFY(controller.recordOverlay); QVERIFY(!source->isVisible());
    QTRY_VERIFY(controller.recordEditor);
    QVERIFY(controller.recordOverlay->artwork()); QVERIFY(!controller.recordEditor->toolbar);
    QTest::mouseClick(controller.recordOverlay->controls()->findChild<QToolButton *>("stopRecording"),Qt::LeftButton);
    QTRY_VERIFY(!controller.recordOverlay);
    QVERIFY(source); QVERIFY(source->isVisible()); QVERIFY(!source->recording);
    QCOMPARE(source->annotation->marks.first().json(),mark.json());
    QCOMPARE(source->selection,QRect(30,40,400,250)); QVERIFY(!controller.recording());
    source->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
  void recordingArtworkUsesLivePixelsAndOriginalMarkOrder() {
    QImage original(480,300,QImage::Format_ARGB32); original.fill(QColor("#d4e5f6"));
    { QPainter p(&original); for(int x=0;x<480;x+=8) p.fillRect(x,0,4,300,QColor("#234567")); }
    Annotation annotation(original);
    Mark mark; mark.kind="arrow"; mark.points={{20,30},{130,70}}; annotation.add(mark);
    mark.kind="marker"; mark.width=12; mark.points={{40,120},{190,120}}; annotation.add(mark);
    mark.kind="text"; mark.text="截图标注 ABC 123"; mark.points={{25,165}}; annotation.add(mark);
    mark.kind="number"; mark.text="说明"; mark.points={{280,55}}; annotation.add(mark);
    mark.kind="mosaic"; mark.areaShape="rect"; mark.points={{100,80},{230,155}}; annotation.add(mark);
    mark.kind="blur"; mark.points={{250,100},{420,165}}; annotation.add(mark);
    mark.kind="eraser"; mark.points={{120,100},{180,140}}; annotation.add(mark);
    mark.kind="ellipse"; mark.points={{140,115},{210,155}}; annotation.add(mark);
    const auto paint=recordingPainter(annotation); QVERIFY(paint);
    for(int phase=0;phase<2;++phase) {
      if(phase) { QPainter p(&original); p.fillRect(0,0,480,300,QColor("#c8e59c"));
        for(int x=0;x<480;x+=8) p.fillRect(x+3,0,4,300,QColor("#813046")); }
      annotation.original=original; const auto expected=annotation.render(false);
      auto future=std::async(std::launch::async,[paint,original] { auto frame=original; paint(frame); return frame; });
      const auto actual=future.get();
      int bad=0,maxDelta=0;
      for(int y=0;y<300;++y) for(int x=0;x<480;++x) {
        const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);
        const int delta=qMax(qAbs(a.red()-b.red()),qMax(qAbs(a.green()-b.green()),qAbs(a.blue()-b.blue())));
        if(delta>3) ++bad; maxDelta=qMax(maxDelta,delta);
      }
      QVERIFY2(!bad,qPrintable(QString("artwork differs in %1 pixels, max channel delta %2").arg(bad).arg(maxDelta)));
      QCOMPARE(actual.pixelColor(470,290),original.pixelColor(470,290));
    }
  }
  void recordingArtworkHandlesFractionalScreenRounding() {
    DesktopSnapshot snapshot; snapshot.bounds={0,0,1707,960};
    QImage screen(2560,1440,QImage::Format_RGB32); screen.fill(Qt::white);
    snapshot.frames={{snapshot.bounds,screen}};
    Annotation annotation(snapshot.crop({17,29,501,501}));
    const QSize frameSize(qRound(501.*2560/1707),qRound(501.*1440/960));
    QCOMPARE(annotation.original.size(),QSize(751,751)); QCOMPARE(frameSize,QSize(751,752));
    Mark mark; mark.kind="rect"; mark.filled=true;
    mark.points={{20,annotation.original.height()-40.},{annotation.original.width()-20.,annotation.original.height()-10.}};
    annotation.add(mark); const auto saved=annotation.marks.first().json();
    const auto paint=recordingPainter(annotation,frameSize); QVERIFY(paint);
    for (auto color:{QColor("#2374a1"),QColor("#71ac43")}) {
      QImage input(frameSize,QImage::Format_RGB32); input.fill(color);
      auto future=std::async(std::launch::async,[paint,input]() mutable {paint(input); return input;});
      const auto actual=future.get();
      QCOMPARE(actual.size(),frameSize); QCOMPARE(actual.pixelColor(375,730),mark.color);
      QCOMPARE(actual.pixelColor(375,400),color); QCOMPARE(actual.pixelColor(375,750),color);
    }
    QCOMPARE(annotation.marks.first().json(),saved);
    QImage resized(frameSize+QSize(1,0),QImage::Format_RGB32);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error,paint(resized));
  }
  void numberedCalloutTabTextEmptyCommitAndExport() {
    QTemporaryDir dir; Store store(dir.path()); auto overlay=editingFixture(store,"number");
    auto cleanup=qScopeGuard([&]{delete overlay;}); overlay->setAttribute(Qt::WA_DeleteOnClose,false);
    auto &a=*overlay->annotation;
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,{180,160});
    QVERIFY(markControls(a.marks.last()).isEmpty());
    QTest::keyClick(overlay,Qt::Key_Tab);
    auto editor=overlay->findChild<InlineTextEditor *>(); QVERIFY(editor && editor->isVisible());
    for (auto button:editor->findChildren<QToolButton *>()) QVERIFY(!button->isVisible());
    auto width=editor->textItem()->boundingRect().width();
    QInputMethodEvent input; input.setCommitString("这是文字说明 ABC123"); QApplication::sendEvent(editor,&input);
    QVERIFY(editor->textItem()->boundingRect().width()>width);
    overlay->setPreference("color",QColor("#ff0004"));
    QTest::mouseClick(editor->viewport(),Qt::LeftButton,Qt::NoModifier,{10,220});
    QCOMPARE(a.marks.size(),1); QCOMPARE(a.marks.first().number,1);
    QCOMPARE(a.marks.first().text,QString("这是文字说明 ABC123")); QCOMPARE(a.selected,-1);
    auto label=numberLabelRect(a.marks.first()); auto rendered=a.render();
    auto line=rendered.pixelColor(qRound(label.center().x()),qRound(label.bottom()-1));
    QVERIFY(line.red()>200 && line.green()<80);
    store.config["magnifier_enabled"]=false;
    QTest::mouseMove(overlay,overlay->toolbar->geometry().center());
    QVERIFY(savePreview(overlay,"number-callout-text.png"));
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,{180,260});
    QTest::keyClick(overlay,Qt::Key_Tab); QVERIFY(editor->isVisible());
    QTest::mouseClick(editor->viewport(),Qt::LeftButton,Qt::NoModifier,{10,220});
    QCOMPARE(a.marks.size(),2); QVERIFY(a.marks.last().text.isEmpty());
    QCOMPARE(markBounds(a.marks.last()).width(),a.marks.last().fontSize*1.3);
    auto value=Mark::fromJson(a.marks.first().json()); QCOMPARE(value.text,a.marks.first().text);
    a.undo(); QCOMPARE(a.marks.size(),1); a.redo(); QCOMPARE(a.marks.size(),2);
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,{330,270}); QTest::keyClick(overlay,Qt::Key_Tab);
    QInputMethodEvent extra; extra.setCommitString("导出说明"); QApplication::sendEvent(editor,&extra);
    bool exported=false;
    overlay->output=[&](const QImage &image,const QRect &,const QString &action) {
      exported=action=="pin" && image==a.render() && a.marks.last().text=="导出说明"; return true;
    };
    QTest::keyClick(editor,Qt::Key_F3); QVERIFY(exported);
  }
  void numberedCalloutOnScaledPin() {
    QTemporaryDir dir; Store store(dir.path()); QImage image(900,600,QImage::Format_RGB32); image.fill(Qt::white);
    PinWindow pin(image,&store,{80,90,900,600},"callout"); pin.setScale(.5); pin.show(); pin.startEditing(); pin.chooseTool("number");
    auto point=pin.contentRect().topLeft()+QPoint(100,80);
    QTest::mouseClick(&pin,Qt::LeftButton,Qt::NoModifier,point); auto center=pin.annotation->marks.last().points.first();
    QTest::keyClick(&pin,Qt::Key_Tab); auto editor=pin.findChild<InlineTextEditor *>(); QVERIFY(editor && editor->isVisible());
    QInputMethodEvent input; input.setCommitString("贴图说明"); QApplication::sendEvent(editor,&input);
    QTest::mouseClick(&pin,Qt::LeftButton,Qt::NoModifier,{2,2});
    QVERIFY(!editor->isVisible()); QCOMPARE(pin.annotation->marks.size(),1);
    QCOMPARE(pin.annotation->marks.last().points.first(),center);
    QCOMPARE(pin.annotation->marks.last().text,QString("贴图说明")); QCOMPARE(pin.annotation->render().size(),image.size());
    QVERIFY(markControls(pin.annotation->marks.last()).isEmpty());
    QVERIFY(savePreview(&pin,"number-callout-pin.png"));
  }
  void imageTextModelReadingOrderAndMixedWidths() {
    ImageTextPage page;
    page.load(imageTextFixture());
    QCOMPARE(page.text, QString("金额\t10.49\n记账日期\t2026/10/03"));
    QCOMPARE(page.caretAt({219,84}), -1);
    QCOMPARE(page.caretAt({221,84}), 3);
    QCOMPARE(page.caretAt({235,84}), 4);
    QCOMPARE(page.caretAt({253,84}), 5);
    QCOMPARE(page.caretAt({261,84}), 6);
    auto selected = page.highlights(5,8);
    QCOMPARE(selected.size(), 3);
    QCOMPARE(selected.first().boundingRect().width(), 8.);
    QCOMPARE(page.highlights(8,5), selected);
    auto oldResult = imageTextFixture();
    auto lines = oldResult["lines"].toArray();
    auto oldLine = lines.first().toObject(); oldLine.remove("characters");
    page.load({{"lines",QJsonArray{oldLine}}});
    QCOMPARE(page.lines.first().glyphs.size(), 1);
    QCOMPARE(page.text, QString("2026/10/03"));
  }
  void translationPinUsesRenderedImageAndOriginalPosition() {
    QTemporaryDir dir; Store store(dir.path()); auto overlay=editingFixture(store,"rect");
    QPointer<CaptureOverlay> alive=overlay; auto cleanup=qScopeGuard([&]{if(alive)delete alive;});
    overlay->startImageTranslation({},false);
    auto translation=overlay->imageTranslation; QVERIFY(translation && translation->isVisible());
    QVERIFY(!overlay->toolbar->isVisible());
    bool called=false;
    overlay->output=[&](const QImage &pixels,const QRect &region,const QString &action) {
      called=true; return action=="pin" && region==overlay->globalSelection() && pixels==translation->translatedImage();
    };
    QVERIFY(!overlay->complete("pin")); QVERIFY(!called);
    translation->setOcrResult(imageTextFixture(),false);
    QJsonArray blocks;
    for(const auto &block:translation->page().blocks) blocks.append(QJsonObject{{"id",block.id},{"text","测试"}});
    translation->setTranslationResult({{"blocks",blocks}});
    QVERIFY(translation->hasCompletedTranslation()); QVERIFY(overlay->complete("pin")); QVERIFY(called);
    QVERIFY(!overlay->isVisible());
  }
  void imageTextSelectionCopyCacheAndExport() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "rect");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    const auto original = overlay->annotation->render();
    auto savedClipboard = std::make_unique<QMimeData>();
    if (auto mime = QApplication::clipboard()->mimeData())
      for (const auto &format : mime->formats())
        savedClipboard->setData(format, mime->data(format));
    auto restoreClipboard = qScopeGuard([&] { QApplication::clipboard()->setMimeData(savedClipboard.release()); });
    overlay->startImageText({}, false);
    auto text = overlay->imageText;
    QVERIFY(text->isVisible());
    QVERIFY(!overlay->toolbar->isVisible());
    text->setResult(imageTextFixture());
    auto start=text->imageToView({253,84}).toPoint(), end=text->imageToView({296,84}).toPoint();
    QTest::mousePress(text,Qt::LeftButton,Qt::NoModifier,start);
    dragMove(text,end);
    QTest::mouseRelease(text,Qt::LeftButton,Qt::NoModifier,end);
    QCOMPARE(text->selectedText(), QString(".49"));
    const auto selectedForTranslation=text->translationSelection()["lines"].toArray();
    QCOMPARE(selectedForTranslation.size(),1);
    QCOMPARE(selectedForTranslation[0].toObject()["text"].toString(),QString(".49"));
    const auto translatedQuad=selectedForTranslation[0].toObject()["quad"].toArray();
    QCOMPARE(translatedQuad[0].toArray()[0].toDouble(),252.);
    auto editor=text->resultEditor();
    QVERIFY(editor->isReadOnly());
    QVERIFY(text->controlBar()->findChildren<QLabel *>().isEmpty());
    QVERIFY(text->controlBar()->findChildren<QToolButton *>().isEmpty());
    QCOMPARE(text->controlBar()->findChildren<QPushButton *>().size(),1);
    QCOMPARE(text->findChild<QPushButton *>("imageTextCancel")->text(),QString("取消"));
    QCOMPARE(editor->textCursor().selectionStart(),5);
    QCOMPARE(editor->textCursor().selectionEnd(),8);
    QVERIFY(!editor->extraSelections().isEmpty());
    QTest::keyClick(text,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QString(".49"));
    QTest::keyClick(text,Qt::Key_A,Qt::ControlModifier);
    QCOMPARE(text->selectedText(), QString("金额\t10.49\n记账日期\t2026/10/03"));
    QCOMPARE(overlay->annotation->render(), original);
    QCOMPARE(text->sourceImage(), original);
    QVERIFY(savePreview(text,"image-text-all-selected.png"));
    QTest::keyClick(text,Qt::Key_Escape);
    QVERIFY(!text->isVisible());
    QVERIFY(overlay->toolbar->isVisible());
    overlay->startImageText({},true);
    QCOMPARE(text->recognitionStarts(),0); // Same image/settings reuse the cached result.
    QVERIFY(!text->page().text.isEmpty());
    auto cursor=editor->textCursor(); cursor.setPosition(14); cursor.setPosition(24,QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    QCOMPARE(text->selectedText(),QString("2026/10/03"));
    QCOMPARE(text->page().highlights(14,24).size(),10);
    editor->setFocus();
    QTest::keyClick(editor,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(),QString("2026/10/03"));
    QTest::keyClicks(editor,"cannot edit");
    QCOMPARE(editor->toPlainText(),text->page().text);
    QTest::keyClick(editor,Qt::Key_A,Qt::ControlModifier);
    QCOMPARE(text->selectedText(),text->page().text);
    QCOMPARE(text->page().text,QString("金额\t10.49\n记账日期\t2026/10/03"));
    QTest::keyClick(editor,Qt::Key_F4);
    Mark mark;
    mark.kind="rect"; mark.points={{10,10},{25,25}};
    overlay->annotation->add(mark);
    overlay->startImageText({},false);
    QVERIFY(text->page().text.isEmpty()); // Pixel changes invalidate the cache.
    text->setResult({{"error","synthetic failure"}});
    QVERIFY(text->page().text.isEmpty());
    QTest::mouseClick(text->findChild<QPushButton *>("imageTextCancel"),Qt::LeftButton);
    QVERIFY(!text->isVisible()); QVERIFY(overlay->toolbar->isVisible());
  }
  void imageTextPinScaleAndToolbarLifecycle() {
    QTemporaryDir dir;
    Store store(dir.path());
    QImage image(480,300,QImage::Format_ARGB32); image.fill(Qt::white);
    auto pin = new PinWindow(image,&store,{100,80,480,300});
    QPointer<PinWindow> alive = pin;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    pin->show();
    pin->startImageText(image,false);
    auto text = pin->imageText;
    text->setResult(imageTextFixture());
    QVERIFY(text->isWindow());
    QVERIFY(text->windowFlags()&Qt::FramelessWindowHint);
    QVERIFY(!text->controlBar()->isWindow());
    pin->move(pin->pos()+QPoint(20,15));
    if (text->placement()!=ImageTextLayout::Split)
      QCOMPARE(text->mapToGlobal(text->imageViewport().topLeft()),pin->mapToGlobal(pin->contentRect().topLeft()));
    pin->setScale(.5);
    auto start = text->imageToView({222,84}).toPoint();
    auto end = text->imageToView({296,84}).toPoint();
    QTest::mousePress(text,Qt::LeftButton,Qt::NoModifier,start);
    QTest::mouseRelease(text,Qt::LeftButton,Qt::NoModifier,end);
    QCOMPARE(text->selectedText(),QString("10.49"));
    QVERIFY(savePreview(text,"image-text-pin-half-scale.png"));
    auto pinGeometry=pin->contentGeometry();
    auto beforeZoom=text->previewRect();
    wheel(text,text->imageViewport().center(),2);
    QVERIFY(text->previewRect().width()>beforeZoom.width());
    QCOMPARE(pin->contentGeometry(),pinGeometry);
    QCOMPARE(pin->displayedImage(),image);
    pin->hide();
    QVERIFY(!text->isVisible());
    QVERIFY(!text->controlBar()->isVisible());
    pin->show(); pin->startEditing();
    pin->startImageText(pin->displayedImage(),false);
    QTest::keyClick(text,Qt::Key_Escape);
    QVERIFY(pin->toolbar->isVisible());
  }
  void imageTextIncludesPendingInlineText_data() {
    QTest::addColumn<bool>("pinWindow");
    QTest::newRow("capture") << false;
    QTest::newRow("pin") << true;
  }
  void imageTextIncludesPendingInlineText() {
    QFETCH(bool,pinWindow);
    QTemporaryDir dir; Store store(dir.path());
    store.config["annotation_last_tool"]="text";
    QImage image(480,300,QImage::Format_ARGB32); image.fill(Qt::white);
    EditingSurface *surface;
    if (pinWindow) {
      auto pin=new PinWindow(image,&store,{100,80,480,300});
      pin->show(); pin->startEditing(); surface=pin;
    } else surface=editingFixture(store,"text");
    auto cleanup=qScopeGuard([&] { delete surface; });
    QTest::mouseClick(surface,Qt::LeftButton,Qt::NoModifier,surface->contentRect().topLeft()+QPoint(60,60));
    auto editor=surface->findChild<InlineTextEditor *>(); QVERIFY(editor);
    QTest::keyClicks(editor,"Pending text 123");
    auto expected=surface->annotation->render(); paintMark(expected,editor->value());
    QTest::keyClick(editor,Qt::Key_F4);
    QVERIFY(!editor->isVisible()); QVERIFY(surface->imageText->isVisible());
    QCOMPARE(surface->imageText->sourceImage(),expected);
    QVERIFY(!surface->toolbar->isVisible());
    QTest::keyClick(surface->imageText,Qt::Key_Escape);
    QVERIFY(surface->toolbar->isVisible());
    QVERIFY(!surface->imageText->findChild<PaddleOcrJob *>()->running());
  }
  void imageTextDestroyDuringRecognition() {
    QTemporaryDir dir; Store store(dir.path());
    QImage image(480,300,QImage::Format_ARGB32); image.fill(Qt::white);
    auto pin=new PinWindow(image,&store,{100,80,480,300}); pin->show();
    pin->startImageText(image);
    auto job=pin->imageText->findChild<PaddleOcrJob *>(); QVERIFY(job->running());
    auto temporary=job->temporaryPath(); QVERIFY(temporary.isEmpty());
    delete pin;
    QVERIFY(!QFileInfo::exists(temporary));
  }
  void imageTextLayoutsPreserveCoordinates() {
    const QRect screen(0,0,1400,900);
    for (const auto &item:QList<QPair<QRect,ImageTextLayout::Placement>>{
        {{100,100,360,250},ImageTextLayout::Right},
        {{1000,200,360,300},ImageTextLayout::Left},
        {screen,ImageTextLayout::Split},
        {{0,0,700,1400},ImageTextLayout::Split}}) {
      auto plan=ImageTextLayout::arrange(item.first,screen);
      QCOMPARE(plan.placement,item.second);
      QVERIFY(screen.contains(plan.window));
      QVERIFY(QRect(QPoint(),plan.window.size()).contains(plan.panel));
      QVERIFY(!plan.panel.intersects(plan.viewport));
      if (item.second!=ImageTextLayout::Split)
        QCOMPARE(plan.viewport.translated(plan.window.topLeft()),item.first);
    }
    const QRect secondary(-1920,-200,1920,1080), image(-500,120,450,300);
    auto plan=ImageTextLayout::arrange(image,secondary);
    QCOMPARE(plan.placement,ImageTextLayout::Left);
    QCOMPARE(plan.viewport.translated(plan.window.topLeft()),image);
    QVERIFY(secondary.contains(plan.window));
    auto collapsed=ImageTextLayout::arrange(secondary,secondary,360,true);
    QVERIFY(collapsed.panel.isEmpty());
    QVERIFY(collapsed.viewport.width()>plan.viewport.width());
  }
  void imageTextFullscreenDividerPanCollapseAndReturn() {
    QTemporaryDir dir; Store store(dir.path());
    QWidget owner; owner.setGeometry(QGuiApplication::primaryScreen()->availableGeometry()); owner.show();
    QImage image(1600,1000,QImage::Format_RGB32); image.fill(Qt::white);
    ImageTextOverlay text(&owner,&store,[&] {return owner.rect();});
    auto originalGeometry=owner.geometry(); text.begin(image,false); text.setResult(imageTextFixture());
    QCOMPARE(text.placement(),ImageTextLayout::Split);
    auto divider=text.findChild<QWidget *>("imageTextDivider");
    auto width=text.controlBar()->width();
    QTest::mousePress(divider,Qt::LeftButton,Qt::NoModifier,{2,100});
    dragMove(divider,{-58,100});
    QTest::mouseRelease(divider,Qt::LeftButton,Qt::NoModifier,{2,100});
    QVERIFY(text.controlBar()->width()>width);
    auto viewport=text.imageViewport();
    wheel(&text,viewport.center(),6);
    auto before=text.previewRect();
    QTest::keyPress(&text,Qt::Key_Space);
    QTest::mousePress(&text,Qt::LeftButton,Qt::NoModifier,viewport.center());
    dragMove(&text,viewport.center()+QPoint(-30,-20));
    QTest::mouseRelease(&text,Qt::LeftButton,Qt::NoModifier,viewport.center()+QPoint(-30,-20));
    QTest::keyRelease(&text,Qt::Key_Space);
    QVERIFY(text.previewRect().topLeft()!=before.topLeft());
    auto cursor=text.resultEditor()->textCursor(); cursor.setPosition(3); cursor.setPosition(8,QTextCursor::KeepAnchor);
    text.resultEditor()->setTextCursor(cursor);
    auto target=text.imageToView({290,84});
    QVERIFY(QRectF(text.imageViewport()).contains(target));
    text.setPanelCollapsed(true); QVERIFY(!text.controlBar()->isVisible());
    QVERIFY(text.imageViewport().width()>viewport.width());
    text.setPanelCollapsed(false);
    QVERIFY(text.controlBar()->isVisible());
    QCOMPARE(text.selectedText(),QString("10.49"));
    QCOMPARE(text.sourceImage(),image); QCOMPARE(owner.geometry(),originalGeometry);
    text.finish(); QCOMPARE(owner.geometry(),originalGeometry);
    QVERIFY(!text.isVisible());
  }
  void imageTextLongSidebarScrollAndReverseSelection() {
    QTemporaryDir dir; Store store(dir.path());
    QWidget owner; owner.setGeometry(QGuiApplication::primaryScreen()->availableGeometry()); owner.show();
    QImage image(800,5200,QImage::Format_RGB32); image.fill(Qt::white);
    QJsonArray lines;
    for (int row=0;row<100;++row) {
      auto line=imageTextFixture()["lines"].toArray()[2].toObject();
      auto shift=[&](QJsonValue value) {
        QJsonArray result;
        for (auto point:value.toArray()) { auto p=point.toArray(); p[1]=p[1].toDouble()+row*50; result.append(p); }
        return result;
      };
      line["quad"]=shift(line["quad"]);
      auto characters=line["characters"].toArray();
      for (int n=0;n<characters.size();++n) {
        auto character=characters[n].toObject(); character["quad"]=shift(character["quad"]); characters[n]=character;
      }
      line["characters"]=characters; lines.append(line);
    }
    ImageTextOverlay text(&owner,&store,[&]{return owner.rect();});
    text.begin(image,false); text.setResult({{"lines",lines}});
    text.selectAll(); QApplication::processEvents();
    auto editor=text.resultEditor(); QVERIFY(editor->verticalScrollBar()->maximum()>0);
    QVERIFY(editor->verticalScrollBar()->value()>0);
    auto cursor=editor->textCursor(); cursor.setPosition(5); cursor.setPosition(0,QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    QCOMPARE(text.selectedText(),QString("金额\n金额"));
    QCOMPARE(text.page().highlights(0,5).size(),4);
    editor->setFocus(); text.setFocus();
    QVERIFY(!editor->extraSelections().isEmpty());
    QVERIFY(editor->isReadOnly());
    text.finish();
  }
  void selectionToolbarTracksDragFrames_data() {
    QTest::addColumn<bool>("recording");
    QTest::newRow("screenshot") << false;
    QTest::newRow("recording-selection") << true;
  }
  void selectionToolbarTracksDragFrames() {
    QFETCH(bool, recording);
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, recording, desktop);
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    overlay->selectRegion({260, 100, 480, 250});
    overlay->show();
    QWidget *bar = recording ? overlay->findChild<QFrame *>("recordToolbar")
                             : static_cast<QWidget *>(overlay->toolbar);
    QVERIFY(bar);
    QVERIFY(bar->isVisible());
    const auto initial = bar->pos();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {300, 200});
    for (auto delta : {QPoint(15, 10), QPoint(35, 25), QPoint(20, 15)}) {
      dragMove(overlay, QPoint(300, 200) + delta);
      QCOMPARE(bar->pos(), initial + delta);
      QVERIFY(bar->isVisible());
    }
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {320, 215});
    QCOMPARE(bar->pos(), initial + QPoint(20, 15));
    auto se = overlay->selection.bottomRight();
    const auto beforeResize = bar->pos();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, se);
    for (auto delta : {QPoint(10, 5), QPoint(25, 15)}) {
      dragMove(overlay, se + delta);
      QCOMPARE(bar->pos(), beforeResize + delta);
    }
    // The release point can arrive without a final mouse-move event.
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier,
                        se + QPoint(30, 20));
    QCOMPARE(bar->pos(), beforeResize + QPoint(30, 20));
    auto beforeKey = bar->pos();
    QTest::keyClick(overlay, Qt::Key_Right);
    QCOMPARE(bar->pos(), beforeKey + QPoint(1, 0));
    overlay->selectRegion({300, 500, 300, 60});
    QVERIFY(bar->geometry().bottom() < overlay->selection.top());
    QVERIFY(overlay->rect().contains(bar->geometry()));
    QVERIFY(savePreview(overlay, recording ? "recording-toolbar-follow.png"
                                          : "capture-toolbar-follow.png"));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void toolbarAnimationAndStableActions() {
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    Annotation a(image);
    Toolbar bar;
    bar.sync(a);
    bar.show();
    QApplication::processEvents();
    auto motion = bar.findChild<QVariantAnimation *>("toolbarSelectionAnimation");
    QVERIFY(motion);
    QCOMPARE(motion->state(), QAbstractAnimation::Stopped);
    const auto mainSize = bar.mainBar->size();
    QMap<QToolButton *, QPoint> actionPositions;
    for (auto button : bar.mainBar->findChildren<QToolButton *>()) {
      QCOMPARE(button->size(), QSize(32, 32));
      QCOMPARE(button->iconSize(), QSize(16, 16));
      if (button->objectName().startsWith("action_"))
        actionPositions[button] = button->mapToGlobal(QPoint(0, 0));
    }
    auto stableActions = [&] {
      QCOMPARE(bar.mainBar->size(), mainSize);
      for (auto button : actionPositions.keys())
        QCOMPARE(button->mapToGlobal(QPoint(0, 0)), actionPositions[button]);
    };
    QSignalSpy chosen(&bar, &Toolbar::toolChosen);
    QTest::mouseClick(bar.findChild<QToolButton *>("tool_pen"), Qt::LeftButton);
    QCOMPARE(chosen.takeFirst().first().toString(), QString("pen"));
    auto pen = bar.findChild<QToolButton *>("tool_pen");
    a.chooseTool("pen");
    bar.sync(a);
    QCOMPARE(motion->state(), QAbstractAnimation::Running);
    motion->pause();
    motion->setCurrentTime(70);
    QVERIFY(pen->width() > 32);
    const auto penDuringTransition = pen->geometry();
    QVERIFY(savePreview(bar.mainBar, "toolbar-pen-transition.png"));
    stableActions();
    // Changing width/color while a transition runs must preserve its progress.
    bar.sync(a);
    QCOMPARE(motion->currentTime(), 70);
    QCOMPARE(pen->geometry(), penDuringTransition);
    a.chooseTool("text");
    bar.sync(a);
    QCOMPARE(pen->geometry(), penDuringTransition);
    motion->pause();
    for (int time : {0, 20, 70, 120, 200}) {
      motion->setCurrentTime(time);
      stableActions();
      auto group = pen->parentWidget();
      const auto buttons = group->findChildren<QToolButton *>();
      for (int n = 1; n < buttons.size(); ++n)
        QVERIFY(buttons[n - 1]->geometry().right() < buttons[n]->x());
    }
    QCOMPARE(pen->width(), 32);
    auto text = bar.findChild<QToolButton *>("tool_text");
    QVERIFY(text->width() > 32);
    QVERIFY(text->isChecked());
    const auto main = bar.mainBar->geometry(), options = bar.optionsBar->geometry();
    QCOMPARE(main.left(), options.left());
    QVERIFY(savePreview(&bar, "toolbar-text-options.png"));
    QVERIFY(!bar.findChild<QToolButton *>("action_undo")->isEnabled());
    Mark m;
    m.kind = "line";
    m.points = {{10, 20}, {90, 20}};
    a.add(m);
    bar.sync(a);
    QVERIFY(bar.findChild<QToolButton *>("action_undo")->isEnabled());
    a.undo();
    bar.sync(a);
    QVERIFY(bar.findChild<QToolButton *>("action_redo")->isEnabled());
    a.chooseTool("pen");
    bar.sync(a);
    bar.hide();
    QCOMPARE(motion->state(), QAbstractAnimation::Stopped);
    QVERIFY(pen->width() > 32);
    a.chooseTool("ellipse");
    bar.sync(a);
    bar.show();
    QCOMPARE(motion->state(), QAbstractAnimation::Stopped);
    QVERIFY(bar.findChild<QToolButton *>("tool_shapes")->width() > 32);
    QVERIFY(savePreview(&bar, "toolbar-shapes-motion.png"));
  }
  void toolbarKeyboardRecordingAndPinIntegration() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "pen");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    auto bar = overlay->toolbar;
    auto copy = bar->findChild<QToolButton *>("action_copy");
    const auto copyPosition = copy->mapToGlobal(QPoint(0, 0));
    QTest::keyClick(overlay, Qt::Key_6, Qt::ControlModifier);
    QCOMPARE(overlay->annotation->tool, QString("text"));
    QCOMPARE(copy->mapToGlobal(QPoint(0, 0)), copyPosition);
    auto motion = bar->findChild<QVariantAnimation *>("toolbarSelectionAnimation");
    QVERIFY(motion);
    QTest::keyClick(overlay, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(overlay->annotation->tool, QString("arrow"));
    motion->pause();
    QApplication::processEvents();
    struct PaintObserver : QObject {
      int paints = 0;
      bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
      }
    } observer;
    overlay->installEventFilter(&observer);
    for (int time : {20, 60, 100, 140, 200}) {
      motion->setCurrentTime(time);
      QApplication::processEvents();
    }
    QCOMPARE(observer.paints, 0); // A selection transition must not repaint the desktop overlay.
    overlay->removeEventFilter(&observer);
    bar->findChild<QToolButton *>("action_record")->click();
    QVERIFY(overlay->recording);
    QCOMPARE(motion->state(), QAbstractAnimation::Stopped);
    QVERIFY(savePreview(overlay, "record-from-capture.png"));
    QTest::keyClick(overlay, Qt::Key_Escape);
    QVERIFY(!overlay->recording);
    QVERIFY(bar->isVisible());
    QCOMPARE(overlay->annotation->tool, QString("arrow"));
    QCOMPARE(copy->mapToGlobal(QPoint(0, 0)), copyPosition);
    QVERIFY(savePreview(overlay, "capture-with-motion-toolbar.png"));
    PinWindow pin(overlay->annotation->render(), &store);
    pin.show();
    pin.startEditing();
    pin.chooseTool("pen");
    QVERIFY(pin.toolbar->isVisible());
    auto pinMotion = pin.toolbar->findChild<QVariantAnimation *>("toolbarSelectionAnimation");
    QVERIFY(pinMotion);
    QTest::keyClick(&pin, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(pin.annotation->tool, QString("arrow"));
    pinMotion->setCurrentTime(pinMotion->duration());
    QVERIFY(savePreview(&pin, "pin-with-motion-toolbar.png"));
  }
  void shapeButtonsFillRenderingHitAndMemory() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "rect");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    auto bar = overlay->toolbar;
    QVERIFY(bar->rectButton->isChecked());
    QVERIFY(!bar->fillButton->isChecked());
    for (auto combo : bar->findChildren<QComboBox *>())
      QVERIFY(!combo->isVisible());
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {160, 150});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {360, 250});
    auto &a = *overlay->annotation;
    const auto points = a.marks.first().points;
    QVERIFY(!paintedHit(a.marks.first(), {160, 120}));
    QCOMPARE(a.render().pixelColor(160, 120), a.original.pixelColor(160, 120));
    QVERIFY(paintedHit(a.marks.first(), {60, 120}));
    QVERIFY(savePreview(bar, "toolbar-shapes-outline.png"));
    QTest::mouseClick(bar->fillButton, Qt::LeftButton);
    QVERIFY(bar->fillButton->isChecked());
    QVERIFY(a.marks.first().filled);
    QCOMPARE(a.marks.first().points, points);
    QVERIFY(paintedHit(a.marks.first(), {160, 120}));
    QCOMPARE(a.render().pixelColor(160, 120), QColor(255, 0, 4));
    QVERIFY(savePreview(bar, "toolbar-shapes-filled.png"));
    QTest::mouseMove(overlay, {30, 30});
    QVERIFY(savePreview(overlay, "shapes-filled-editing.png"));
    QTest::keyClick(overlay, Qt::Key_Z, Qt::ControlModifier);
    QVERIFY(!a.marks.first().filled);
    QTest::keyClick(overlay, Qt::Key_Y, Qt::ControlModifier);
    QVERIFY(a.marks.first().filled);
    QTest::mouseClick(bar->ellipseButton, Qt::LeftButton);
    QCOMPARE(a.tool, QString("ellipse"));
    QVERIFY(bar->ellipseButton->isChecked());
    QVERIFY(!bar->rectButton->isChecked());
    QVERIFY(!bar->fillButton->isChecked());
    QTest::mouseClick(bar->fillButton, Qt::LeftButton);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {180, 170});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {400, 270});
    QVERIFY(a.marks.last().filled);
    QVERIFY(paintedHit(a.marks.last(), {190, 140}));
    QVERIFY(!paintedHit(a.marks.last(), {80, 90}));
    QCOMPARE(Mark::fromJson(a.marks.last().json()).json(), a.marks.last().json());
    QTest::mouseClick(bar->rectButton, Qt::LeftButton);
    QVERIFY(a.preferences.filled);
    overlay->chooseTool("pen");
    QTest::mouseClick(bar->findChild<QToolButton *>("tool_shapes"), Qt::LeftButton);
    QCOMPARE(a.tool, QString("rect"));
    overlay->chooseTool("rect");
    QVERIFY(bar->fillButton->isChecked());
    Store reopened(dir.path());
    Annotation restored(a.original, &reopened);
    restored.chooseTool("rect");
    QVERIFY(restored.preferences.filled);
    restored.chooseTool("ellipse");
    QVERIFY(restored.preferences.filled);
    QVERIFY(!Mark::fromJson({}).filled);
  }
  void compactOptionsAndDirectToolButtons() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "arrow");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    auto bar = overlay->toolbar;
    auto &a = *overlay->annotation;
    auto styles = bar->lineButton->menu()->actions();
    QCOMPARE(styles.size(), 4);
    for (int n = 0; n < styles.size(); ++n) {
      styles[n]->trigger();
      QCOMPARE(a.tool, n ? QString("arrow") : QString("line"));
      QVERIFY(styles[n]->isChecked());
    }
    QTest::mouseClick(bar->directionButton, Qt::LeftButton);
    QCOMPARE(a.preferences.arrowMode, QString("reverse"));
    QVERIFY(savePreview(bar, "toolbar-arrow-options.png"));
    overlay->chooseTool("pen");
    QVERIFY(savePreview(bar, "toolbar-pen-options.png"));
    overlay->chooseTool("marker");
    QCOMPARE(bar->widthButton->text(), QString("12"));
    QVERIFY(!bar->areaButton->isChecked());
    QTest::mouseClick(bar->areaButton, Qt::LeftButton);
    QCOMPARE(a.preferences.areaShape, QString("rect"));
    QTest::mouseClick(bar->areaButton, Qt::LeftButton);
    QCOMPARE(a.preferences.areaShape, QString("stroke"));
    QVERIFY(savePreview(bar, "toolbar-marker-options.png"));
    overlay->chooseTool("mosaic");
    QTest::mouseClick(bar->blurButton, Qt::LeftButton);
    QCOMPARE(a.tool, QString("blur"));
    QVERIFY(bar->blurButton->isChecked());
    QVERIFY(!bar->mosaicButton->isChecked());
    bar->strength->setValue(6);
    QCOMPARE(a.preferences.intensity, 6);
    QCOMPARE(bar->strengthValue->text(), QString("6"));
    overlay->setPreference("width", 41);
    QCOMPARE(bar->widthButton->text(), QString("41"));
    QTest::mouseClick(bar->areaEllipseButton, Qt::LeftButton);
    QCOMPARE(a.preferences.areaShape, QString("ellipse"));
    QTest::mouseClick(bar->areaButton, Qt::LeftButton);
    QCOMPARE(a.preferences.areaShape, QString("rect"));
    QVERIFY(!bar->areaEllipseButton->isChecked());
    QVERIFY(savePreview(bar, "toolbar-blur-options.png"));
    overlay->chooseTool("eraser");
    overlay->setPreference("width", 50);
    QCOMPARE(bar->widthButton->text(), QString("50"));
    QVERIFY(!bar->colorButton->isVisible());
    QVERIFY(!bar->strength->isVisible());
    QVERIFY(savePreview(bar, "toolbar-eraser-options.png"));
    for (const auto &tool : QStringList{"rect", "ellipse", "arrow", "line", "pen",
                                       "marker", "mosaic", "blur", "eraser", "number"}) {
      overlay->chooseTool(tool);
      for (auto combo : bar->findChildren<QComboBox *>())
        QVERIFY(!combo->isVisible());
    }
    overlay->chooseTool("blur");
    QCOMPARE(a.preferences.intensity, 6);
    QCOMPARE(a.preferences.areaShape, QString("rect"));
    QVERIFY(bar->optionsBar->width() < 350);
    QCOMPARE(bar->optionsBar->size(), bar->optionsBar->sizeHint());
  }
  void widthButtonSelectsStrokeWithoutPopup_data() {
    QTest::addColumn<QString>("tool");
    QTest::addColumn<QString>("area");
    QTest::addColumn<bool>("pin");
    for (bool pin : {false, true})
      for (const auto &mode : QList<QPair<QString, QString>>{
               {"rect", "filled"}, {"ellipse", "filled"}, {"marker", "rect"},
               {"mosaic", "rect"}, {"mosaic", "ellipse"}, {"blur", "rect"},
               {"blur", "ellipse"}, {"eraser", "rect"}, {"eraser", "ellipse"}}) {
        auto name = (pin ? "pin-" : "capture-") + mode.first + "-" + mode.second;
        QTest::newRow(qPrintable(name)) << mode.first << mode.second << pin;
      }
  }
  void widthButtonSelectsStrokeWithoutPopup() {
    QFETCH(QString, tool);
    QFETCH(QString, area);
    QFETCH(bool, pin);
    QTemporaryDir dir;
    Store store(dir.path());
    EditingSurface *surface;
    if (pin) {
      QImage image(600, 400, QImage::Format_ARGB32);
      image.fill(Qt::white);
      auto window = new PinWindow(image, &store, {100, 80, 600, 400});
      window->show();
      window->startEditing();
      window->chooseTool(tool);
      surface = window;
    } else {
      surface = editingFixture(store, tool);
    }
    QPointer<EditingSurface> alive = surface;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    auto &a = *surface->annotation;
    auto bar = surface->toolbar;
    const bool shape = area == "filled";
    surface->setPreference(shape ? "filled" : "area_shape",
                           shape ? QVariant(true) : QVariant(area));
    Mark mark = a.preferences;
    mark.points = {{40, 50}, {260, 160}};
    a.add(mark);
    bar->sync(a);
    const auto points = mark.points;
    const auto color = mark.color;
    const auto width = mark.width;
    const auto undoCount = a.undoStates.size();
    bool popupShown = false;
    QTimer::singleShot(0, bar, [&] {
      if (auto popup = QApplication::activePopupWidget()) {
        popupShown = true;
        popup->close();
      }
    });
    QSignalSpy changed(bar, &Toolbar::preferencesChanged);
    QTest::mouseClick(bar->widthButton, Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(!popupShown);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(a.tool, tool);
    QVERIFY(shape ? !a.preferences.filled : a.preferences.areaShape == "stroke");
    QVERIFY(shape ? !a.marks.last().filled : a.marks.last().areaShape == "stroke");
    QCOMPARE(a.marks.last().points, points);
    QCOMPARE(a.preferences.width, width);
    QCOMPARE(a.preferences.color, color);
    QCOMPARE(a.selected, 0);
    QCOMPARE(a.undoStates.size(), undoCount + 1);
    QVERIFY(bar->widthButton->styleSheet().contains("#087ae8"));
    QVERIFY(!bar->areaButton->isChecked());
    QVERIFY(!bar->areaEllipseButton->isChecked());
    QVERIFY(!bar->fillButton->isChecked());
    // Selecting the already active mode is a no-op, including undo history.
    QTimer::singleShot(0, bar, [&] {
      if (auto popup = QApplication::activePopupWidget()) {
        popupShown = true;
        popup->close();
      }
    });
    QTest::mouseClick(bar->widthButton, Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY(!popupShown);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(a.undoStates.size(), undoCount + 1);
    // Both directions remain a single click and update the latest mark.
    QTest::mouseClick(shape ? bar->fillButton : bar->areaButton, Qt::LeftButton);
    QVERIFY(shape ? a.preferences.filled : a.preferences.areaShape == "rect");
    QTest::mouseClick(bar->widthButton, Qt::LeftButton);
    QVERIFY(shape ? !a.preferences.filled : a.preferences.areaShape == "stroke");
    QVERIFY(savePreview(bar, QString("mode-return-%1-%2-%3.png")
                                .arg(pin ? "pin" : "capture", tool, area)));
    Store reopened(dir.path());
    Annotation restored(a.original, &reopened);
    restored.chooseTool(tool);
    QVERIFY(shape ? !restored.preferences.filled : restored.preferences.areaShape == "stroke");
  }
  void widthContextMenuKeepsModeAndChangesSize_data() {
    QTest::addColumn<QString>("tool");
    QTest::newRow("filled-rectangle") << QString("rect");
    QTest::newRow("marker-rectangle") << QString("marker");
  }
  void widthContextMenuKeepsModeAndChangesSize() {
    QFETCH(QString, tool);
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, tool);
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    const bool shape = tool == "rect";
    overlay->setPreference(shape ? "filled" : "area_shape",
                           shape ? QVariant(true) : QVariant("rect"));
    auto bar = overlay->toolbar;
    bool opened = false;
    QTimer::singleShot(0, bar, [&] {
      if (auto menu = namedMenu("annotationWidthMenu")) {
        if (auto slider = menu->findChild<QSlider *>("annotationWidthSlider")) {
          opened = true;
          slider->setValue(30);
        }
        menu->close();
      }
    });
    auto button = bar->widthButton;
    QContextMenuEvent event(QContextMenuEvent::Mouse, {10, 10},
                            button->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(button, &event);
    QVERIFY(opened);
    auto &preferences = overlay->annotation->preferences;
    QVERIFY(shape ? preferences.filled : preferences.areaShape == "rect");
    QCOMPARE(preferences.width, shape ? 30.0 : 10.0);
    QVERIFY(bar->widthButton->toolTip().contains("右键"));
  }
  void numberedCircleToolbarSequenceAndEditing() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "rect");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    QTest::keyClick(overlay, Qt::Key_8, Qt::ControlModifier);
    auto &a = *overlay->annotation;
    auto bar = overlay->toolbar;
    QCOMPARE(a.tool, QString("number"));
    auto button = bar->findChild<QToolButton *>("tool_number");
    QVERIFY(button->isChecked());
    QVERIFY(button->toolTip().contains("Ctrl+8"));
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {180, 160});
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {300, 190});
    QCOMPARE(a.marks.size(), 2);
    QCOMPARE(a.marks.first().number, 1);
    QCOMPARE(a.marks.last().number, 2);
    QCOMPARE(markBounds(a.marks.first()).center(), QPointF(80, 80));
    const auto first = a.marks.first().json();
    overlay->setPreference("font_size", 36);
    overlay->setPreference("color", QColor("#1688ef"));
    QCOMPARE(a.marks.last().number, 2);
    QCOMPARE(a.marks.last().fontSize, 36.0);
    QCOMPARE(a.marks.first().json(), first);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {430, 240});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {450, 260});
    QCOMPARE(a.marks.size(), 3);
    QCOMPARE(a.marks.last().number, 3);
    QCOMPARE(markBounds(a.marks.last()).center(), QPointF(330, 160));
    QCOMPARE(a.preferences.number, 4);
    QTest::keyClick(overlay, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(a.marks.size(), 2);
    QCOMPARE(a.preferences.number, 3);
    QTest::keyClick(overlay, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(a.preferences.number, 4);
    QTest::keyClick(overlay, Qt::Key_3, Qt::ControlModifier);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(a.tool, QString("number"));
    QCOMPARE(a.preferences.number, 4);
    a.confirm();
    QTest::mouseMove(overlay, {30, 30});
    QVERIFY(bar->colorButton->x() < 45);
    QVERIFY(savePreview(bar, "toolbar-number-options.png"));
    QVERIFY(savePreview(overlay, "numbered-circles.png"));
    Store reopened(dir.path());
    Annotation restored(a.original, &reopened);
    restored.chooseTool("number");
    QCOMPARE(restored.preferences.number, 1);
    QCOMPARE(restored.preferences.fontSize, 36.0);
    QCOMPARE(restored.preferences.color, QColor("#1688ef"));
  }
  void inlineTextInputTransparencyAndF3Export() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    const QColor background(43, 71, 93);
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(background);
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    overlay->selectRegion({100, 80, 480, 300});
    overlay->chooseTool("text");
    overlay->show();
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {200, 150});
    auto editor = overlay->findChild<InlineTextEditor *>("inlineTextEditor");
    QVERIFY(editor);
    QVERIFY(editor->isVisible());
    QVERIFY(!editor->isWindow());
    QCOMPARE(editor->parentWidget(), static_cast<QWidget *>(overlay));
    QCOMPARE(editor->findChildren<QToolButton *>().size(), 5);
    QTest::keyClicks(editor, "Inline 123");
    QTest::keyClick(editor, Qt::Key_Return);
    QInputMethodEvent chinese;
    chinese.setCommitString("中文直接输入");
    QApplication::sendEvent(editor, &chinese);
    QCOMPARE(editor->value().text, QString("Inline 123\n中文直接输入"));
    auto initialFormat = editor->textItem()->textCursor();
    initialFormat.movePosition(QTextCursor::Start);
    initialFormat.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE(initialFormat.charFormat().foreground().color(), QColor(255, 0, 4));
    auto preview = overlay->grab().toImage();
    QCOMPARE(preview.pixelColor(450, 330), background);
    QVERIFY(savePreview(overlay, "inline-text-editing.png"));
    const auto pending = editor->value();
    QImage exported;
    QString exportedAction;
    overlay->output = [&](const QImage &value, const QRect &, const QString &action) {
      exportedAction = action;
      exported = value;
      return true;
    };
    QImage expected = overlay->annotation->original.copy();
    paintMark(expected, pending);
    QTest::keyClick(editor, Qt::Key_F3);
    QCOMPARE(exportedAction, QString("pin"));
    QCOMPARE(exported, expected);
    QVERIFY(exported != image.copy({100, 80, 480, 300}));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void inlineTextToolbarSettingsAndMemory() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "text");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {180, 160});
    auto editor = overlay->findChild<InlineTextEditor *>();
    auto bar = overlay->toolbar;
    QVERIFY(!bar->widthButton->isVisible());
    QVERIFY(bar->outlineButton->isVisible());
    QCOMPARE(bar->findChild<QWidget *>("annotationPalette")
                 ->findChildren<QToolButton *>().size(), 20);
    bar->fontSize->setEditText("8.75");
    QVERIFY(QMetaObject::invokeMethod(bar->fontSize->lineEdit(), "editingFinished"));
    QVERIFY(qAbs(overlay->annotation->preferences.fontSize - 35.0 / 3) < .001);
    QTest::mouseClick(bar->boldButton, Qt::LeftButton);
    QTest::mouseClick(bar->italicButton, Qt::LeftButton);
    QTest::mouseClick(bar->outlineButton, Qt::LeftButton);
    overlay->setPreference("color", QColor(0, 180, 255));
    QTest::keyClicks(editor, "Format");
    auto pending = editor->value();
    QVERIFY(pending.bold);
    QVERIFY(pending.italic);
    QCOMPARE(pending.outlineWidth, 1);
    QCOMPARE(pending.color, QColor(0, 180, 255));
    QCOMPARE(editor->textItem()->font().pointSizeF(), 8.75);
    auto actualText = overlay->grab().toImage();
    QImage expectedText = overlay->annotation->original.copy();
    paintMark(expectedText, pending);
    QVERIFY(savePreview(overlay, "inline-text-formatting.png"));
    auto textPreviewFolder = qEnvironmentVariable("FLOATSHOT_ANNOTATION_TEST_DIR");
    if (!textPreviewFolder.isEmpty()) {
      QVERIFY(expectedText.save(textPreviewFolder + "/inline-text-export.png"));
    }
    auto cursor = editor->textItem()->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QCOMPARE(cursor.charFormat().foreground().color(), pending.color);
    QVERIFY(cursor.charFormat().textOutline().style() != Qt::NoPen);
    const auto bounds = markBounds(pending).toAlignedRect().adjusted(8, 4, -8, -4);
    int different = 0, pixels = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
      for (int x = bounds.left(); x <= bounds.right(); ++x) {
        auto a = actualText.pixelColor(x + overlay->selection.x(),
                                       y + overlay->selection.y());
        auto b = expectedText.pixelColor(x, y);
        if (qAbs(a.red() - b.red()) > 10 || qAbs(a.green() - b.green()) > 10 ||
            qAbs(a.blue() - b.blue()) > 10)
          ++different;
        ++pixels;
      }
    QVERIFY2(different < qMax(1, pixels / 20),
             qPrintable(QString("Editing text differs from exported glyphs: %1/%2")
                            .arg(different).arg(pixels)));
    QVERIFY(savePreview(overlay, "inline-text-formatting.png"));
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(overlay->annotation->marks.size(), 1);
    QCOMPARE(overlay->annotation->marks.first().text, QString("Format"));
    QVERIFY(!editor->isVisible());
    Store reopened(dir.path());
    Annotation restored(QImage(50, 50, QImage::Format_ARGB32), &reopened);
    restored.chooseTool("text");
    QVERIFY(qAbs(restored.preferences.fontSize - 35.0 / 3) < .001);
    QVERIFY(restored.preferences.bold && restored.preferences.italic);
    QCOMPARE(restored.preferences.outlineWidth, 1);
    QCOMPARE(restored.preferences.color, QColor(0, 180, 255));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void inlineTextResizeRotateMoveDeleteAndCancel() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "text");
    QPointer<CaptureOverlay> alive = overlay;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {240, 200});
    auto editor = overlay->findChild<InlineTextEditor *>();
    QTest::keyClicks(editor, "Resize");
    const auto initial = editor->value();
    auto corner = editor->findChild<QToolButton *>("text_se");
    QTest::mousePress(corner, Qt::LeftButton, Qt::NoModifier, {7, 7});
    dragMove(corner, {35, 22});
    QTest::mouseRelease(corner, Qt::LeftButton);
    QVERIFY(editor->value().fontSize > initial.fontSize);
    QCOMPARE(editor->value().points.first(), initial.points.first());
    auto rotate = editor->findChild<QToolButton *>("text_rotate");
    QTest::mousePress(rotate, Qt::LeftButton, Qt::NoModifier, {7, 7});
    dragMove(rotate, {28, 23});
    QTest::mouseRelease(rotate, Qt::LeftButton);
    QVERIFY(editor->value().rotation != 0);
    QVERIFY(savePreview(overlay, "inline-text-rotated.png"));
    auto before = editor->value();
    const auto edge = editor->mapFromScene(editor->textItem()->mapToScene({10, 0}));
    QTest::mousePress(editor->viewport(), Qt::LeftButton, Qt::NoModifier, edge);
    dragMove(editor->viewport(), edge + QPoint(20, 10));
    QTest::mouseRelease(editor->viewport(), Qt::LeftButton, Qt::NoModifier,
                        edge + QPoint(20, 10));
    QCOMPARE(editor->value().points.first(), before.points.first() + QPointF(20, 10));
    QTest::mouseClick(editor->findChild<QToolButton *>("text_delete"), Qt::LeftButton);
    QVERIFY(!editor->isVisible());
    QVERIFY(overlay->annotation->marks.isEmpty());
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {260, 240});
    QTest::keyClicks(editor, "cancel");
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(!editor->isVisible());
    QVERIFY(overlay->annotation->marks.isEmpty());
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void inlineTextOnScaledPinAndOutsideConfirmation() {
    QTemporaryDir dir;
    Store store(dir.path());
    store.config["annotation_last_tool"] = "text";
    QImage image(600, 400, QImage::Format_ARGB32);
    image.fill(QColor(190, 210, 230));
    auto pin = new PinWindow(image, &store, {100, 100, 300, 200});
    QPointer<PinWindow> alive = pin;
    auto cleanup = qScopeGuard([&] { if (alive) delete alive; });
    pin->show();
    pin->startEditing();
    QTest::mouseClick(pin, Qt::LeftButton, Qt::NoModifier, {80, 65});
    auto editor = pin->findChild<InlineTextEditor *>();
    QVERIFY(editor && editor->isVisible());
    QCOMPARE(editor->transform().m11(), .5);
    QTest::keyClicks(editor, "Pin 50%");
    auto size = editor->value().fontSize;
    wheel(editor->viewport(), {90, 80}, 1);
    QVERIFY(editor->value().fontSize > size);
    // A blank click commits the existing text and begins the next insertion.
    QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, {200, 150});
    QCOMPARE(pin->annotation->marks.size(), 1);
    QCOMPARE(pin->annotation->marks.first().text, QString("Pin 50%"));
    QTest::keyClicks(editor, "Copy pending");
    QString requestedAction;
    connect(pin, &PinWindow::requested, pin,
            [&](PinWindow *, const QString &action) { requestedAction = action; });
    QVERIFY(pin->complete("copy"));
    QCOMPARE(requestedAction, QString("copy"));
    QCOMPARE(pin->annotation->marks.size(), 2);
    QVERIFY(!editor->isVisible());
    QVERIFY(pin->displayedImage() != image);
    pin->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void imageExportCodecs() {
    QTemporaryDir dir;
    QImage image(64, 48, QImage::Format_ARGB32);
    image.fill(QColor(255, 0, 4));
    for (const auto &format : QStringList{"png", "jpg", "webp"}) {
      auto path = dir.path() + "/原生截图." + format;
      QVERIFY2(image.save(path), qPrintable("Cannot encode " + format));
      QImageReader reader(path);
      QVERIFY(reader.canRead());
      QCOMPARE(reader.read().size(), image.size());
    }
  }
  void legacyDataMigration() {
    QTemporaryDir dir;
    auto legacy = dir.path() + "/FloatShot",
         destination = dir.path() + "/FloatShotNative";
    QDir().mkpath(legacy + "/history");
    QDir().mkpath(legacy + "/pins");
    QImage image(40, 30, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(legacy + "/history/old.png"));
    QVERIFY(image.save(legacy + "/pins/old.png"));
    Store::writeJson(legacy + "/settings.json",
                     QJsonObject{{"startup", true},
                                 {"annotation_color", "#ff123456"},
                                 {"hotkeys", QJsonObject{{"capture", "F2"}}}});
    Store::writeJson(
        legacy + "/history.json",
        QJsonArray{QJsonObject{
            {"path", QDir::toNativeSeparators(legacy + "/history/old.png")},
            {"output", "D:/unchanged.png"}}});
    Store::writeJson(legacy + "/workspace.json",
                     QJsonArray{QJsonObject{{"id", "old"}, {"x", 100}}});
    QVERIFY(Store::importLegacy(destination, legacy));
    Store imported(destination);
    QCOMPARE(imported.config["annotation_color"].toString(),
             QString("#ff123456"));
    QVERIFY(imported.config["startup"].toBool());
    QCOMPARE(imported.config["hotkeys"].toObject()["capture"].toString(),
             QString("F2"));
    QCOMPARE(imported.config["hotkeys"].toObject()["pin"].toString(),
             QString("F3"));
    QCOMPARE(imported.history.first().toObject()["path"].toString(),
             destination + "/history/old.png");
    QCOMPARE(imported.history.first().toObject()["output"].toString(),
             QString("D:/unchanged.png"));
    QVERIFY(QFile::exists(legacy + "/history/old.png"));
    QVERIFY(QImage(destination + "/pins/old.png") == image);
    QVERIFY(!Store::importLegacy(destination, legacy));
  }
  void liveSelectionAndSpaceMovement() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, y, QColor(x % 256, y % 256, 40));
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({100, 80, 480, 300});
    overlay->chooseTool("select");
    overlay->show();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {240, 180});
    QTest::mouseMove(overlay, {270, 200});
    QCOMPARE(overlay->annotation->original.pixelColor(0, 0),
             image.pixelColor(130, 100));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {270, 200});
    auto se = overlay->selection.bottomRight();
    QMouseEvent hover(QEvent::MouseMove, QPointF(se), QPointF(overlay->mapToGlobal(se)),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(overlay, &hover);
    QCOMPARE(overlay->cursor().shape(), Qt::SizeFDiagCursor);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, se);
    QTest::mouseMove(overlay, se + QPoint(30, 20));
    QCOMPARE(overlay->annotation->original.size(), overlay->selection.size());
    QCOMPARE(overlay->annotation->original.pixelColor(509, 319),
             image.pixelColor(639, 419));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier,
                        se + QPoint(30, 20));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {20, 500});
    QTest::mouseMove(overlay, {100, 560});
    auto initial = overlay->selection;
    QTest::keyPress(overlay, Qt::Key_Space);
    QTest::mouseMove(overlay, {120, 570});
    QCOMPARE(overlay->selection, initial.translated(20, 10));
    QTest::keyRelease(overlay, Qt::Key_Space);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {120, 570});
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void exitPromptAtSelectionAndRotatedWidth() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({100, 80, 420, 300});
    overlay->chooseTool("rect");
    Mark mark = overlay->annotation->preferences;
    mark.points = {{30, 50}, {230, 180}};
    overlay->annotation->add(mark);
    overlay->show();
    auto rotate =
        markControls(mark)["rotate"].toPoint() + overlay->selection.topLeft();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, rotate);
    QTest::mouseMove(overlay, rotate + QPoint(50, 20));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier,
                        rotate + QPoint(50, 20));
    auto rotation = overlay->annotation->marks.first().rotation;
    QVERIFY(rotation != 0);
    overlay->annotation->adjustWidth(2);
    QCOMPARE(overlay->annotation->marks.first().rotation, rotation);
    bool seen = false;
    QPoint center;
    QTimer::singleShot(30, [&] {
      if (auto box =
              qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        seen = true;
        center = box->frameGeometry().center();
        box->reject();
      }
    });
    QVERIFY(!overlay->complete("cancel"));
    QVERIFY(seen);
    QVERIFY2((center - overlay->globalSelection().center()).manhattanLength() <=
                 4,
             qPrintable(QString("prompt %1,%2 selection %3,%4")
                            .arg(center.x())
                            .arg(center.y())
                            .arg(overlay->globalSelection().center().x())
                            .arg(overlay->globalSelection().center().y())));
    QVERIFY(overlay->annotation);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void initTestCase() {
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
  }
  void qrRecognition() {
    auto matrix = ZXing::MultiFormatWriter(ZXing::BarcodeFormat::QRCode)
                      .setMargin(3)
                      .encode("FloatShot native QR", 180, 180);
    QImage image(matrix.width(), matrix.height(), QImage::Format_Grayscale8);
    for (int y = 0; y < matrix.height(); ++y)
      for (int x = 0; x < matrix.width(); ++x)
        image.setPixelColor(x, y, matrix.get(x, y) ? Qt::black : Qt::white);
    QVERIFY(recognizeCodes(image).contains("FloatShot native QR"));
  }
  void localOcr() {
    QImage image(1000, 180, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font("Arial");
    font.setPixelSize(56);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(40, 110, "FLOATSHOT 12345");
    painter.end();
    QString result, error;
    auto thread = QThread::create([&] {
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        result = recognizeWindowsText(image);
        winrt::uninit_apartment();
      } catch (const winrt::hresult_error &e) {
        error = QString::fromWCharArray(e.message().c_str());
      } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
      }
    });
    thread->start();
    QVERIFY(thread->wait(15000));
    delete thread;
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(result.toUpper().remove(' ').contains("FLOATSHOT12345"),
             qPrintable(result));
  }
  void interactiveEndpointAndShapeEditing() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({100, 80, 480, 300});
    overlay->chooseTool("line");
    overlay->show();
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {140, 160});
    QTest::mouseMove(overlay, {380, 160});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {380, 160});
    QCOMPARE(overlay->annotation->marks.size(), 1);
    QCOMPARE(overlay->annotation->marks[0].points.first(), QPointF(40, 80));
    QCOMPARE(overlay->annotation->marks[0].points.last(), QPointF(280, 80));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {380, 160});
    QTest::mouseMove(overlay, {400, 180});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {400, 180});
    QCOMPARE(overlay->annotation->marks[0].points.first(), QPointF(40, 80));
    QCOMPARE(overlay->annotation->marks[0].points.last(), QPointF(300, 100));
    overlay->chooseTool("rect");
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {150, 190});
    QTest::mouseMove(overlay, {330, 300});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {330, 300});
    QCOMPARE(markControls(overlay->annotation->marks.last()).size(), 9);
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {240, 245});
    QCOMPARE(overlay->annotation->selected, -1);
    QCOMPARE(overlay->annotation->marks.size(), 2);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void defaultAndPersistentPreferences() {
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(!store.config["notifications_enabled"].toBool());
    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(Qt::white);
    Annotation a(image, &store);
    QCOMPARE(a.preferences.color, QColor(255, 0, 4));
    a.chooseTool("arrow");
    a.preferences.arrowHead = "hollow";
    a.preferences.color = QColor(12, 88, 180, 200);
    a.preferences.width = 9;
    a.savePreferences(&store);
    a.chooseTool("text");
    QCOMPARE(a.preferences.color, QColor(12, 88, 180, 200));
    a.preferences.bold = true;
    a.preferences.fontSize = 32;
    a.savePreferences(&store);
    Store reopened(dir.path());
    Annotation b(image, &reopened);
    b.chooseTool("arrow");
    QCOMPARE(b.preferences.arrowHead, QString("hollow"));
    QCOMPARE(b.preferences.width, 9.0);
    QCOMPARE(b.preferences.color, QColor(12, 88, 180, 200));
    b.chooseTool("text");
    QVERIFY(b.preferences.bold);
    QCOMPARE(b.preferences.fontSize, 32);
  }
  void freehandKeepsPositionsAndLocksWidthDuringDrawing() {
    QTemporaryDir dir;
    Store store(dir.path());
    store.config["annotation_options"] = QJsonObject{{"pen", QJsonObject{
        {"rotation", 45}, {"area_shape", "ellipse"}, {"width", 4}}}};
    auto overlay = editingFixture(store, "pen");
    auto &a = *overlay->annotation;
    QCOMPARE(a.preferences.rotation, 0);
    QCOMPARE(a.preferences.areaShape, QString("stroke"));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {140, 160});
    dragMove(overlay, {190, 170});
    auto firstPoints = a.draft->points;
    dragMove(overlay, {205, 240});
    QCOMPARE(a.draft->points.mid(0, firstPoints.size()), firstPoints);
    QCOMPARE(a.render().pixelColor(40, 80), QColor(255, 0, 4));
    QCOMPARE(a.selected, -1);
    QVERIFY(!overlay->toolbar->widthButton->isEnabled());
    wheel(overlay, {205, 240}, 1);
    QTest::keyClick(overlay, Qt::Key_2);
    overlay->setPreference("width", 12);
    QCOMPARE(a.preferences.width, 4.0);
    QCOMPARE(a.draft->width, 4.0);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {280, 180});
    QCOMPARE(a.marks.size(), 1);
    QCOMPARE(a.marks.first().points.last(), QPointF(180, 100));
    QVERIFY(markControls(a.marks.first()).isEmpty());
    QVERIFY(overlay->editTarget({140, 160}).isEmpty());
    QVERIFY(overlay->toolbar->widthButton->isEnabled());
    auto fixed = a.marks.first().json();
    overlay->setPreference("width", 9);
    wheel(overlay, {280, 180}, 1);
    QCOMPARE(a.preferences.width, 10.0);
    QCOMPARE(a.marks.first().json(), fixed);
    QVERIFY(savePreview(overlay, "freehand-fixed.png"));
    // Starting another stroke on old ink must draw, never grab a resize handle.
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {140, 160});
    dragMove(overlay, {175, 220});
    QCOMPARE(a.marks.first().json(), fixed);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {200, 240});
    QCOMPARE(a.marks.size(), 2);
    QCOMPARE(a.marks.last().width, 10.0);
    QCOMPARE(a.marks.first().json(), fixed);
    QTest::keyClick(overlay, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(a.marks.size(), 1);
    QCOMPARE(a.marks.first().json(), fixed);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void shiftPenTracksMouseAndEditsEndpoints() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "pen");
    auto &a = *overlay->annotation;
    overlay->setPreference("width", 6);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::ShiftModifier, {140, 140});
    dragMove(overlay, {260, 178}, Qt::ShiftModifier);
    QVERIFY(a.draft->straight);
    QCOMPARE(a.draft->points, QList<QPointF>({{40, 60}, {160, 98}}));
    QVERIFY(a.render() != a.original);
    // The gesture stays straight if Shift is released before the mouse.
    dragMove(overlay, {330, 195});
    QCOMPARE(a.draft->points, QList<QPointF>({{40, 60}, {230, 115}}));
    wheel(overlay, {330, 195}, 1);
    QCOMPARE(a.draft->width, 6.0);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {390, 215});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{40, 60}, {290, 135}}));
    QCOMPARE(a.selected, 0);
    QCOMPARE(markControls(a.marks.first()).keys(), QStringList({"end", "start"}));
    QCOMPARE(overlay->editTarget({140, 140}), QString("start"));
    QTest::mouseMove(overlay, {140, 140});
    QCOMPARE(overlay->cursor().shape(), Qt::SizeAllCursor);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {140, 140});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::ShiftModifier, {160, 165});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{60, 85}, {290, 135}}));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {390, 215});
    dragMove(overlay, {420, 240});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {450, 255});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{60, 85}, {350, 175}}));
    auto line = a.marks.first().json();
    QTest::keyClick(overlay, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(a.marks.first().points.last(), QPointF(290, 135));
    QTest::keyClick(overlay, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(a.marks.first().json(), line);
    // Serialized annotations retain endpoint editing; legacy pen paths do not.
    QVERIFY(Store::writeJson(store.path("line.json"), line));
    auto restored = Mark::fromJson(Store::readJson(store.path("line.json"), QJsonObject()).toObject());
    QCOMPARE(restored.json(), line);
    QCOMPARE(markControls(restored).size(), 2);
    line.remove("straight");
    QVERIFY(markControls(Mark::fromJson(line)).isEmpty());
    // Start another freehand stroke without Shift: the previous line is fixed.
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {180, 300});
    dragMove(overlay, {220, 310}, Qt::ShiftModifier);
    dragMove(overlay, {250, 280});
    QVERIFY(!a.draft->straight);
    QVERIFY(a.draft->points.size() > 2);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {280, 320});
    QCOMPARE(a.marks.size(), 2);
    QCOMPARE(a.selected, -1);
    QVERIFY(markControls(a.marks.last()).isEmpty());
    QVERIFY(overlay->editTarget({160, 165}).isEmpty());
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void shiftPenOnScaledPinAndClickConfirmation() {
    QTemporaryDir dir;
    Store store(dir.path());
    store.config["annotation_last_tool"] = "pen";
    QImage image(640, 360, QImage::Format_ARGB32);
    image.fill(Qt::white);
    auto pin = new PinWindow(image, &store, {100, 100, 320, 180});
    pin->show(); pin->startEditing();
    auto position = pin->pos();
    auto start = pin->imagePoint({52, 52});
    QTest::mousePress(pin, Qt::LeftButton, Qt::ShiftModifier, {52, 52});
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::ShiftModifier, {182, 97});
    auto &a = *pin->annotation;
    QCOMPARE(a.marks.first().points, QList<QPointF>({start, pin->imagePoint({182, 97})}));
    QCOMPARE(markControls(a.marks.first()).size(), 2);
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, {52, 52});
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, {62, 72});
    QCOMPARE(a.marks.first().points.first(), pin->imagePoint({62, 72}));
    auto fixed = a.marks.first().json();
    QTest::mouseClick(pin, Qt::LeftButton, Qt::NoModifier, {220, 150});
    QCOMPARE(a.selected, -1);
    QCOMPARE(a.marks.size(), 1);
    wheel(pin, {220, 150}, 1);
    QCOMPARE(a.marks.first().json(), fixed);
    QCOMPARE(pin->pos(), position);
    QVERIFY(savePreview(pin, "shift-pen-endpoints.png"));
    pin->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void markerStraightLineEndpointWidthAndConfirmation() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "marker");
    overlay->setPreference("width", 6);
    auto &a = *overlay->annotation;
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {140, 130});
    dragMove(overlay, {200, 220});
    dragMove(overlay, {350, 130});
    QCOMPARE(a.draft->points.size(), 2);
    QCOMPARE(a.render().pixelColor(100, 140), a.original.pixelColor(100, 140));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {380, 130});
    QCOMPARE(a.marks.size(), 1);
    QCOMPARE(a.marks.first().points, QList<QPointF>({{40, 50}, {280, 50}}));
    QCOMPARE(markControls(a.marks.first()).size(), 2);
    QVERIFY(a.render().pixelColor(100, 50) != a.original.pixelColor(100, 50));
    QVERIFY(savePreview(overlay, "marker-line-endpoints.png"));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {380, 130});
    // No intermediate move: the release position must still update the endpoint.
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {420, 190});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{40, 50}, {320, 110}}));
    wheel(overlay, {420, 190}, 1);
    QCOMPARE(a.marks.first().width, 7.0);
    QCOMPARE(a.marks.first().points.last(), QPointF(320, 110));
    // The visible marker is 3 * width; its painted edge must also be draggable.
    QCOMPARE(overlay->editTarget({280, 169}), QString("move"));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {280, 169});
    dragMove(overlay, {290, 179});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {290, 179});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{50, 60}, {330, 120}}));
    QTest::mouseClick(overlay, Qt::LeftButton, Qt::NoModifier, {200, 330});
    QCOMPARE(a.selected, -1);
    auto fixed = a.marks.first().json();
    QVERIFY(overlay->editTarget({280, 169}).isEmpty());
    wheel(overlay, {200, 330}, 1);
    QCOMPARE(a.marks.first().json(), fixed);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void freehandAndMarkerOnScaledPin() {
    QTemporaryDir dir;
    Store store(dir.path());
    store.config["annotation_last_tool"] = "pen";
    QImage image(640, 360, QImage::Format_ARGB32);
    image.fill(Qt::white);
    auto pin = new PinWindow(image, &store, {100, 100, 320, 180});
    pin->show(); pin->startEditing();
    auto position = pin->pos();
    auto start = pin->imagePoint({52, 52});
    auto end = pin->imagePoint({182, 122});
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, {52, 52});
    dragMove(pin, {102, 92});
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, {182, 122});
    QCOMPARE(pin->annotation->marks.first().points.first(), start);
    QCOMPARE(pin->annotation->marks.first().points.last(), end);
    QCOMPARE(pin->pos(), position);
    auto fixed = pin->annotation->marks.first().json();
    wheel(pin, {182, 122}, 1);
    QCOMPARE(pin->annotation->marks.first().json(), fixed);
    pin->chooseTool("marker");
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, {62, 62});
    dragMove(pin, {120, 130});
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, {212, 62});
    auto marker = pin->annotation->marks.last();
    QCOMPARE(marker.points.size(), 2);
    QCOMPARE(markControls(marker).size(), 2);
    QTest::mousePress(pin, Qt::LeftButton, Qt::NoModifier, {212, 62});
    QTest::mouseRelease(pin, Qt::LeftButton, Qt::NoModifier, {232, 82});
    QCOMPARE(pin->annotation->marks.last().points.first(), marker.points.first());
    QCOMPARE(pin->annotation->marks.last().points.last(), pin->imagePoint({232, 82}));
    QCOMPARE(pin->pos(), position);
    pin->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void markerOnlyTwoStylesBoxResizingAndPreferences() {
    QTemporaryDir dir;
    Store store(dir.path());
    store.config["annotation_options"] = QJsonObject{{"marker", QJsonObject{
        {"area_shape", "ellipse"}, {"rotation", 35}}}};
    auto overlay = editingFixture(store, "marker");
    auto area = overlay->toolbar->areaButton;
    QVERIFY(area->isVisible());
    QVERIFY(!overlay->toolbar->areaEllipseButton->isVisible());
    QCOMPARE(overlay->annotation->preferences.areaShape, QString("stroke"));
    QCOMPARE(overlay->annotation->preferences.rotation, 0);
    QTest::mouseClick(area, Qt::LeftButton);
    overlay->setPreference("color", QColor(255, 0, 4));
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {150, 150});
    dragMove(overlay, {350, 250});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {350, 250});
    auto &a = *overlay->annotation;
    QCOMPARE(a.marks.first().areaShape, QString("rect"));
    QCOMPARE(markControls(a.marks.first()).size(), 9);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {350, 200});
    dragMove(overlay, {390, 200});
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, {390, 200});
    QCOMPARE(a.marks.first().points, QList<QPointF>({{50, 70}, {290, 170}}));
    QVERIFY(a.render().pixelColor(100, 100) != a.original.pixelColor(100, 100));
    QVERIFY(savePreview(overlay, "marker-box-controls.png"));
    overlay->chooseTool("mosaic");
    QVERIFY(overlay->toolbar->areaEllipseButton->isVisible());
    overlay->chooseTool("eraser");
    QVERIFY(overlay->toolbar->areaEllipseButton->isVisible());
    overlay->chooseTool("marker");
    QVERIFY(!overlay->toolbar->areaEllipseButton->isVisible());
    QVERIFY(area->isChecked());
    QCOMPARE(a.preferences.areaShape, QString("rect"));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    Store reopened(dir.path());
    Annotation saved(QImage(100, 100, QImage::Format_ARGB32), &reopened);
    saved.chooseTool("marker");
    QCOMPARE(saved.preferences.areaShape, QString("rect"));
    QCOMPARE(saved.preferences.color, QColor(255, 0, 4));
  }
  void markerShiftAndFreehandTransformStability() {
    Mark pen;
    pen.kind = "pen"; pen.rotation = 45;
    pen.points = {{10, 20}, {40, 60}, {90, 25}};
    auto path = markPath(pen);
    QCOMPARE(path.elementAt(0).x, 10.0);
    QCOMPARE(path.elementAt(0).y, 20.0);
    pen.points.append({180, 160});
    auto extended = markPath(pen);
    QCOMPARE(extended.elementAt(1).x, path.elementAt(1).x);
    QCOMPARE(extended.elementAt(1).y, path.elementAt(1).y);
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "marker");
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, {150, 160});
    dragMove(overlay, {340, 210}, Qt::ShiftModifier);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::ShiftModifier, {340, 210});
    auto first = overlay->annotation->marks.first().points;
    QCOMPARE(first.first().y(), first.last().y());
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier,
                      first.last().toPoint() + QPoint(100, 80));
    dragMove(overlay, {330, 270}, Qt::ShiftModifier);
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::ShiftModifier, {330, 270});
    auto delta = overlay->annotation->marks.first().points.last() - first.first();
    QVERIFY(qAbs(delta.x() - delta.y()) < .01);
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void captureColorFormatPersistsAcrossCapturesAndRestart() {
    QTemporaryDir dir;
    auto savedClipboard = std::make_unique<QMimeData>();
    if (auto mime = QApplication::clipboard()->mimeData())
      for (const auto &format : mime->formats())
        savedClipboard->setData(format, mime->data(format));
    auto restoreClipboard = qScopeGuard([&] {
      QApplication::clipboard()->setMimeData(savedClipboard.release());
    });
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 240, 180};
    QImage image(desktop.bounds.size(), QImage::Format_RGB32);
    image.fill(QColor(18, 52, 86));
    desktop.frames = {{desktop.bounds, image}};
    auto copyColor = [](CaptureOverlay &capture) {
      QMouseEvent move(QEvent::MouseMove, QPointF(40, 40),
                       QPointF(capture.mapToGlobal(QPoint(40, 40))),
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
      QApplication::sendEvent(&capture, &move);
      QTest::keyClick(&capture, Qt::Key_C);
      return QApplication::clipboard()->text();
    };
    {
      Store store(dir.path());
      CaptureOverlay first(&store, false, desktop);
      QCOMPARE(copyColor(first), QString("18, 52, 86"));
      QTest::keyClick(&first, Qt::Key_Shift);
      QCOMPARE(copyColor(first), QString("#123456"));
      QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, {}, true);
      QApplication::sendEvent(&first, &repeat);
      QCOMPARE(copyColor(first), QString("#123456"));
      CaptureOverlay second(&store, false, desktop);
      QCOMPARE(copyColor(second), QString("#123456"));
    }
    {
      Store store(dir.path());
      CaptureOverlay restarted(&store, false, desktop);
      QCOMPARE(copyColor(restarted), QString("#123456"));
      QTest::keyClick(&restarted, Qt::Key_Shift);
      QCOMPARE(copyColor(restarted), QString("18, 52, 86"));
    }
    Store store(dir.path());
    CaptureOverlay restarted(&store, false, desktop);
    QCOMPARE(copyColor(restarted), QString("18, 52, 86"));
  }
  void trayRestoresIndividualAndAllThroughPins() {
    QTemporaryDir dir;
    auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([previous] {
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    ApplicationController controller(true);
    auto menu = namedMenu("trayMenu");
    QVERIFY(menu);
    auto throughMenu = menu->findChild<QMenu *>("throughPinsMenu");
    auto clear = menu->findChild<QAction *>("clearPinThrough");
    QVERIFY(throughMenu); QVERIFY(clear);
    QVERIFY(!throughMenu->isEnabled()); QVERIFY(!clear->isEnabled());
    QImage image(80, 60, QImage::Format_RGB32); image.fill(Qt::red);
    auto first = controller.pins.create(image, {40, 40, 80, 60});
    image.fill(Qt::blue);
    auto second = controller.pins.create(image, {140, 40, 80, 60});
    auto normal = controller.pins.create(image, {240, 40, 80, 60});
    first->setThrough(true); second->setThrough(true);
    QVERIFY(first->windowFlags() & Qt::WindowTransparentForInput);
    if (QGuiApplication::platformName() != "offscreen")
      QVERIFY(GetWindowLongPtr(reinterpret_cast<HWND>(first->winId()), GWL_EXSTYLE) & WS_EX_TRANSPARENT);
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    QCOMPARE(throughMenu->actions().size(), 2);
    QVERIFY(throughMenu->isEnabled()); QVERIFY(clear->isEnabled());
    QAction *restoreFirst = nullptr, *restoreSecond = nullptr;
    for (auto action : throughMenu->actions()) {
      QVERIFY(!action->icon().isNull());
      if (action->data().toString() == first->id) restoreFirst = action;
      if (action->data().toString() == second->id) restoreSecond = action;
    }
    QVERIFY(restoreFirst); QVERIFY(restoreSecond);
    restoreFirst->trigger();
    QVERIFY(!first->through); QVERIFY(second->through); QVERIFY(!normal->through);
    QVERIFY(!(first->windowFlags() & Qt::WindowTransparentForInput));
    if (QGuiApplication::platformName() != "offscreen")
      QVERIFY(!(GetWindowLongPtr(reinterpret_cast<HWND>(first->winId()), GWL_EXSTYLE) & WS_EX_TRANSPARENT));
    // A menu action may outlive a pin closed while the menu is open.
    controller.pins.handle(second, "destroy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    restoreSecond->trigger();
    first->setThrough(true); normal->setThrough(true);
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    QCOMPARE(throughMenu->actions().size(), 2);
    clear->trigger();
    QVERIFY(!first->through); QVERIFY(!normal->through);
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    QVERIFY(throughMenu->actions().isEmpty());
    QVERIFY(!throughMenu->isEnabled()); QVERIFY(!clear->isEnabled());
  }
  void pinBackgroundOptionsOnlyForTransparentImages() {
    QTemporaryDir dir;
    Store store(dir.path());
    QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::red);
    PinWindow pin(image, &store, {40, 40, 120, 80});
    auto inspectMenu = [&](bool expected, const QString &select = {}) {
      QTimer::singleShot(0, [&] {
        auto menu = namedMenu("pinMenu");
        QVERIFY(menu);
        auto close = qScopeGuard([&] { menu->close(); });
        auto bg = menu->findChild<QMenu *>("pinBackgroundMenu");
        QCOMPARE(bool(bg), expected);
        if (!bg) return;
        QList<QAction *> choices;
        for (auto action : bg->actions())
          if (action->isCheckable()) choices.append(action);
        QCOMPARE(choices.size(), 3);
        QStringList values;
        int checked = 0;
        for (auto action : choices) {
          values.append(action->data().toString());
          if (action->isChecked()) ++checked;
        }
        QCOMPARE(values, (QStringList{"透明", "暗色棋盘", "亮色棋盘"}));
        QCOMPARE(checked, 1);
        if (!select.isEmpty()) {
          for (auto action : choices)
            if (action->data().toString() == select) action->trigger();
          QCOMPARE(pin.background, select);
          for (auto action : choices)
            QCOMPARE(action->isChecked(), action->data().toString() == select);
        }
      });
      QContextMenuEvent event(QContextMenuEvent::Mouse, {30, 30}, pin.mapToGlobal(QPoint(30, 30)));
      QApplication::sendEvent(&pin, &event);
    };
    inspectMenu(false); // An alpha channel alone must not show the options.
    pin.setAlpha(.5);
    inspectMenu(false); // Whole-window opacity is independent of image alpha.
    pin.setAlpha(1);
    image.fill(Qt::transparent); image.setPixelColor(60, 40, Qt::red);
    pin.image = image;
    inspectMenu(true, "亮色棋盘");
    auto rendered = pin.grab().toImage();
    QCOMPARE(rendered.pixelColor(PinWindow::Margin + 30, PinWindow::Margin + 30), QColor("#dddddd"));
    QCOMPARE(rendered.pixelColor(PinWindow::Margin + 60, PinWindow::Margin + 40), QColor(Qt::red));
    QCOMPARE(pin.displayedImage(), image);
    inspectMenu(true, "透明");
    QCOMPARE(pin.grab().toImage().pixelColor(PinWindow::Margin + 30, PinWindow::Margin + 30).alpha(), 0);
    QCOMPARE(pin.displayedImage(), image);
    PinManager manager(&store);
    auto legacy = manager.create(image, {}, {{"background", "伪透明"}});
    QCOMPARE(legacy->background, QString("透明"));
  }
  void trayShortcutHintsFollowSettings() {
    QTemporaryDir dir;
    auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([previous] {
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    ApplicationController controller(true);
    auto menu = namedMenu("trayMenu");
    QVERIFY(menu);
    QMap<QString, QAction *> actions;
    for (auto action : menu->actions()) {
      auto key = action->property("hotkey_action").toString();
      if (!key.isEmpty()) actions[key] = action;
      QVERIFY(action->shortcut().isEmpty());
    }
    QCOMPARE(actions["capture"]->text(), QString("截图\tF1"));
    QCOMPARE(actions["pin"]->text(), QString("贴图\tF3"));
    QCOMPARE(actions["toggle_pins"]->text(), QString("显示 / 隐藏贴图\tShift+F3"));
    QVERIFY(!actions.contains("record"));
    bool recordMenu=false; for(auto action:menu->actions()) recordMenu|=action->text()=="区域录屏";
    QVERIFY(recordMenu);
    QVERIFY(!actions["pause"]->isEnabled());
    auto keys = controller.store.config["hotkeys"].toObject();
    keys["capture"] = "Ctrl+Shift+C";
    keys["pin"] = "Alt+F3";
    keys["through"] = "Ctrl+Shift+T";
    controller.store.config["hotkeys"] = keys;
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    QCOMPARE(actions["capture"]->text(), QString("截图\tCtrl+Shift+C"));
    QCOMPARE(actions["pin"]->text(), QString("贴图\tAlt+F3"));
    QCOMPARE(actions["through"]->text(), QString("切换鼠标穿透\tCtrl+Shift+T"));
    menu->adjustSize();
    QVERIFY(savePreview(menu, "tray-shortcut-menu.png"));
    keys["pin"] = "";
    controller.store.config["hotkeys"] = keys;
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    QCOMPARE(actions["pin"]->text(), QString("贴图"));
  }
  void annotationAndPinMenusDisplayRealShortcuts() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto overlay = editingFixture(store, "pen");
    QStringList editingLabels, toolLabels;
    bool saved = false;
    QTimer::singleShot(0, [&] {
      if (auto menu = namedMenu("annotationMenu")) {
        for (auto action : menu->actions()) {
          editingLabels << action->text();
          if (action->menu() && action->text() == "标注工具")
            for (auto tool : action->menu()->actions()) toolLabels << tool->text();
        }
        saved = savePreview(menu, "annotation-shortcut-menu.png");
        menu->close();
      }
    });
    overlay->showMore();
    QVERIFY(saved);
    QVERIFY(editingLabels.contains("撤销\tCtrl+Z"));
    QVERIFY(editingLabels.contains("重做\tCtrl+Y"));
    QVERIFY(toolLabels.contains("画笔\tCtrl+3"));
    QVERIFY(toolLabels.contains("马克笔\tCtrl+4"));
    QVERIFY(toolLabels.contains("序号\tCtrl+8"));
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QImage image(300, 180, QImage::Format_ARGB32);
    image.fill(Qt::white);
    auto pin = new PinWindow(image, &store);
    QStringList pinLabels;
    saved = false;
    QTimer::singleShot(0, [&] {
      if (auto menu = namedMenu("pinMenu")) {
        for (auto action : menu->actions()) pinLabels << action->text();
        saved = savePreview(menu, "pin-shortcut-menu.png");
        menu->close();
      }
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, {30, 30}, pin->mapToGlobal(QPoint(30, 30)));
    QApplication::sendEvent(pin, &event);
    QVERIFY(saved);
    QVERIFY(pinLabels.contains("标注\tSpace"));
    QVERIFY(pinLabels.contains("复制\tCtrl+C"));
    QVERIFY(pinLabels.contains("鼠标穿透\tCtrl+Alt+T"));
    QVERIFY(pinLabels.contains("销毁\tShift+Esc"));
    pin->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void arrowSilhouettesAndHit() {
    QImage image(400, 200, QImage::Format_ARGB32);
    Mark m;
    m.kind = "arrow";
    m.width = 9;
    m.points = {{30, 80}, {320, 80}};
    for (auto head : QStringList{"tapered", "filled", "hollow"}) {
      image.fill(Qt::white);
      m.arrowHead = head;
      paintMark(image, m);
      if (head == "hollow") {
        QCOMPARE(image.pixelColor(180, 80), QColor(Qt::white));
        QVERIFY(!paintedHit(m, {180, 80}));
      } else {
        QVERIFY(image.pixelColor(180, 80) != QColor(Qt::white));
        QVERIFY(paintedHit(m, {180, 80}));
      }
      QCOMPARE(markControls(m).size(), 2);
    }
  }
  void hollowRectangleAndNineControls() {
    Mark m;
    m.kind = "rect";
    m.points = {{20, 20}, {200, 160}};
    QVERIFY(!paintedHit(m, {100, 80}));
    QVERIFY(paintedHit(m, {20, 80}));
    QCOMPARE(markControls(m).size(), 9);
    m.rotation = 30;
    QCOMPARE(markControls(m).size(), 9);
  }
  void recentWidthConfirmationAndUndo() {
    QImage image(400, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    Annotation a(image);
    a.chooseTool("line");
    Mark m = a.preferences;
    m.points = {{30, 80}, {300, 80}};
    a.add(m);
    a.adjustWidth(2);
    QCOMPARE(a.marks.first().width, 6.0);
    a.confirm();
    a.adjustWidth(2);
    QCOMPARE(a.marks.first().width, 6.0);
    a.undo();
    QCOMPARE(a.marks.first().width, 4.0);
    a.undo();
    QVERIFY(a.marks.isEmpty());
    a.redo();
    QCOMPARE(a.marks.size(), 1);
  }
  void snapshotAndOriginalPosition() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {-800, 0, 1600, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    desktop.frames = {{QRect(-800, 0, 800, 600), image},
                      {QRect(0, 0, 800, 600), image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({900, 80, 480, 300});
    QCOMPARE(overlay->globalSelection(), QRect(100, 80, 480, 300));
    QImage exported;
    QRect region;
    overlay->output = [&](const QImage &value, const QRect &r,
                          const QString &) {
      exported = value;
      region = r;
      return true;
    };
    QSignalSpy dead(overlay, &QObject::destroyed);
    QVERIFY(overlay->complete("pin"));
    QVERIFY(overlay->desktop.frames.isEmpty());
    QVERIFY(!overlay->annotation);
    QCOMPARE(exported.size(), QSize(480, 300));
    QCOMPARE(region, QRect(100, 80, 480, 300));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(dead.size(), 1);
    PinManager pins(&store);
    auto pin = pins.create(exported, region);
    QCOMPARE(pin->contentGeometry(), region);
    QCOMPARE(pin->image.size(), exported.size());
    pins.handle(pin, "destroy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void cancelledSaveRetainsPixels() {
    QTemporaryDir dir;
    Store store(dir.path());
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 800, 600};
    QImage image(800, 600, QImage::Format_ARGB32);
    image.fill(Qt::white);
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&store, false, desktop);
    overlay->selectRegion({100, 80, 480, 300});
    overlay->output = [](const QImage &, const QRect &, const QString &) {
      return false;
    };
    QVERIFY(!overlay->complete("save"));
    QVERIFY(overlay->annotation);
    QVERIFY(!overlay->desktop.frames.isEmpty());
    overlay->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void asyncHistoryFlushAndFailure() {
    QTemporaryDir dir;
    QImage image(240, 180, QImage::Format_ARGB32);
    image.fill(Qt::red);
    {
      Store store(dir.path());
      auto release = std::make_shared<std::promise<void>>();
      auto ready = release->get_future().share();
      store.submitIo([ready] { ready.wait(); return QString(); });
      store.addImageAsync(image);
      QCOMPARE(store.history.size(), 0);
      release->set_value();
      store.flushIo();
      QCOMPARE(store.history.size(), 1);
      QCOMPARE(QImage(store.history.first().toObject()["path"].toString()), image);
      image.fill(Qt::blue);
      store.addImageAsync(image);
      // Destruction flushes even after the GUI event loop stops.
    }
    Store restored(dir.path());
    QCOMPARE(restored.history.size(), 2);
    QCOMPARE(QImage(restored.history.first().toObject()["path"].toString()), image);
    QTemporaryDir failedDir;
    Store failed(failedDir.path());
    QVERIFY(QDir().rmdir(failed.path("history")));
    QFile obstruction(failed.path("history"));
    QVERIFY(obstruction.open(QIODevice::WriteOnly));
    obstruction.close();
    QSignalSpy errors(&failed, &Store::ioFailed);
    failed.addImageAsync(image);
    failed.flushIo();
    QCOMPARE(errors.size(), 1);
    QVERIFY(failed.history.isEmpty());
    QVERIFY(failed.readJson(failed.path("history.json"), QJsonArray()).toArray().isEmpty());
  }
  void asyncOutputReservationsAndIndexFailure() {
    QTemporaryDir dir;
    Store store(dir.path());
    auto output = dir.path() + "/自动保存";
    auto first = store.reserveOutputPath(output, "截图", "png");
    auto second = store.reserveOutputPath(output, "截图", "png");
    QVERIFY(first != second);
    QImage red(240,180,QImage::Format_ARGB32); red.fill(Qt::red);
    QImage blue=red; blue.fill(Qt::blue);
    store.addImageAsync(red,first); store.addImageAsync(blue,second);
    store.flushIo();
    QCOMPARE(QImage(first),red); QCOMPARE(QImage(second),blue);
    QCOMPARE(store.history.size(),2);
    QCOMPARE(store.history.first().toObject()["output"].toString(),second);
    QTemporaryDir failedDir;
    Store failed(failedDir.path());
    QVERIFY(QDir().mkpath(failed.path("history.json")));
    QSignalSpy errors(&failed,&Store::ioFailed);
    failed.addImageAsync(red); failed.flushIo();
    QCOMPARE(errors.size(),1); QVERIFY(failed.history.isEmpty());
    QVERIFY(QDir(failed.path("history")).entryList(QDir::Files).isEmpty());
  }
  void pinMetadataDoesNotRewritePixels() {
    QTemporaryDir dir;
    Store store(dir.path());
    QImage image(240,180,QImage::Format_ARGB32); image.fill(Qt::red);
    PinManager pins(&store);
    auto pin = pins.create(image, {100,100,240,180});
    pins.persist(); store.flushIo();
    auto file = store.path("pins/" + pin->id + ".png");
    QFile saved(file);
    QVERIFY(saved.open(QIODevice::ReadWrite));
    auto stamp = QDateTime::fromString("2000-01-01T00:00:00Z", Qt::ISODate);
    QVERIFY(saved.setFileTime(stamp, QFileDevice::FileModificationTime));
    saved.close();
    pin->move(180,140); pin->setAlpha(.6); pin->setScale(.8);
    pins.persist(); store.flushIo();
    QCOMPARE(QFileInfo(file).lastModified().toMSecsSinceEpoch(), stamp.toMSecsSinceEpoch());
    QCOMPARE(QImage(file), image);
    auto layout = store.workspace().first().toObject();
    QCOMPARE(layout["x"].toInt(), 180);
    QCOMPARE(layout["alpha"].toDouble(), .6);
    pin->startEditing();
    Mark mark; mark.kind="rect"; mark.points={{20,20},{160,120}};
    pin->annotation->add(mark); pin->endEditing();
    auto annotated = pin->image;
    pins.persist(); store.flushIo();
    QCOMPARE(QImage(file), annotated);
    QVERIFY(QFileInfo(file).lastModified().toMSecsSinceEpoch() != stamp.toMSecsSinceEpoch());
    pins.handle(pin,"destroy"); pins.persist(); store.flushIo();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
  void queuedPinDestroyAndShutdown() {
    QTemporaryDir dir;
    Store store(dir.path());
    QImage image(240,180,QImage::Format_ARGB32); image.fill(Qt::green);
    {
      PinManager pins(&store);
      auto release=std::make_shared<std::promise<void>>();
      auto ready=release->get_future().share();
      store.submitIo([ready] { ready.wait(); return QString(); });
      auto pin=pins.create(image,{100,100,240,180});
      auto file=store.path("pins/"+pin->id+".png");
      pins.persist();
      pins.handle(pin,"destroy"); pins.persist();
      release->set_value(); store.flushIo();
      QVERIFY(!QFile::exists(file)); QVERIFY(store.workspace().isEmpty());
      QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
      pins.create(image,{120,140,240,180});
    }
    QCOMPARE(store.workspace().size(),1);
    auto id=store.workspace().first().toObject()["id"].toString();
    QCOMPARE(QImage(store.path("pins/"+id+".png")), image);
    PinManager restored(&store); restored.restore();
    QCOMPARE(restored.windows.size(),1);
    QCOMPARE(restored.windows.first()->contentGeometry(),QRect(120,140,240,180));
    restored.handle(restored.windows.first(),"destroy");
    restored.persist(); store.flushIo();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
  void failedPinSaveKeepsVisibleImage() {
    QTemporaryDir dir;
    Store store(dir.path());
    QVERIFY(QDir().rmdir(store.path("pins")));
    QFile obstruction(store.path("pins"));
    QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.close();
    QImage image(240,180,QImage::Format_ARGB32); image.fill(Qt::yellow);
    PinManager pins(&store);
    auto pin=pins.create(image,{120,140,240,180});
    QSignalSpy errors(&store,&Store::ioFailed);
    pins.persist(); store.flushIo();
    QCOMPARE(errors.size(),1);
    QCOMPARE(pin->image,image); QVERIFY(pin->isVisible());
    QCOMPARE(pin->contentGeometry(),QRect(120,140,240,180));
    QVERIFY(store.workspace().isEmpty());
    pins.handle(pin,"destroy"); pins.persist(); store.flushIo();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  }
  void clipboardPinVisibleAfterHideAll_data() {
    QTest::addColumn<bool>("copyAsFile");
    QTest::newRow("image") << false;
    QTest::newRow("image-and-file") << true;
  }
  void clipboardPinVisibleAfterHideAll() {
    if (QGuiApplication::platformName() != "offscreen")
      QSKIP("Uses the private offscreen clipboard to preserve the system clipboard");
    QTemporaryDir dir; auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([previous] {
      QApplication::clipboard()->clear();
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    QFETCH(bool, copyAsFile);
    ApplicationController controller(true);
    controller.store.config["copy_image_as_file"] = copyAsFile;
    QImage image(240, 180, QImage::Format_ARGB32); image.fill(Qt::red);
    auto old = controller.pins.create(image);
    controller.pins.toggleAll(); QVERIFY(!old->isVisible());
    image.fill(Qt::green);
    QVERIFY(controller.imageOutput(image, {100, 100, 240, 180}, "copy"));
    MSG message{}; message.message = WM_HOTKEY; message.wParam = 2;
    QVERIFY(controller.nativeEventFilter({}, &message, nullptr));
    QCOMPARE(controller.pins.windows.size(), 2);
    auto added = controller.pins.windows.last();
    QCOMPARE(added->image, image); QVERIFY(added->isVisible());
    QVERIFY(!old->isVisible());
    controller.pins.toggleAll();
    QVERIFY(old->isVisible()); QVERIFY(added->isVisible());
  }
  void clipboardImageReadRecovery_data() {
    QTest::addColumn<int>("failures");
    QTest::addColumn<bool>("fileFallback");
    QTest::addColumn<bool>("success");
    QTest::newRow("delayed-image") << 1 << false << true;
    QTest::newRow("image-unavailable-valid-file") << 100 << true << true;
    QTest::newRow("image-stays-unavailable") << 100 << false << false;
  }
  void clipboardImageReadRecovery() {
    if (QGuiApplication::platformName() != "offscreen")
      QSKIP("Uses the private offscreen clipboard to preserve the system clipboard");
    QFETCH(int, failures); QFETCH(bool, fileFallback); QFETCH(bool, success);
    QTemporaryDir dir; auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([previous] {
      QApplication::clipboard()->clear();
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    ApplicationController controller(true);
    QImage image(240, 180, QImage::Format_ARGB32); image.fill(Qt::cyan);
    auto closed = controller.pins.create(image);
    controller.pins.handle(closed, "close");
    auto mime = new DelayedClipboardImage(image, failures);
    if (fileFallback) {
      const auto file = dir.path() + "/clipboard.png";
      QVERIFY(image.save(file));
      mime->setUrls({QUrl::fromLocalFile(file)});
    }
    QApplication::clipboard()->setMimeData(mime);
    controller.pinClipboard();
    if (success) {
      QTRY_COMPARE_WITH_TIMEOUT(controller.pins.windows.size(), 2, 1500);
      QCOMPARE(controller.pins.windows.last()->image, image);
      QVERIFY(controller.pins.windows.last()->isVisible());
      const int reads = mime->reads;
      QTest::qWait(900);
      QCOMPARE(mime->reads, reads); // No polling once a pin was created.
    } else {
      QTest::qWait(1000);
      QCOMPARE(controller.pins.windows.size(), 1);
      QVERIFY(mime->reads > 1 && mime->reads <= 5);
    }
    QVERIFY(!closed->isVisible()); // A read failure must not restore an old pin.
    QCOMPARE(closed->state, QString("closed"));
  }
  void clipboardPinRetryTracksLatestRequest() {
    if (QGuiApplication::platformName() != "offscreen")
      QSKIP("Uses the private offscreen clipboard to preserve the system clipboard");
    QTemporaryDir dir; auto previous = qgetenv("FLOATSHOT_DATA_DIR");
    auto restore = qScopeGuard([previous] {
      QApplication::clipboard()->clear();
      if (previous.isNull()) qunsetenv("FLOATSHOT_DATA_DIR");
      else qputenv("FLOATSHOT_DATA_DIR", previous);
    });
    qputenv("FLOATSHOT_DATA_DIR", dir.path().toUtf8());
    ApplicationController controller(true);
    QImage image(240, 180, QImage::Format_ARGB32); image.fill(Qt::yellow);
    auto mime = new DelayedClipboardImage(image, 100);
    QApplication::clipboard()->setMimeData(mime);
    controller.pinClipboard(); controller.pinClipboard();
    mime->failures = 0;
    QTRY_COMPARE_WITH_TIMEOUT(controller.pins.windows.size(), 1, 1500);
    QTest::qWait(900); QCOMPARE(controller.pins.windows.size(), 1);
    QCOMPARE(controller.pins.windows.first()->image, image);
    QApplication::clipboard()->setMimeData(new DelayedClipboardImage(image, 100));
    controller.pinClipboard();
    QApplication::clipboard()->setText("New clipboard content");
    QTest::qWait(1000); QCOMPARE(controller.pins.windows.size(), 1);
    controller.pinClipboard(); QCOMPARE(controller.pins.windows.size(), 2);
    QApplication::clipboard()->clear();
    controller.pins.handle(controller.pins.windows.last(), "close");
    controller.pinClipboard(); // An actually empty clipboard still restores closed pins.
    QVERIFY(controller.pins.windows.last()->isVisible());
  }
  void pinPersistence() {
    QTemporaryDir dir;
    Store store(dir.path());
    QImage image(240, 180, QImage::Format_ARGB32);
    image.fill(Qt::red);
    {
      PinManager pins(&store);
      auto pin = pins.create(image, {100, 100, 240, 180});
      pin->setAlpha(.7);
      pins.persist();
      pins.handle(pin, "destroy");
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    image.save(store.path("pins/sample.png"));
    store.saveWorkspace(QJsonArray{QJsonObject{{"id", "sample"},
                                               {"x", 88},
                                               {"y", 88},
                                               {"frame_margin", 12},
                                               {"scale", 1.0},
                                               {"alpha", .7}}});
    PinManager fresh(&store);
    fresh.restore();
    QCOMPARE(fresh.windows.size(), 1);
    auto pin = fresh.windows.first();
    QCOMPARE(pin->contentGeometry(), QRect(100, 100, 240, 180));
    QCOMPARE(pin->alpha, .7);
    fresh.handle(pin, "destroy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  void pauseClock() {
    ActiveClock clock;
    clock.start();
    QTest::qWait(35);
    clock.pause();
    auto paused = clock.elapsed();
    QTest::qWait(40);
    QVERIFY(qAbs(clock.elapsed() - paused) < .005);
    clock.resume();
    QTest::qWait(35);
    clock.finish();
    QVERIFY(clock.elapsed() >= paused + .02);
    auto final = clock.elapsed();
    QTest::qWait(20);
    QCOMPARE(clock.elapsed(), final);
  }
  void ffmpegRecordingAndPause() {
    if (!QFile::exists(Recorder::ffmpegPath()))
      QSKIP("Set FLOATSHOT_FFMPEG to an FFmpeg executable");
    QTemporaryDir dir;
    Recorder recorder(dir.path() + "/recording");
    RecordOptions options;
    options.region = {0, 0, 160, 90};
    options.output = dir.path() + "/sample.mp4";
    QSignalSpy started(&recorder, &Recorder::started),
        finished(&recorder, &Recorder::finished),
        failed(&recorder, &Recorder::failed);
    QImage frame(160, 90, QImage::Format_RGB32);
    frame.fill(Qt::green);
    recorder.start(options, [frame] { return frame; });
    QVERIFY(started.wait(10000));
    QTest::qWait(220);
    recorder.pause();
    auto paused = recorder.elapsed();
    QTest::qWait(120);
    QVERIFY(qAbs(recorder.elapsed() - paused) < .005);
    recorder.pause();
    QTest::qWait(220);
    recorder.stop();
    QVERIFY(finished.wait(15000));
    QCOMPARE(failed.size(), 0);
    QVERIFY(QFileInfo(options.output).size() > 100);
    QProcess decode;
    decode.start(Recorder::ffmpegPath(),
                 {"-v", "error", "-i", options.output, "-f", "rawvideo",
                  "-pix_fmt", "rgb24", "pipe:1"});
    QVERIFY(decode.waitForFinished(10000));
    QCOMPARE(decode.exitCode(), 0);
    QVERIFY(decode.readAllStandardOutput().size() >= 160 * 90 * 3);
    QLibrary library(QCoreApplication::applicationDirPath() +
                     "/NativeVideoPreview.dll");
    using Verify = QJsonObject (*)(const QString &);
    auto verify =
        reinterpret_cast<Verify>(library.resolve("verifyVideoPlayback"));
    QVERIFY2(verify, qPrintable(library.errorString()));
    auto playback = verify(options.output);
    QVERIFY2(playback["passed"].toBool(),
             qPrintable(playback["error"].toString()));
    // Reusing one recorder must create a fresh recording timeline.
    options.output = dir.path() + "/second.mp4";
    started.clear();
    finished.clear();
    recorder.start(options, [frame] { return frame; });
    QVERIFY(started.wait(10000));
    QVERIFY(recorder.elapsed() < .1);
    QTest::qWait(160);
    recorder.stop();
    QVERIFY(finished.wait(15000));
    QCOMPARE(failed.size(), 0);
  }
  void wasapiLoopbackRecording() {
    if (!QFile::exists(Recorder::ffmpegPath()))
      QSKIP("FFmpeg unavailable");
    if (audioDevices(false).isEmpty())
      QSKIP("No Windows output audio device");
    QTemporaryDir dir;
    Recorder recorder(dir.path() + "/recording");
    RecordOptions options;
    options.region = {0, 0, 160, 90};
    options.output = dir.path() + "/loopback.mp4";
    options.systemAudio = true;
    QSignalSpy started(&recorder, &Recorder::started),
        finished(&recorder, &Recorder::finished),
        failed(&recorder, &Recorder::failed);
    QImage frame(160, 90, QImage::Format_RGB32);
    frame.fill(Qt::blue);
    recorder.start(options, [frame] { return frame; });
    QVERIFY2(started.wait(10000),
             failed.isEmpty() ? "Audio start timed out"
                              : qPrintable(failed.first().first().toString()));
    recorder.setMuted(true, true);
    QTest::qWait(180);
    recorder.pause();
    QTest::qWait(80);
    recorder.pause();
    QTest::qWait(180);
    recorder.stop();
    QVERIFY2(finished.wait(15000),
             failed.isEmpty() ? "Audio save timed out"
                              : qPrintable(failed.first().first().toString()));
    QCOMPARE(failed.size(), 0);
    QProcess decode;
    decode.start(Recorder::ffmpegPath(),
                 {"-v", "error", "-i", options.output, "-map", "0:a:0", "-f",
                  "s16le", "pipe:1"});
    QVERIFY(decode.waitForFinished(10000));
    QCOMPARE(decode.exitCode(), 0);
    QVERIFY(decode.readAllStandardOutput().size() > 1000);
  }
};
QTEST_MAIN(NativeTests)
#include "tests.moc"
