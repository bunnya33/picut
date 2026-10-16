#include "recognition.h"
#include <QtWidgets/QPushButton>
#include "image_text.h"
#include "store.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QFontMetrics>
#include <QtGui/QFontDatabase>
#include <QtGui/QPainter>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QLabel>
#include <winrt/base.h>
#include <algorithm>
#include <numeric>

class OcrTests : public QObject {
  Q_OBJECT
  QString artifact;
  QImage sample(const QString &text, int pixels = 24, bool dark = false, bool transparent = false) {
    QFont font("Microsoft YaHei UI"); font.setPixelSize(pixels);
    QFontMetrics metrics(font);
    auto lines = text.split('\n'); int width = 0;
    for (auto line : lines) width = qMax(width, metrics.horizontalAdvance(line));
    QImage image(width+48, metrics.height()*lines.size()+48, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : (dark ? QColor(24,35,47) : QColor(Qt::white)));
    QPainter p(&image); p.setFont(font); p.setPen(dark ? QColor(232,238,247) : QColor(Qt::black));
    int y = 24+metrics.ascent();
    for (auto line : lines) { p.drawText(24,y,line); y += metrics.height(); }
    return image;
  }
  QString normalized(QString text) { text.remove(QRegularExpression("\\s")); return text; }
  int distance(const QString &expected, const QString &actual) {
    auto a = normalized(expected).toUcs4(), b = normalized(actual).toUcs4();
    std::vector<int> prev(b.size()+1), next(b.size()+1); std::iota(prev.begin(),prev.end(),0);
    for (int i = 1; i <= a.size(); ++i) {
      next[0] = i;
      for (int j = 1; j <= b.size(); ++j)
        next[j] = std::min({prev[j]+1,next[j-1]+1,prev[j-1]+int(a[i-1]!=b[j-1])});
      std::swap(prev,next);
    }
    return prev.back();
  }
  QJsonObject run(QStringList arguments,bool gpu=false) {
    QProcess process;
    process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    if(gpu) arguments.append("--gpu");
    int outputIndex = arguments.indexOf("--output");
    QFile::remove(arguments.value(outputIndex+1));
    process.start(QCoreApplication::applicationDirPath()+(gpu?"/gpu/FloatShotPaddleOcr.exe":"/FloatShotPaddleOcr.exe"), arguments);
    if (!process.waitForFinished(180000)) { process.kill(); process.waitForFinished(); return {{"error","Test timeout"}}; }
    QFile file(arguments.value(outputIndex+1));
    if (!file.open(QIODevice::ReadOnly)) return {{"error","Missing output: " + QString::fromUtf8(process.readAllStandardError())}};
    return QJsonDocument::fromJson(file.readAll()).object();
  }
  void verifyCharacterGeometry(const QJsonObject &result, QSize imageSize) {
    QCOMPARE(result["coordinate_space"].toString(),QString("original"));
    QCOMPARE(result["image_width"].toInt(),imageSize.width());
    QCOMPARE(result["image_height"].toInt(),imageSize.height());
    ImageTextPage page; page.load(result);
    for (const auto &line : page.lines) {
      QString joined;
      for (const auto &glyph : line.glyphs) {
        joined += glyph.text;
        QVERIFY(glyph.quad.boundingRect().width()>0);
        QVERIFY(glyph.quad.boundingRect().height()>0);
        for (auto point : glyph.quad) {
          QVERIFY(point.x()>=0 && point.x()<=imageSize.width());
          QVERIFY(point.y()>=0 && point.y()<=imageSize.height());
        }
        const auto start=(glyph.quad[0]+glyph.quad[3])/2,
                   end=(glyph.quad[1]+glyph.quad[2])/2;
        QCOMPARE(page.caretAt(start*.95+end*.05),glyph.offset);
        QCOMPARE(page.caretAt(start*.05+end*.95),glyph.offset+glyph.text.size());
      }
      QCOMPARE(joined,line.text);
      // Every tested worker result must contain actual CTC character positions.
      QVERIFY(line.glyphs.size()>1 || line.text.size()==1);
    }
  }
private slots:
  void initTestCase() {
    for (auto name : {"msyh.ttc", "msyhbd.ttc", "arial.ttf", "meiryo.ttc"})
      QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/"+name);
    artifact = qEnvironmentVariable("FLOATSHOT_OCR_TEST_DIR");
    if (artifact.isEmpty()) artifact = QDir::tempPath()+"/FloatShot-OCR-tests";
    QVERIFY(QDir().mkpath(artifact));
    QVERIFY(QFile::exists(PaddleOcrJob::modelsPath()+"/models.json"));
  }
  void accuracyAndScripts() {
    const QString expected = "中文截图识别测试，录屏已暂停。 FloatShot ABC abc 0123456789 100.50 2026-10-10";
    QJsonArray cases; QMap<QString,QString> truth; QMap<QString,QImage> images;
    auto add = [&](const QString &id,const QString &text,const QImage &image) {
      auto path = artifact+"/"+id+".png"; QVERIFY(image.save(path));
      cases.append(QJsonObject{{"id",id},{"input",path}}); truth[id] = text; images[id] = image;
    };
    for (int size : {12,16,24,36}) for (bool dark : {false,true})
      add(QString("%1-%2").arg(size).arg(dark ? "dark":"light"),expected,sample(expected,size,dark));
    add("traditional","繁體中文文字識別，錄製已暫停。",sample("繁體中文文字識別，錄製已暫停。",32));
    add("japanese","日本語の文字認識テスト 12345",sample("日本語の文字認識テスト 12345",32));
    add("latin","Français déjà vu — Grüße für Köln 12345",sample("Français déjà vu — Grüße für Köln 12345",32));
    add("vietnamese","Tiếng Việt: nhận dạng văn bản 12345",sample("Tiếng Việt: nhận dạng văn bản 12345",32));
    add("transparent","透明背景文字 12345",sample("透明背景文字 12345",32,false,true));
    auto rotated = sample("截图识别已暂停 12345",32).transformed(QTransform().rotate(180));
    add("upside-down","截图识别已暂停 12345",rotated);
    auto vertical = sample("截图识别已暂停 12345",32).transformed(QTransform().rotate(90));
    add("rotated-90","截图识别已暂停 12345",vertical);
    add("multiline","第一行中文 12345\nSecond line ABC 100.50\n第三行，录屏已暂停。",sample("第一行中文 12345\nSecond line ABC 100.50\n第三行，录屏已暂停。",24));
    QImage blank(600,300,QImage::Format_RGB32); blank.fill(Qt::white); add("blank","",blank);
    auto batch = artifact+"/cases.json", output = artifact+"/paddle-results.json";
    QVERIFY(Store::writeJson(batch,QJsonObject{{"cases",cases}}));
    auto result = run({"--models",PaddleOcrJob::modelsPath(),"--batch",batch,"--output",output});
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    QCOMPARE(result["cases"].toArray().size(),cases.size());
    QJsonArray report; int paddleErrors=0, windowsErrors=0, total=0;
    for (auto value : result["cases"].toArray()) {
      auto item=value.toObject(); auto id=item["id"].toString(), actual=item["text"].toString();
      int errors=distance(truth[id],actual);
      item["expected"]=truth[id]; item["paddle_errors"]=errors;
      item["expected_characters"]=normalized(truth[id]).toUcs4().size();
      if (id.contains("-light") || id.contains("-dark")) {
        QString windows, error;
        auto thread=QThread::create([&] {
          try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            try { windows=recognizeWindowsText(images[id]); }
            catch (...) { winrt::uninit_apartment(); throw; }
            winrt::uninit_apartment();
          } catch (...) { error="Windows OCR unavailable"; }
        });
        thread->start(); thread->wait(); delete thread;
        QVERIFY2(error.isEmpty(),qPrintable(error));
        item["windows_text"]=windows; item["windows_errors"]=distance(truth[id],windows);
        paddleErrors+=errors; windowsErrors+=distance(truth[id],windows); total+=normalized(truth[id]).toUcs4().size();
      }
      report.append(item);
      verifyCharacterGeometry(item,images[id].size());
      qInfo().noquote() << id << "errors:" << errors << "text:" << actual;
      if (id=="blank") QVERIFY(actual.isEmpty());
      if (id=="traditional" || id=="japanese" || id=="transparent" || id=="upside-down" || id=="rotated-90")
        QVERIFY2(errors <= 2,qPrintable(id+": "+actual));
    }
    QJsonObject summary{{"cases",report},{"paddle_errors",paddleErrors},{"windows_errors",windowsErrors},
      {"characters",total},{"paddle_cer",double(paddleErrors)/total},{"windows_cer",double(windowsErrors)/total},
      {"worker_peak_mb",result["peak_working_set_mb"]},{"batch_total_ms",result["total_ms"]},
      {"normalization","Only whitespace ignored. Case and punctuation preserved. Synthetic samples; not a real-world accuracy guarantee."}};
    QVERIFY(Store::writeJson(artifact+"/quality-report.json",summary));
    qInfo() << "Paddle errors" << paddleErrors << "Windows errors" << windowsErrors << "of" << total;
    QVERIFY2(paddleErrors <= windowsErrors,"Paddle should improve or equal the previous engine on this fixed screenshot corpus");
  }
  void coldProcessAndCleanup() {
    PaddleOcrJob job; QSignalSpy finished(&job,&PaddleOcrJob::finished);
    job.start(sample("中文截图测试 0123456789",32));
    auto temp=job.temporaryPath(); QVERIFY(QFile::exists(temp+"/input.png"));
    QVERIFY(finished.wait(60000));
    auto result=finished.first()[0].toJsonObject();
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    QVERIFY(result["text"].toString().contains("0123456789"));
    QVERIFY(!job.running()); QVERIFY(!QFile::exists(temp));
    QVERIFY(Store::writeJson(artifact+"/cold-process.json",result));
  }
  void cancellationAndFailure() {
    PaddleOcrJob job; QSignalSpy finished(&job,&PaddleOcrJob::finished);
    job.start(sample("取消识别测试 12345",32)); auto temp=job.temporaryPath();
    job.cancel(); QVERIFY(!job.running()); QVERIFY(!QFile::exists(temp)); QCOMPARE(finished.size(),0);
    job.start(QImage()); QCOMPARE(finished.size(),1); QVERIFY(finished[0][0].toJsonObject().contains("error"));
    job.start(sample("test"),"missing-profile"); QCOMPARE(finished.size(),2);
    auto result=run({"--models",PaddleOcrJob::modelsPath(),"--input",artifact+"/不存在.png","--output",artifact+"/failure.json"});
    QVERIFY(result.contains("error"));
  }
  void unicodePathAndManualRotation() {
    auto path=artifact+"/中文路径 识别.png";
    QVERIFY(sample("文字识别 12345",32).transformed(QTransform().rotate(90)).save(path));
    auto result=run({"--models",PaddleOcrJob::modelsPath(),"--input",path,"--rotation","270","--output",artifact+"/中文结果.json"});
    QVERIFY2(result["text"].toString().contains("12345"),qPrintable(result["error"].toString()));
    verifyCharacterGeometry(result,QImage(path).size());
    // The same original pixels with the opposite manual rotation exercise both
    // inverse transforms, while automatic classification handles the upside-down crop.
    result=run({"--models",PaddleOcrJob::modelsPath(),"--input",path,"--rotation","90","--output",artifact+"/manual-90.json"});
    QVERIFY2(result["text"].toString().contains("12345"),qPrintable(result["error"].toString()));
    verifyCharacterGeometry(result,QImage(path).size());
  }
  void largeImageTiles() {
    auto words=sample("区域边界文字 0123456789",28);
    QImage image(3400,1850,QImage::Format_RGB32); image.fill(Qt::white);
    QPainter p(&image); p.drawImage(1450,1360,words); p.drawImage(2900,1700,sample("底部测试 ABC123",28)); p.end();
    auto path=artifact+"/large-tiles.png"; QVERIFY(image.save(path));
    auto result=run({"--models",PaddleOcrJob::modelsPath(),"--input",path,"--output",artifact+"/large-tiles.json"});
    auto text=normalized(result["text"].toString());
    QVERIFY2(text.contains("区域边界文字0123456789"),qPrintable(text));
    QCOMPARE(text.count("0123456789"),1);
    QVERIFY2(text.contains("底部测试ABC123"),qPrintable(text));
    verifyCharacterGeometry(result,image.size());
  }
  void denseTextPerformance() {
    QFont font("Microsoft YaHei UI"); font.setPixelSize(16);
    QImage image(2560,1440,QImage::Format_RGB32); image.fill(Qt::white);
    QPainter p(&image); p.setFont(font); p.setPen(Qt::black);
    QStringList expected;
    for(int column=0;column<3;++column) for(int row=0;row<40;++row) {
      QString text=QString("第%1行 中文截图识别测试 FloatShot ABC 0123456789 100.50").arg(column*40+row+1);
      p.drawText(30+column*830,30+row*34,text); expected.append(text);
    }
    p.end(); auto path=artifact+"/dense-text.png"; QVERIFY(image.save(path));
    QVERIFY(Store::writeJson(artifact+"/dense-expected.json",QJsonObject{{"text",expected.join('\n')}}));
    auto result=run({"--models",PaddleOcrJob::modelsPath(),"--input",path,"--output",artifact+"/dense-timing.json"});
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    QVERIFY(result["lines"].toArray().size()>=110);
    qInfo() << "Dense text timing" << result["timing"] << "total" << result["total_ms"];
    auto gpu=run({"--models",PaddleOcrJob::modelsPath(),"--input",path,"--output",artifact+"/dense-gpu-timing.json"},true);
    if(gpu.value("error").toString()=="没有可用的 DirectX 12 显卡。") QSKIP("CPU corpus passed; this machine has no supported GPU.");
    QVERIFY2(!gpu.contains("error"),qPrintable(gpu["error"].toString()));
    QCOMPARE(normalized(gpu["text"].toString()),normalized(result["text"].toString()));
    verifyCharacterGeometry(gpu,image.size());
  }
  void imageSelectionWithRealOcr() {
    QTemporaryDir data; Store store(data.path());
    store.config["ocr_acceleration"]="cpu";
    QImage image(600,360,QImage::Format_RGB32); image.fill(Qt::white);
    {
      QPainter p(&image); QFont font("Microsoft YaHei UI"); font.setPixelSize(28);
      p.setFont(font); p.setPen(Qt::black);
      p.drawText(30,65,"交易记录");
      p.drawText(30,140,"金额"); p.drawText(290,140,"10.49");
      p.drawText(30,210,"记账日期"); p.drawText(290,210,"2026/10/03");
      p.drawText(30,280,"商户名称"); p.drawText(290,280,"FloatShot");
    }
    QVERIFY(image.save(artifact+"/image-text-source.png"));
    auto savedClipboard=std::make_unique<QMimeData>();
    if (auto mime=QApplication::clipboard()->mimeData())
      for (const auto &format:mime->formats()) savedClipboard->setData(format,mime->data(format));
    auto restore=qScopeGuard([&] { QApplication::clipboard()->setMimeData(savedClipboard.release()); });
    QWidget window; window.resize(740,510);
    QRect content(70,40,600,360);
    ImageTextOverlay overlay(&window,&store,[&] { return content; });
    window.show(); overlay.begin(image);
    auto job=overlay.findChild<PaddleOcrJob *>(); QVERIFY(job);
    auto temporary=job->temporaryPath();
    QTRY_VERIFY_WITH_TIMEOUT(!job->running(),60000);
    QVERIFY2(overlay.page().text.contains("10.49"),qPrintable(overlay.page().text));
    QVERIFY(overlay.page().text.contains("2026/10/03"));
    QVERIFY(!QFileInfo::exists(temporary));
    int first=overlay.page().text.indexOf(".49"), last=first+3;
    auto quads=overlay.page().highlights(first,last); QCOMPARE(quads.size(),3);
    auto left=(quads.first()[0]+quads.first()[3])/2,
         right=(quads.last()[1]+quads.last()[2])/2;
    auto drag=[&] {
      auto map=[&](QPointF pt) { return overlay.imageToView(pt).toPoint(); };
      QTest::mousePress(&overlay,Qt::LeftButton,Qt::NoModifier,map(left*.98+right*.02));
      QTest::mouseRelease(&overlay,Qt::LeftButton,Qt::NoModifier,map(left*.02+right*.98));
    };
    drag(); QCOMPARE(overlay.selectedText(),QString(".49"));
    QCOMPARE(overlay.resultEditor()->textCursor().selectedText(),QString(".49"));
    QTest::keyClick(&overlay,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(),QString(".49"));
    QCOMPARE(overlay.sourceImage(),image);
    QVERIFY(overlay.grab().save(artifact+"/image-text-partial-selection.png"));
    QTest::keyClick(&overlay,Qt::Key_A,Qt::ControlModifier);
    QVERIFY(overlay.selectedText().contains("记账日期"));
    QVERIFY(overlay.grab().save(artifact+"/image-text-all-selection.png"));
    for (const auto &line:overlay.page().lines) if (line.text=="FloatShot") {
      const auto quad=line.glyphs.last().quad;
      auto point=(quad[0]+quad[3])*.05+(quad[1]+quad[2])*.45;
      QTest::mouseDClick(&overlay,Qt::LeftButton,Qt::NoModifier,overlay.imageToView(point).toPoint());
      QCOMPARE(overlay.selectedText(),QString("FloatShot"));
    }
    overlay.finish(); overlay.begin(image);
    QCOMPARE(overlay.recognitionStarts(),1); QVERIFY(!job->running());
    // Resizing the surface remaps the same result without another inference.
    content=QRect(120,70,420,252); window.resize(720,490); drag();
    QCOMPARE(overlay.selectedText(),QString(".49"));
    overlay.finish();
    image.setPixelColor(0,0,Qt::red); overlay.begin(image);
    QCOMPARE(overlay.recognitionStarts(),2); QVERIFY(job->running());
    temporary=job->temporaryPath();
    QTest::mouseClick(overlay.findChild<QPushButton *>("imageTextCancel"),Qt::LeftButton);
    QVERIFY(!job->running()); QVERIFY(!QFileInfo::exists(temporary));
  }
  void gpuAccuracyAndCancellation() {
    const QString expected="中文截图识别测试，录屏已暂停。 FloatShot ABC abc 0123456789 100.50 2026-10-10";
    QJsonArray cases; QJsonArray report; int errors=0;
    for(int size:{12,16,24,36}) for(bool dark:{false,true}) {
      auto id=QString("gpu-%1-%2").arg(size).arg(dark?"dark":"light"), path=artifact+"/"+id+".png";
      QVERIFY(sample(expected,size,dark).save(path)); cases.append(QJsonObject{{"id",id},{"input",path}});
    }
    QVERIFY(Store::writeJson(artifact+"/gpu-cases.json",QJsonObject{{"cases",cases}}));
    auto result=run({"--models",PaddleOcrJob::modelsPath(),"--batch",artifact+"/gpu-cases.json","--output",artifact+"/gpu-results.json"},true);
    if(result.value("error").toString()=="没有可用的 DirectX 12 显卡。") QSKIP("No supported GPU; automatic CPU fallback is tested separately.");
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    QCOMPARE(result["cases"].toArray().size(),8);
    for(auto value:result["cases"].toArray()) {
      auto item=value.toObject(); int count=distance(expected,item["text"].toString()); errors+=count;
      item["expected"]=expected; item["errors"]=count; report.append(item);
    }
    QVERIFY(Store::writeJson(artifact+"/gpu-quality-report.json",QJsonObject{{"cases",report},{"errors",errors},{"characters",448},{"cer",double(errors)/448}}));
    QVERIFY2(errors<=2,qPrintable(QString("GPU corpus errors: %1").arg(errors)));
    QImage input(800,500,QImage::Format_RGB32); input.fill(Qt::white);
    {QPainter p(&input); p.drawImage(20,20,sample("显卡测试 0123456789",32));}
    PaddleOcrJob accelerated; QSignalSpy acceleratedDone(&accelerated,&PaddleOcrJob::finished);
    accelerated.start(input,"common",0,"gpu"); QVERIFY(acceleratedDone.wait(60000));
    QVERIFY(acceleratedDone.first()[0].toJsonObject()["gpu"].toBool());
    accelerated.start(input,"common",0,"gpu"); auto path=accelerated.temporaryPath();
    accelerated.cancel(); QVERIFY(!accelerated.running()); QVERIFY(!QFileInfo::exists(path));
  }
  void automaticCpuFallback() {
    // The real CPU worker rejects --gpu. Verify that automatic selection can
    // recover from this actual backend failure without leaving a child process.
    PaddleOcrJob fallback(nullptr,QCoreApplication::applicationDirPath()+"/FloatShotPaddleOcr.exe");
    QSignalSpy finished(&fallback,&PaddleOcrJob::finished), progress(&fallback,&PaddleOcrJob::progress);
    QImage input(800,500,QImage::Format_RGB32); input.fill(Qt::white);
    {QPainter p(&input); p.drawImage(20,20,sample("回退测试 0123456789",32));}
    fallback.start(input,"common",0,"auto");
    auto path=fallback.temporaryPath(); QVERIFY(finished.wait(60000));
    auto result=finished.first()[0].toJsonObject();
    QVERIFY2(result["text"].toString().contains("0123456789"),qPrintable(result["error"].toString()));
    QVERIFY(result["acceleration_fallback"].toBool()); QVERIFY(!result["gpu"].toBool());
    QVERIFY(!fallback.running()); QVERIFY(!QFileInfo::exists(path));
    QVERIFY(std::any_of(progress.begin(),progress.end(),[](const auto &row){return row[0].toString()=="fallback";}));
    PaddleOcrJob unavailable(nullptr,QCoreApplication::applicationDirPath()+"/missing-gpu-worker.exe");
    QSignalSpy unavailableDone(&unavailable,&PaddleOcrJob::finished);
    unavailable.start(input,"common",0,"auto"); QVERIFY(unavailableDone.wait(60000));
    result=unavailableDone.first()[0].toJsonObject();
    QVERIFY2(result["text"].toString().contains("0123456789"),qPrintable(result["error"].toString()));
    QVERIFY(!result["gpu"].toBool());
    unavailableDone.clear(); unavailable.start(input,"common",0,"gpu");
    path=unavailable.temporaryPath(); QTRY_COMPARE_WITH_TIMEOUT(unavailableDone.size(),1,60000);
    QVERIFY(unavailableDone.first()[0].toJsonObject().contains("error"));
    QVERIFY(!unavailable.running()); QVERIFY(!QFileInfo::exists(path));
  }
  void workspaceCompletionAndCancellation() {
    QTemporaryDir data; Store store(data.path());
    QTimer poll;
    bool completed=false;
    connect(&poll,&QTimer::timeout,this,[&] {
      auto dialog=qobject_cast<ImageTextOverlay *>(QApplication::activeModalWidget());
      if (!dialog) for (auto widget:QApplication::topLevelWidgets())
        if (auto workspace=qobject_cast<ImageTextOverlay *>(widget);workspace && workspace->isVisible()) dialog=workspace;
      if (!dialog) return;
      auto text=dialog->resultEditor();
      if (text && text->toPlainText().contains("12345")) {
        completed=true;
        QVERIFY(text->isReadOnly());
        QVERIFY(dialog->windowFlags()&Qt::FramelessWindowHint);
        dialog->grab().save(artifact+"/recognition-workspace.png"); dialog->finish();
      }
    });
    poll.start(100);
    QTimer timeout; timeout.setSingleShot(true);
    connect(&timeout,&QTimer::timeout,this,[] {
      for (auto widget:QApplication::topLevelWidgets())
        if (auto d=qobject_cast<ImageTextOverlay *>(widget)) d->finish();
    });
    timeout.start(60000);
    showRecognition(sample("中文识别 12345",32),false,nullptr,&store);
    poll.stop(); timeout.stop(); QVERIFY(completed);
    QTimer::singleShot(20,this,[] {
      for (auto widget:QApplication::topLevelWidgets())
        if (auto d=qobject_cast<ImageTextOverlay *>(widget)) d->finish();
    });
    QElapsedTimer timer; timer.start();
    showRecognition(sample("立即取消",32),false,nullptr,&store);
    QVERIFY(timer.elapsed()<3000);
  }
  void fullscreenWorkspaceRealOcrAndScroll() {
    QTemporaryDir data; Store store(data.path()); store.config["ocr_acceleration"]="cpu";
    QImage image(1600,1000,QImage::Format_RGB32); image.fill(Qt::white);
    {
      QPainter p(&image); QFont font("Microsoft YaHei UI"); font.setPixelSize(28);
      p.setFont(font); p.setPen(Qt::black);
      p.drawText(48,60,"交易明细 · 图片与文字同步选择");
      for (int row=0;row<20;++row)
        p.drawText(48,120+row*38,QString("第%1行 账单记录 金额 100.50 日期 2026/10/03").arg(row+1));
      p.drawText(1100,950,"图像底部 ABC123");
    }
    QWidget owner; owner.setGeometry(QGuiApplication::primaryScreen()->availableGeometry()); owner.show();
    ImageTextOverlay workspace(&owner,&store,[&]{return owner.rect();});
    workspace.begin(image);
    auto job=workspace.findChild<PaddleOcrJob *>(); QTRY_VERIFY_WITH_TIMEOUT(!job->running(),60000);
    const int offset=workspace.page().text.indexOf("ABC123");
    QVERIFY2(offset>=0,qPrintable(workspace.page().text));
    QCOMPARE(workspace.placement(),ImageTextLayout::Split);
    // Enlarge the image, then select text outside the initial visible viewport.
    QWheelEvent wheel(workspace.imageViewport().center(),workspace.mapToGlobal(workspace.imageViewport().center()),
                      {},{0,720},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(&workspace,&wheel);
    auto cursor=workspace.resultEditor()->textCursor(); cursor.setPosition(offset);
    cursor.setPosition(offset+6,QTextCursor::KeepAnchor); workspace.resultEditor()->setTextCursor(cursor);
    QCOMPARE(workspace.selectedText(),QString("ABC123"));
    auto quads=workspace.page().highlights(offset,offset+6); QCOMPARE(quads.size(),6);
    QVERIFY(QRectF(workspace.imageViewport()).contains(workspace.imageToView(quads.last().boundingRect().center())));
    QVERIFY(workspace.grab().save(artifact+"/image-text-fullscreen-zoomed.png"));
    workspace.fitImage();
    QVERIFY(workspace.grab().save(artifact+"/image-text-fullscreen.png"));
    QCOMPARE(workspace.sourceImage(),image);
    workspace.finish();
  }
};
QTEST_MAIN(OcrTests)
#include "ocr_tests.moc"
