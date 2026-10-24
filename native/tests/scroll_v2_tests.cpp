#include "scroll_capture.h"
#include "scroll_grabber.h"
#include "native_windows.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QScopeGuard>
#include <QtCore/QSet>
#include <QtGui/QPainter>
#include <QtGui/QFontDatabase>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <dwmapi.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

namespace {
struct DynamicObject { QRect documentRect; int seed; };

// The rectangles are fixture truth, never passed to the stitcher. Objects at
// different document positions produce independent repaint regions.
const QList<DynamicObject> dynamicObjects={
  {{55,305,26,26},0},{{435,525,26,26},1},{{205,715,26,26},2},
  {{550,1095,26,26},3},{{84,1435,26,26},4},{{450,1775,26,26},5},
  {{250,2085,26,26},6},{{535,2505,26,26},7}};

QImage animatedFrame(const QImage &document,int at,int phase) {
  QImage frame=document.copy(0,at,document.width(),720);
  QPainter p(&frame);
  p.setRenderHint(QPainter::Antialiasing);
  for(const auto &object:dynamicObjects) {
    QRect r=object.documentRect.translated(0,-at);
    if(!r.intersects(frame.rect())) continue;
    p.fillRect(r,QColor("#fafafa"));
    const QColor ink=object.seed%2?QColor("#236b77"):QColor("#ad452b");
    p.setPen(QPen(ink,3,Qt::SolidLine,Qt::RoundCap));
    p.drawArc(r.adjusted(3,3,-3,-3),((phase*47+object.seed*31)%360)*16,225*16);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e9b421"));
    const double angle=(phase*0.8+object.seed)*3.141592653589793/3.0;
    p.drawEllipse(QPointF(r.center())+QPointF(std::cos(angle)*7,std::sin(angle)*7),2.5,2.5);
  }
  return frame;
}

bool isDynamicPixel(int x,int y) {
  for(const auto &object:dynamicObjects) if(object.documentRect.contains(x,y)) return true;
  return false;
}

int percentile(QList<qint64> values,int percentage) {
  if(values.isEmpty()) return 0;
  std::sort(values.begin(),values.end());
  return int(values[(values.size()-1)*percentage/100]);
}

qint64 fileTime100ns(const FILETIME &value) {
  return (qint64(value.dwHighDateTime)<<32)|value.dwLowDateTime;
}

qint64 processCpu100ns() {
  FILETIME created{},exited{},kernel{},user{};
  return GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)
      ? fileTime100ns(kernel)+fileTime100ns(user):0;
}

qint64 workingSetBytes() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb=sizeof(counters);
  return K32GetProcessMemoryInfo(GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),sizeof(counters))
      ? qint64(counters.WorkingSetSize):0;
}

qint64 monotonicNanoseconds() {
  using Clock=std::chrono::steady_clock;
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
}

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
  void scatteredAnimations_data() {
    QTest::addColumn<QList<int>>("positions");
    QTest::newRow("normal")<<QList<int>{0,73,196,353,497,622,801,1003,1187,1350,1594,1821,2060,2340,2640};
    QTest::newRow("large-steps")<<QList<int>{0,317,620,920,1200,1500,1800,2080,2360,2640};
  }
  void scatteredAnimations() {
    QFETCH(QList<int>,positions);
    const auto doc=document("paragraphs",720,3500);
    ScrollStitcherV2 engine;
    QList<QPair<int,QImage>> sources;
    int phase=0;
    for(int at:positions) {
      auto frame=animatedFrame(doc,at,phase++);
      sources.append({at,frame});
      const auto result=engine.accept(frame,1);
      QVERIFY2(good(result),qPrintable(QString("animation at %1 rejected: %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at);
      QCOMPARE(engine.size().height(),at+720);
    }
    const auto committed=engine.image();
    QCOMPARE(committed.size(),QSize(720,3360));
    for(int y=0;y<committed.height();++y) for(int x=0;x<committed.width();++x)
      if(!isDynamicPixel(x,y)) {
        QVERIFY2(committed.pixel(x,y)==doc.pixel(x,y),
          qPrintable(QString("static text changed at (%1,%2)").arg(x).arg(y)));
      }
    for(const auto &object:dynamicObjects) {
      const auto actual=committed.copy(object.documentRect);
      bool singleSource=false;
      for(const auto &[at,frame]:sources) {
        const auto inFrame=object.documentRect.translated(0,-at);
        if(frame.rect().contains(inFrame) && frame.copy(inFrame)==actual) { singleSource=true; break; }
      }
      QVERIFY2(singleSource,qPrintable(QString("animation %1 has no complete source frame").arg(object.seed)));
    }
    const auto revision=engine.contentRevision();
    for(int at:{1200,317,0,1800,2640}) {
      const auto result=engine.accept(animatedFrame(doc,at,phase++),-1);
      QVERIFY2(good(result),qPrintable(QString("animated revisit %1 returned %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at);
      QCOMPARE(engine.contentRevision(),revision);
      QCOMPARE(engine.image(),committed);
    }
  }
  void stationaryAnimationDoesNotExtend() {
    const auto doc=document("paragraphs",720,3500);
    ScrollStitcherV2 engine;
    const auto first=animatedFrame(doc,0,0);
    QCOMPARE(engine.accept(first),ScrollStitcher::Started);
    const auto revision=engine.contentRevision();
    for(int phase=1;phase<=12;++phase) {
      const auto result=engine.accept(animatedFrame(doc,0,phase));
      QVERIFY2(good(result),qPrintable(QString("stationary animation phase %1 rejected: %2").arg(phase).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),0);
      QCOMPARE(engine.size(),QSize(720,720));
      QCOMPARE(engine.contentRevision(),revision);
    }
    QCOMPARE(engine.image(),first);
    const auto next=animatedFrame(doc,220,13);
    QVERIFY(good(engine.accept(next,1)));
    QCOMPARE(engine.viewportPosition(),220);
    QCOMPARE(engine.image().copy(0,0,720,720),first);
  }
  void hoverCardRepaint() {
    const auto doc=document("paragraphs",720,3000);
    const QRect card(310,706,155,78);
    auto frame=[&](int at,bool hovered) {
      auto image=doc.copy(0,at,720,720); QPainter p(&image);
      const QRect visible=card.translated(0,-at);
      p.fillRect(visible,hovered?QColor("#cbe8dc"):QColor("#e4e8ee"));
      p.setPen(QColor("#253641")); p.drawText(visible.adjusted(9,8,-9,-8),Qt::AlignCenter,"Account / Summary");
      return image;
    };
    ScrollStitcherV2 engine;
    const QList<int> positions={0,75,222,415,610,815,1030,1250};
    QList<QPair<int,QImage>> sources;
    for(int index=0;index<positions.size();++index) {
      const int at=positions[index]; const auto image=frame(at,index%2);
      sources.append({at,image});
      const auto result=engine.accept(image,1);
      QVERIFY2(good(result),qPrintable(QString("hover card at %1 returned %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at);
    }
    const auto result=engine.image();
    QCOMPARE(result.size(),QSize(720,1970));
    for(int y=0;y<result.height();++y) for(int x=0;x<720;++x)
      if(!card.contains(x,y)) QVERIFY2(result.pixel(x,y)==doc.pixel(x,y),
        qPrintable(QString("hover repaint modified text at (%1,%2)").arg(x).arg(y)));
    bool completeSource=false;
    for(const auto &[at,image]:sources) {
      const auto visible=card.translated(0,-at);
      if(image.rect().contains(visible) && image.copy(visible)==result.copy(card)) completeSource=true;
    }
    QVERIFY2(completeSource,"hover card was combined from incompatible phases");
    const auto revision=engine.contentRevision();
    QVERIFY(good(engine.accept(frame(610,true),-1)));
    QCOMPARE(engine.viewportPosition(),610);
    QCOMPARE(engine.contentRevision(),revision);
    QCOMPARE(engine.image(),result);
  }
  void repeatedMessagesWithSmallDifferences() {
    QImage doc(720,5000,QImage::Format_RGB32); doc.fill("#f8fafb");
    QPainter p(&doc); p.setFont(QFont("Consolas",12));
    for(int row=0;row<34;++row) {
      const int y=row*144;
      p.fillRect(28,y+10,650,126,row%2?QColor("#e4eef5"):QColor("#e7f0e7"));
      p.setPen("#182532");
      p.drawText(48,y+42,"Repeated message: Please review the same conversation again.");
      p.drawText(48,y+90,"The body stays identical across every nearby message.");
      p.drawText(628,y+112,QString("%1").arg(row,2,10,QChar('0')));
    }
    p.end();
    ScrollStitcherV2 engine;
    for(int at:{0,72,216,432,648,936,1224,1512,1800}) {
      const auto result=engine.accept(doc.copy(0,at,720,720),1);
      QVERIFY2(good(result),qPrintable(QString("repeated messages at %1 returned %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at);
    }
    QCOMPARE(engine.image(),doc.copy(0,0,720,2520));
    const auto revision=engine.contentRevision();
    for(int at:{648,216,1512,1800}) {
      QVERIFY(good(engine.accept(doc.copy(0,at,720,720),-1)));
      QCOMPARE(engine.viewportPosition(),at);
      QCOMPARE(engine.contentRevision(),revision);
    }
    QCOMPARE(engine.image(),doc.copy(0,0,720,2520));
  }
  void repeatedChatReturnMustNotPrepend() {
    QImage doc(720,2600,QImage::Format_RGB32); doc.fill("#fafafa");
    QPainter p(&doc); p.setFont(QFont("Consolas",12));
    for(int group=0;group*300<doc.height();++group) {
      const int y=group*300;
      p.fillRect(22,y+18,52,52,QColor("#446688"));
      p.setPen(Qt::white); p.drawText(27,y+49,"same");
      p.fillRect(92,y+24,400,35,QColor("#d3edce"));
      p.setPen(Qt::black); p.drawText(104,y+48,"Repeated sticker and message");
      p.drawText(310,y+149,QString("Conversation section %1, sequence %2").arg(group).arg(group*127));
      p.drawText(310,y+209,QString("2026-10-24 13:%1, event %2").arg(group,2,10,QChar('0')).arg(group*731));
    }
    p.end(); ScrollStitcherV2 engine;
    for(int at:{0,180,360,540,720}) {
      const auto r=engine.accept(doc.copy(0,at,720,720),1);
      QVERIFY2(good(r),qPrintable(QString("initial position %1: %2").arg(at).arg(int(r))));
      QCOMPARE(engine.viewportPosition(),at);
    }
    const auto before=engine.image(); const auto revision=engine.contentRevision();
    auto returned=doc.copy(0,0,720,720);
    QPainter patch(&returned); patch.fillRect(530,240,12,12,QColor("#6688aa")); patch.end();
    const auto r=engine.accept(returned,-1);
    QVERIFY2(good(r),qPrintable(QString("return to captured origin: %1").arg(int(r))));
    QCOMPARE(engine.viewportPosition(),0);
    QCOMPARE(engine.size(),before.size());
    QCOMPARE(engine.contentRevision(),revision);
    QCOMPARE(engine.image(),before);
  }
  void userRepeatedChatReturn_data() {
    QTest::addColumn<int>("viewport");
    for(int height:{480,720,960}) QTest::newRow(qPrintable(QString::number(height)))<<height;
  }
  void userRepeatedChatReturn() {
    const auto path=qEnvironmentVariable("PICUT_SCROLL_RETURN_FIXTURE");
    if(path.isEmpty()) QSKIP("Set PICUT_SCROLL_RETURN_FIXTURE to the user's repeated chat image");
    QFETCH(int,viewport); QImage doc(path); QVERIFY(!doc.isNull());
    doc=doc.convertToFormat(QImage::Format_RGB32); ScrollStitcherV2 engine;
    int last=0;
    for(int at=0;at<doc.height()-viewport;at+=53) {
      const auto r=engine.accept(doc.copy(0,at,doc.width(),viewport),1);
      if(good(r)) {
        QCOMPARE(engine.viewportPosition(),at); last=at;
        QCOMPARE(engine.image(),doc.copy(0,0,doc.width(),last+viewport));
      }
    }
    QVERIFY(last>viewport*2);
    const auto before=engine.image(); const auto revision=engine.contentRevision();
    for(int at=last;at>=0;at=qMax(0,at-71)) {
      const auto r=engine.accept(doc.copy(0,at,doc.width(),viewport),-1);
      if(good(r)) QCOMPARE(engine.viewportPosition(),at);
      QCOMPARE(engine.contentRevision(),revision); QCOMPARE(engine.image(),before);
      if(!at) break;
    }
    const auto r=engine.accept(doc.copy(0,0,doc.width(),viewport),-1);
    QVERIFY(good(r)); QCOMPARE(engine.viewportPosition(),0);
    QCOMPARE(engine.image(),before);
  }
  void fractionalScrollPositions() {
    const auto doc=document("code",720,3500);
    auto frame=[&](double at) {
      const int row=int(std::floor(at));
      QImage image(720,720,QImage::Format_RGB32); image.fill(Qt::white);
      QPainter p(&image); p.setRenderHint(QPainter::SmoothPixmapTransform);
      p.drawImage(QPointF(0,-(at-row)),doc.copy(0,row,720,722));
      return image;
    };
    const QList<double> positions={0,97.25,194.5,291.75,389.0,586.25,803.5,1007.75,1234.0,1451.25,1717.5,1989.75};
    ScrollStitcherV2 engine; QList<QPair<double,QImage>> sources;
    for(double at:positions) {
      const auto pixels=frame(at); sources.append({at,pixels});
      const auto result=engine.accept(pixels,1);
      QVERIFY2(good(result),qPrintable(QString("fractional scroll at %1 returned %2").arg(at).arg(int(result))));
      QVERIFY2(std::abs(engine.viewportPosition()-qRound(at))<=1,
        qPrintable(QString("fractional position %1 resolved to %2").arg(at).arg(engine.viewportPosition())));
    }
    const auto output=engine.image();
    QVERIFY(std::abs(output.height()-(qRound(positions.last())+720))<=1);
    QCOMPARE(output.copy(0,0,720,720),sources.first().second);
    // At each document row, exported pixels must come from a captured original
    // row. Allow one row of coordinate rounding, not a cumulative drift.
    for(int y=0;y<output.height();y+=17) {
      bool hasSource=false;
      for(const auto &[at,pixels]:sources) for(int rounding=-1;rounding<=1;++rounding) {
        const int sourceY=y-qRound(at)-rounding;
        if(sourceY>=0 && sourceY<pixels.height() &&
           std::memcmp(output.constScanLine(y),pixels.constScanLine(sourceY),size_t(720*4))==0)
          hasSource=true;
      }
      QVERIFY2(hasSource,qPrintable(QString("no original source row for document y=%1").arg(y)));
    }
    const auto revision=engine.contentRevision();
    for(double at:{803.5,389.0,1451.25,1989.75}) {
      const auto result=engine.accept(frame(at),-1);
      QVERIFY2(good(result),qPrintable(QString("fractional revisit %1 returned %2").arg(at).arg(int(result))));
      QVERIFY(std::abs(engine.viewportPosition()-qRound(at))<=1);
      QCOMPARE(engine.contentRevision(),revision);
      QCOMPARE(engine.image(),output);
    }
  }
  void realScreenshotSpinners() {
    const auto path=qEnvironmentVariable("PICUT_SCROLL_V2_REAL_FIXTURE");
    if(path.isEmpty()) QSKIP("Set PICUT_SCROLL_V2_REAL_FIXTURE to the user's 818x416 screenshot");
    QImage reference(path); QVERIFY(!reference.isNull());
    reference=reference.convertToFormat(QImage::Format_RGB32);
    QCOMPARE(reference.size(),QSize(818,416));
    const QList<QRect> spinnerRects={{540,77,18,18},{629,77,18,18},{716,77,18,18}};
    auto frame=[&](int at,int phase) {
      QImage image=reference.copy(0,at,818,280);
      QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
      for(const auto &original:spinnerRects) {
        const auto visible=original.translated(0,-at);
        if(!visible.intersects(image.rect())) continue;
        p.fillRect(visible,Qt::white);
        p.setPen(QPen(QColor("#525b60"),1.5));
        p.drawArc(visible.adjusted(3,3,-3,-3),((phase*57+original.x())%360)*16,265*16);
      }
      return image;
    };
    ScrollStitcherV2 engine;
    QList<QPair<int,QImage>> sources;
    int phase=0;
    for(int at:{0,20,45,80,110,136}) {
      const auto pixels=frame(at,phase++); sources.append({at,pixels});
      const auto result=engine.accept(pixels,1);
      QVERIFY2(good(result),qPrintable(QString("real screenshot spinner at %1 returned %2").arg(at).arg(int(result))));
      QCOMPARE(engine.viewportPosition(),at);
    }
    const auto output=engine.image();
    QCOMPARE(output.size(),reference.size());
    for(int y=0;y<416;++y) for(int x=0;x<818;++x) {
      bool spinner=false;
      for(const auto &rect:spinnerRects) spinner|=rect.contains(x,y);
      if(!spinner) QVERIFY2(output.pixel(x,y)==reference.pixel(x,y),
        qPrintable(QString("real screenshot text changed at (%1,%2)").arg(x).arg(y)));
    }
    for(const auto &rect:spinnerRects) {
      bool onePhase=false;
      for(const auto &[at,pixels]:sources) {
        const auto visible=rect.translated(0,-at);
        if(pixels.rect().contains(visible) && output.copy(rect)==pixels.copy(visible)) onePhase=true;
      }
      QVERIFY2(onePhase,qPrintable(QString("spinner at x=%1 split across phases").arg(rect.x())));
    }
    const auto revision=engine.contentRevision();
    for(int at:{80,0,110,136}) {
      const auto result=engine.accept(frame(at,phase++),-1);
      QVERIFY(good(result)); QCOMPARE(engine.viewportPosition(),at);
      QCOMPARE(engine.contentRevision(),revision); QCOMPARE(engine.image(),output);
    }
  }
  void longTrackingBudget() {
    if(qEnvironmentVariable("PICUT_SCROLL_LONG_BENCH")!="1")
      QSKIP("Set PICUT_SCROLL_LONG_BENCH=1 for the isolated 30,000-pixel budget run");
    const auto doc=document("code",1000,31000);
    const auto baselineWorkingSet=workingSetBytes();
    const auto baselineCpu=processCpu100ns();
    ScrollStitcherV2 engine;
    QList<qint64> acceptUs;
    qint64 peakWorkingSet=baselineWorkingSet;
    int firstFailure=-1, accepted=0;
    QElapsedTimer total; total.start();
    for(int at=0;at<=30000;at=qMin(30000,at+397)) {
      const auto image=doc.copy(0,at,1000,800);
      QElapsedTimer operation; operation.start();
      const auto result=engine.accept(image,1);
      acceptUs.append(operation.nsecsElapsed()/1000);
      peakWorkingSet=qMax(peakWorkingSet,workingSetBytes());
      if(!good(result) || engine.viewportPosition()!=at) {
        firstFailure=at; break;
      }
      ++accepted;
      if(at==30000) break;
    }
    const qint64 wallMs=total.elapsed();
    const qint64 cpuMs=(processCpu100ns()-baselineCpu)/10000;
    const QList<qint64> early=acceptUs.mid(1,qMin(20,acceptUs.size()-1));
    const QList<qint64> late=acceptUs.mid(qMax(1,acceptUs.size()-20));
    const double earlyP95Ms=percentile(early,95)/1000.0;
    const double lateP95Ms=percentile(late,95)/1000.0;
    QJsonArray timings; for(auto us:acceptUs) timings.append(double(us)/1000.0);
    const QJsonObject report{
      {"scenario","offline-static-code-1000x800-to-30000px"},
      {"captureAndPreviewLatency","not measured by offline matcher test"},
      {"cacheFileCount","not exposed by engine"},
      {"framesOffered",acceptUs.size()},{"framesAccepted",accepted},{"firstFailureAt",firstFailure},
      {"confirmedHeight",engine.size().height()},{"wallMs",double(wallMs)},
      {"processCpuMs",double(cpuMs)},{"processCpuOverWall",wallMs?double(cpuMs)/wallMs:0.0},
      {"workingSetDeltaMiB",double(peakWorkingSet-baselineWorkingSet)/(1024*1024)},
      {"workingSetIncludesTestFrames",true},
      {"acceptP95Ms",percentile(acceptUs,95)/1000.0},
      {"acceptP99Ms",percentile(acceptUs,99)/1000.0},
      {"earlyP95Ms",earlyP95Ms},{"lateP95Ms",lateP95Ms},
      {"lateOverEarlyP95",earlyP95Ms?lateP95Ms/earlyP95Ms:0.0},
      {"targetP95AtMost8Ms",percentile(acceptUs,95)<=8000},
      {"targetP99AtMost16Ms",percentile(acceptUs,99)<=16000},
      {"targetLateP95GrowthAtMost20Percent",earlyP95Ms>0 && lateP95Ms<=earlyP95Ms*1.2},
      {"acceptMs",timings}};
    QFile file(artifacts+"/long-tracking-budget.json");
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QJsonDocument(report).toJson()),qint64(QJsonDocument(report).toJson().size()));
    qInfo()<<"LONG_TRACKING_BUDGET"<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    QCOMPARE(firstFailure,-1);
    QCOMPARE(engine.size(),QSize(1000,30800));
    QCOMPARE(engine.image(),doc.copy(0,0,1000,30800));
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
  void independentDesktopProducer() {
    if(QGuiApplication::platformName()!="windows") QSKIP("Real Windows desktop required");
    wchar_t desktopClass[256]{}; GetClassNameW(WindowFromPoint({300,270}),desktopClass,256);
    if(QString::fromWCharArray(desktopClass).startsWith("LockScreen"))
      QSKIP("Windows desktop is locked; live producer pixels cannot be verified");
    const QRect area=physicalRect(QRect(80,100,640,460));
    struct Context { QImage document; std::atomic<int> at{0}; int height=0; };
    Context context; context.document=document("paragraphs",area.width(),area.height()+2200);
    context.height=area.height();
    std::promise<HWND> ready;
    auto windowReady=ready.get_future();
    std::atomic<bool> begin{false};
    std::mutex eventMutex;
    QHash<int,qint64> eventNs;
    const QList<int> positions={100,260,430,600,780,970,1150,1320,1480,1630};
    std::jthread producer([&](std::stop_token stop) {
      WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr);
      cls.lpszClassName=L"PicutScrollV2IndependentProducer";
      cls.lpfnWndProc=[](HWND window,UINT message,WPARAM w,LPARAM l)->LRESULT {
        if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,
          LONG_PTR(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams));
        auto c=reinterpret_cast<Context *>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_PAINT && c) {
          PAINTSTRUCT paint{}; auto dc=BeginPaint(window,&paint); BITMAPINFO info{};
          info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
          info.bmiHeader.biWidth=c->document.width(); info.bmiHeader.biHeight=-c->height;
          info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
          SetDIBitsToDevice(dc,0,0,UINT(c->document.width()),UINT(c->height),0,0,0,
            UINT(c->height),c->document.constScanLine(c->at.load()),&info,DIB_RGB_COLORS);
          EndPaint(window,&paint); return 0;
        }
        return DefWindowProcW(window,message,w,l);
      };
      if(!RegisterClassW(&cls)) { ready.set_value(nullptr); return; }
      HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,cls.lpszClassName,
        L"picut independent producer",WS_POPUP|WS_VISIBLE,area.x(),area.y(),
        area.width(),area.height(),nullptr,nullptr,cls.hInstance,&context);
      ready.set_value(window);
      if(window) {
        UpdateWindow(window); DwmFlush();
        while(!begin.load() && !stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        for(int at:positions) {
          if(stop.stop_requested()) break;
          context.at.store(at);
          {
            std::lock_guard lock(eventMutex);
            eventNs.insert(at,monotonicNanoseconds());
          }
          InvalidateRect(window,nullptr,FALSE); UpdateWindow(window); DwmFlush();
          for(int ms=0;ms<150 && !stop.stop_requested();ms+=5)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        while(!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        DestroyWindow(window);
      }
      UnregisterClassW(cls.lpszClassName,cls.hInstance);
    });
    auto stopProducer=qScopeGuard([&] {
      if(producer.joinable()) { producer.request_stop(); begin.store(true); producer.join(); }
    });
    QVERIFY(windowReady.get());
    ScrollFramePump pump(area); pump.start();
    bool pumpStopped=false;
    auto stopPump=qScopeGuard([&] { if(!pumpStopped) pump.stop(); });
    QImage initial;
    const auto expected=context.document.copy(0,0,area.width(),area.height());
    QTRY_VERIFY_WITH_TIMEOUT((initial=pump.take())==expected,5000);
    ScrollStitcherV2 engine;
    QCOMPARE(engine.accept(initial),ScrollStitcher::Started);
    ScrollMatchPump matcher(&pump,&engine); matcher.start();
    bool matcherStopped=false;
    auto stopMatcher=qScopeGuard([&] { if(!matcherStopped) matcher.stop(true); });
    quint64 serial=0;
    QList<qint64> previewLatencyUs;
    QSet<int> confirmedPositions;
    QJsonArray failedUpdates;
    int lastPosition=0,updates=0,failures=0;
    begin.store(true);
    QElapsedTimer wait; wait.start();
    while(lastPosition!=positions.last() && wait.elapsed()<10000) {
      auto update=matcher.takeUpdate(serial);
      if(!update) { QTest::qWait(5); continue; }
      serial=update->serial; ++updates;
      if(!good(update->result)) {
        ++failures;
        failedUpdates.append(QJsonObject{{"result",int(update->result)},
          {"producerAt",context.at.load()},{"lastConfirmed",update->preview.viewportY}});
        continue;
      }
      lastPosition=update->preview.viewportY;
      confirmedPositions.insert(lastPosition);
      qint64 occurred=0;
      {
        std::lock_guard lock(eventMutex);
        occurred=eventNs.value(lastPosition);
      }
      if(occurred) previewLatencyUs.append((monotonicNanoseconds()-occurred)/1000);
    }
    producer.request_stop(); producer.join();
    pump.stop(); pumpStopped=true;
    matcher.stop(true); matcherStopped=true;
    QJsonArray missedPositions;
    for(int at:positions) if(!confirmedPositions.contains(at)) missedPositions.append(at);
    QJsonObject report{
      {"scenario","independent-win32-producer-to-matcher-preview"},
      {"desktopBackend",pump.backendName()},
      {"producedChanges",positions.size()},{"matcherUpdatesObserved",updates},
      {"matcherFailuresObserved",failures},{"lastViewportPosition",lastPosition},
      {"failedUpdates",failedUpdates},
      {"missedProducerPositions",missedPositions},
      {"changedDesktopFrames",double(pump.changedFrameCount())},
      {"samplePollHz",pump.captureRate()},
      {"previewP95Ms",percentile(previewLatencyUs,95)/1000.0},
      {"previewP99Ms",percentile(previewLatencyUs,99)/1000.0},
      {"latencySamples",previewLatencyUs.size()},
      {"targetPreviewP95AtMost100Ms",!previewLatencyUs.isEmpty() && percentile(previewLatencyUs,95)<=100000}};
    QFile reportFile(artifacts+"/independent-desktop-producer.json");
    QVERIFY(reportFile.open(QIODevice::WriteOnly));
    reportFile.write(QJsonDocument(report).toJson());
    qInfo()<<"INDEPENDENT_DESKTOP_PRODUCER"<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    QVERIFY2(missedPositions.size()<=1,"two or more producer positions never reached preview");
    QCOMPARE(lastPosition,positions.last());
    QCOMPARE(engine.image(),context.document.copy(0,0,area.width(),positions.last()+area.height()));
  }
};

int main(int argc,char **argv) {
  enableDpi(); QApplication app(argc,argv); ScrollV2Tests tests; return QTest::qExec(&tests,argc,argv);
}
#include "scroll_v2_tests.moc"
