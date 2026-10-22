#include "image_translation.h"
#include "translation_terms.h"
#include "store.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QScopeGuard>
#include <QtGui/QClipboard>
#include <QtGui/QFontDatabase>
#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>
#include <QtGui/QTextCursor>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QToolButton>
#include <QtTest/QtTest>

class TranslationTests : public QObject {
  Q_OBJECT
  QString artifact;
  QJsonArray report;
  static QJsonObject line(QString text, QRect rect) {
    return {{"text", text}, {"quad", QJsonArray{QJsonArray{rect.left(),rect.top()},QJsonArray{rect.right()+1,rect.top()},
              QJsonArray{rect.right()+1,rect.bottom()+1},QJsonArray{rect.left(),rect.bottom()+1}}}};
  }
  QJsonObject worker(const QJsonArray &blocks, QString target, QString source = "auto") {
    LocalTranslationJob job; QSignalSpy finished(&job, &LocalTranslationJob::finished);
    job.start(blocks, source, target);
    QElapsedTimer timer; timer.start();
    while (finished.isEmpty() && timer.elapsed() < 120000) QTest::qWait(10);
    if (finished.isEmpty()) return {{"error", "Test timed out"}};
    const auto value = finished.first().first().toJsonObject();
    QJsonObject entry = value; entry["target"] = target; entry["input"] = blocks; report.append(entry);
    return value;
  }
  QImage fixture(QJsonObject *ocr = nullptr) {
    QImage image(1000,650,QImage::Format_ARGB32); image.fill(QColor("#fafafa"));
    QPainter p(&image); p.fillRect(QRect(0,0,1000,78),QColor("#26344a"));
    QFont font("Microsoft YaHei UI"); font.setPixelSize(28); p.setFont(font);
    QJsonArray lines;
    auto draw = [&](QString text, QPoint point, QColor color = Qt::black) {
      const QFontMetrics metrics(font); const QRect box(point,QSize(metrics.horizontalAdvance(text)+2,metrics.height()));
      p.setPen(color); p.drawText(point + QPoint(0,metrics.ascent()),text); lines.append(line(text,box));
    };
    draw("Screenshot translation",{24,18},Qt::white);
    draw("Translate text directly inside the image.",{24,112});
    draw("Keep the original layout and paragraph positions.",{24,155});
    draw("No internet connection is required.",{24,228});
    draw("Save image",{24,310}); draw("Settings",{340,310});
    draw("2026/10/22  100.50",{24,382});
    p.setPen(QPen(QColor("#2080f0"),4)); p.setBrush(QColor("#e0f0ff"));
    p.drawRoundedRect(QRect(520,300,400,260),16,16); p.drawLine(550,510,880,345);
    if (ocr) *ocr = {{"lines",lines}};
    return image;
  }
private slots:
  void initTestCase() {
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/msyh.ttc");
    artifact = qEnvironmentVariable("PICUT_TRANSLATION_TEST_DIR");
    if (artifact.isEmpty()) artifact = QDir::tempPath()+"/picut-translation-tests";
    QVERIFY(QDir().mkpath(artifact));
  }
  void paragraphGroupingKeepsLabelsAndColumnsSeparate() {
    QImage image(1000,650,QImage::Format_ARGB32); image.fill(Qt::white);
    TranslationPage page;
    page.load(image,{{"lines", QJsonArray{
      line("This sentence wraps naturally across",{20,20,410,26}),
      line("two lines in the same paragraph.",{20,54,350,26}),
      line("Save",{20,150,60,26}),line("Cancel",{20,185,80,26}),
      line("Left column has a long sentence here.",{20,280,410,26}),
      line("Right column has another sentence.",{550,280,400,26}),
      line("and continues on the next line.",{20,314,350,26}),
      line("with its own continuation.",{550,314,360,26})}}});
    QCOMPARE(page.blocks.size(),5);
    QCOMPARE(page.blocks[0].original,QString("This sentence wraps naturally across two lines in the same paragraph."));
    QCOMPARE(page.blocks[1].original,QString("Save")); QCOMPARE(page.blocks[2].original,QString("Cancel"));
    QVERIFY(!page.blocks[3].original.contains("Right")); QVERIFY(!page.blocks[4].original.contains("Left"));
    QCOMPARE(page.blocks[3].lines.size(),2); QCOMPARE(page.blocks[4].lines.size(),2);
    page.load(image,{{"lines",QJsonArray{
        line("1. Keep this numbered item in its own position.",{20,20,500,26}),
        line("2. Keep the next item at the original line.",{20,54,500,26})}}});
    QCOMPARE(page.blocks.size(),2);
  }
  void renderChangesOnlySuccessfulTextRegions() {
    QJsonObject ocr; const auto original = fixture(&ocr); TranslationPage page; page.load(original,ocr);
    QCOMPARE(page.render(original),original);
    for (auto &block : page.blocks) { block.translated = block.original.contains("2026") ? block.original : QString("本地翻译"); }
    const auto image = page.render(original); QVERIFY(image != original); QCOMPARE(image.size(),original.size());
    QRegion allowed; for (const auto &block : page.blocks) allowed += block.bounds;
    for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
      if (!allowed.contains({x,y})) QCOMPARE(image.pixel(x,y),original.pixel(x,y));
    QCOMPARE(original.pixelColor(10,10),QColor("#26344a"));
    QVERIFY(page.blocks[0].foreground.lightness()>200); QVERIFY(page.blocks[1].foreground.lightness()<80);
    auto rejected = page.blocks[0].translated;
    page.apply({{"id",-1},{"text","wrong"}}); QCOMPARE(page.blocks[0].translated,rejected);
    QVERIFY(original.save(artifact+"/synthetic-source.png")); QVERIFY(image.save(artifact+"/synthetic-in-place.png"));
  }
  void overflowKeepsOriginalAndCompleteText() {
    QImage image(100,60,QImage::Format_ARGB32); image.fill(Qt::white); TranslationPage page;
    page.load(image,{{"lines",QJsonArray{line("A",{10,10,10,12})}}});
    page.blocks[0].translated = QString("Long translation ").repeated(100);
    QCOMPARE(page.render(image),image); QVERIFY(page.blocks[0].clipped);
    QCOMPARE(page.text(),page.blocks[0].translated);
    page.blocks[0].unchanged = true; QCOMPARE(page.render(image),image);
  }
  void anchoredLayoutKeepsImageWhenControlsMove() {
    for(const auto &available:{QRect(0,0,1920,1032),QRect(-1920,0,1920,1032)}) {
      for(auto image:{QRect(80,60,800,500),QRect(1050,410,800,500),QRect(0,0,1920,1080),QRect(10,970,80,40)}) {
        image.translate(available.topLeft());
        for(bool panel:{false,true}) {
          const auto layout=ImageTranslationLayout::arrange(image,available,{510,36},panel);
          QCOMPARE(layout.image.translated(layout.window.topLeft()),image);
          QVERIFY(available.contains(layout.toolbar.translated(layout.window.topLeft())));
          QVERIFY(available.contains(layout.status.translated(layout.window.topLeft())));
          if(panel)QVERIFY(available.contains(layout.panel.translated(layout.window.topLeft())));
        }
      }
    }
    const auto right=ImageTranslationLayout::arrange({1100,200,800,500},{0,0,1920,1032},{510,36},true);
    QVERIFY(right.panel.right()<right.image.left());
  }
  void scanAnimationPublishesOnlyCompleteResult() {
    QTemporaryDir dir; Store store(dir.path()); QWidget parent; parent.move(35,20); parent.show();
    QJsonObject ocr; const auto original=fixture(&ocr);
    ImageTranslationOverlay overlay(&parent,&store,[]{return QRect(200,180,800,520);});
    overlay.begin(original,false); overlay.setOcrResult(ocr,false);
    const auto position=overlay.sourceGlobalRect(); QCOMPARE(overlay.imageGlobalRect(),position);
    overlay.setPanelVisible(true); QCOMPARE(overlay.imageGlobalRect(),position);
    overlay.setPanelVisible(false); QCOMPARE(overlay.imageGlobalRect(),position);
    QVERIFY(overlay.isBusy()); QVERIFY(!overlay.hasCompletedTranslation());
    auto canvas=overlay.findChild<QWidget *>("translationCanvas"); QVERIFY(canvas);
    QTest::qWait(400); const auto scan=canvas->grab().toImage(); QTest::qWait(160);
    QVERIFY(scan!=canvas->grab().toImage());
    auto job=overlay.findChild<LocalTranslationJob *>(); QVERIFY(job);
    job->blockFinished({{"id",0},{"text","这个部分结果必须保持不可见"}});
    QTest::qWait(140);
    QCOMPARE(overlay.translatedImage(),original); QVERIFY(overlay.page().blocks[0].translated.isEmpty());
    QVERIFY(!overlay.findChild<QToolButton *>("translation_copy")->isEnabled());
    QVERIFY(overlay.grab().save(artifact+"/translation-loading.png"));
    QJsonArray values; for(const auto &block:overlay.page().blocks)values.append(QJsonObject{{"id",block.id},{"text","完整结果"}});
    overlay.setTranslationResult({{"blocks",values}});
    QVERIFY(!overlay.isBusy()); QVERIFY(overlay.hasCompletedTranslation());
    QVERIFY(overlay.translatedImage()!=original); QCOMPARE(overlay.imageGlobalRect(),position);
    overlay.finish(); overlay.begin(original,false);
    overlay.setShowingOriginal(true); QVERIFY(!overlay.showingOriginal());
    overlay.setTranslationResult({{"error","模拟翻译失败"}});
    QVERIFY(!overlay.isBusy()); QVERIFY(!overlay.hasCompletedTranslation()); QCOMPARE(overlay.translatedImage(),original);
    overlay.setPanelVisible(true);
    auto panel=overlay.findChild<QPlainTextEdit *>("translationText"); QVERIFY(panel);
    QCOMPARE(panel->toPlainText(),overlay.page().text(true));
    const auto point=canvas->mapFrom(&overlay,overlay.imageToView(overlay.page().blocks[1].bounds.center()).toPoint());
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
    QCOMPARE(panel->textCursor().selectedText(),overlay.page().blocks[1].original);
    overlay.finish();
    QImage dark(800,520,QImage::Format_ARGB32); dark.fill(QColor("#22232d"));
    { QPainter p(&dark); p.setPen(QColor("#f2f2f3")); p.setFont(QFont("Microsoft YaHei",12));
      p.drawText(20,40,"picut · 图片翻译");
      for(int row=0;row<9;++row)p.drawText(20,95+row*44,"正在识别并翻译图片中的文字，完成后统一显示结果。"); }
    overlay.begin(dark,false); QTest::qWait(730);
    QCOMPARE(overlay.sourceImage(),dark); QCOMPARE(overlay.translatedImage(),dark);
    QVERIFY(overlay.grab().save(artifact+"/translation-loading-dark.png"));
    overlay.finish();
  }
  void terminologyProtectsIdentifiersAndRejectsMissingMarkers() {
    TranslationTerminology glossary({"HybridCLR", "hybridclr is a native C# runtime."});
    for(const auto &text:{QString("HybridCLR"),QString("CLR"),QString("C++"),QString("il2cpp"),
                         QString("https://www.hybridclr.cn/docs"),QString(".NET"),QString("mail@example.com")}) {
      const auto protectedText=glossary.protect(text); QVERIFY(protectedText.onlyTerms);
      auto translated=protectedText.masked; QVERIFY(protectedText.restore(&translated)); QCOMPARE(translated,text);
    }
    const auto protectedText=glossary.protect("hybridclr integrates CLR and il2cpp using C++.");
    QCOMPARE(protectedText.replacements.size(),4); QVERIFY(!protectedText.onlyTerms);
    auto missing=QString("错误结果"); QVERIFY(!protectedText.restore(&missing));
    auto duplicate=protectedText.masked+' '+protectedText.replacements[0].first;
    QVERIFY(!protectedText.restore(&duplicate));
    for(const auto &label:{QString("My Site Logo"),QString("Quick Start Guide"),QString("THIS IS A TEST")})
      QVERIFY(glossary.protect(label).replacements.isEmpty());
  }
  void workspaceComparisonSelectionAndCancel() {
    QTemporaryDir dir; Store store(dir.path()); QWidget parent; parent.show();
    QJsonObject ocr; const auto image=fixture(&ocr);
    ImageTranslationOverlay overlay(&parent,&store,[]{return QRect(100,100,1000,650);});
    overlay.begin(image,false); overlay.setOcrResult(ocr,false);
    QJsonArray values; for (const auto &block:overlay.page().blocks) values.append(QJsonObject{{"id",block.id},{"text","本地翻译内容"}});
    overlay.setTranslationResult({{"blocks",values}}); QTest::qWait(20);
    QVERIFY(overlay.translatedImage()!=image); QCOMPARE(overlay.sourceImage(),image);
    QVERIFY(overlay.windowFlags()&Qt::FramelessWindowHint);
    auto panel=overlay.findChild<QPlainTextEdit *>("translationText"); QVERIFY(panel); QVERIFY(panel->isReadOnly()); QVERIFY(!panel->isVisible());
    overlay.setPanelVisible(true); QVERIFY(panel->isVisible());
    overlay.setShowingOriginal(true); QCOMPARE(panel->toPlainText(),overlay.page().text(true));
    overlay.setShowingOriginal(false); QCOMPARE(panel->toPlainText(),overlay.page().text(false));
    auto canvas=overlay.findChild<QWidget *>("translationCanvas"); QVERIFY(canvas);
    const auto point=canvas->mapFrom(&overlay,overlay.imageToView(overlay.page().blocks[0].bounds.center()).toPoint());
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,point);
    QCOMPARE(panel->textCursor().selectedText(),overlay.page().blocks[0].translated);
    QVERIFY(overlay.grab().save(artifact+"/translation-workspace.png"));
    QTest::keyClick(&overlay,Qt::Key_Escape); QVERIFY(!overlay.isVisible());
    QVERIFY(!overlay.findChild<LocalTranslationJob *>()->running());
    overlay.begin(image,false); parent.hide(); QVERIFY(!overlay.isVisible());
  }
  void workerEnglishChineseAndPreservedNumbers() {
    const auto value=worker(QJsonArray{QJsonObject{{"id",0},{"text","Save image"}},
        QJsonObject{{"id",1},{"text","The screenshot is translated locally. No internet connection is required."}},
        QJsonObject{{"id",2},{"text","2026/10/22 100.50"}},QJsonObject{{"id",3},{"text","保留中文原文"}}},"zh");
    QVERIFY2(!value.contains("error"),qPrintable(value["error"].toString()));
    const auto blocks=value["blocks"].toArray(); QCOMPARE(blocks.size(),4);
    QVERIFY(blocks[0].toObject()["text"].toString().contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
    QCOMPARE(blocks[2].toObject()["text"].toString(),QString("2026/10/22 100.50"));
    QCOMPARE(blocks[3].toObject()["text"].toString(),QString("保留中文原文"));
    QVERIFY(value["peak_working_set"].toDouble()>0);
    const auto english=worker(QJsonArray{QJsonObject{{"id",0},{"text","请先选择截图区域，然后保存图片。"}}},"en");
    QVERIFY2(!english.contains("error"),qPrintable(english["error"].toString()));
    const auto text=english["blocks"].toArray()[0].toObject()["text"].toString();
    QVERIFY2(text.contains("image",Qt::CaseInsensitive)||text.contains("picture",Qt::CaseInsensitive),qPrintable(text));
  }
  void nativeWorkerGenericCpu() {
    const auto savedIsa=qgetenv("CT2_FORCE_CPU_ISA"),savedRuy=qgetenv("RUY_PATHS");
    auto restore=qScopeGuard([&]{
      if(savedIsa.isNull())qunsetenv("CT2_FORCE_CPU_ISA");else qputenv("CT2_FORCE_CPU_ISA",savedIsa);
      if(savedRuy.isNull())qunsetenv("RUY_PATHS");else qputenv("RUY_PATHS",savedRuy);
    });
    qputenv("CT2_FORCE_CPU_ISA","GENERIC"); qputenv("RUY_PATHS","1");
    const auto value=worker(QJsonArray{QJsonObject{{"id",0},{"text","Open settings"}}},"zh","en");
    QVERIFY2(!value.contains("error"),qPrintable(value["error"].toString()));
    QVERIFY(!value["blocks"].toArray()[0].toObject()["text"].toString().isEmpty());
  }
  void shortUiLabelsUseSimplifiedChinese() {
    const auto value=worker(QJsonArray{QJsonObject{{"id",0},{"text","Settings"}},
        QJsonObject{{"id",1},{"text","Cancel"}},QJsonObject{{"id",2},{"text","Copy"}},
        QJsonObject{{"id",3},{"text","Close"}}},"zh","en");
    QVERIFY2(!value.contains("error"),qPrintable(value["error"].toString()));
    const auto blocks=value["blocks"].toArray(); QCOMPARE(blocks.size(),4);
    QCOMPARE(blocks[0].toObject()["text"].toString(),QString("设置"));
    QCOMPARE(blocks[1].toObject()["text"].toString(),QString("取消"));
    QCOMPARE(blocks[2].toObject()["text"].toString(),QString("复制"));
    QCOMPARE(blocks[3].toObject()["text"].toString(),QString("关闭"));
  }
  void workerPreservesTechnicalNamesInBothDirections() {
    const auto value=worker(QJsonArray{QJsonObject{{"id",0},{"text","HybridCLR"}},
        QJsonObject{{"id",1},{"text","HybridCLR is a native runtime implementation, so calling ..."}},
        QJsonObject{{"id",2},{"text","hybridclr is the core code of the interpreter part, including ..."}},
        QJsonObject{{"id",3},{"text","C++ implementation, deep integration with il2cpp and CLR."}},
        QJsonObject{{"id",4},{"text","https://www.hybridclr.cn"}}},"zh");
    QVERIFY2(!value.contains("error"),qPrintable(value["error"].toString()));
    const auto blocks=value["blocks"].toArray(); QCOMPARE(blocks.size(),5);
    QCOMPARE(blocks[0].toObject()["text"].toString(),QString("HybridCLR"));
    QVERIFY(blocks[0].toObject()["unchanged"].toBool());
    QVERIFY(blocks[1].toObject()["text"].toString().contains("HybridCLR"));
    QVERIFY(blocks[2].toObject()["text"].toString().contains("hybridclr"));
    const auto mixed=blocks[3].toObject()["text"].toString();
    QVERIFY2(mixed.contains("C++") && mixed.contains("il2cpp") && mixed.contains("CLR"),qPrintable(mixed));
    QVERIFY(mixed.contains(QRegularExpression("[\\x{4e00}-\\x{9fff}]")));
    QCOMPARE(blocks[4].toObject()["text"].toString(),QString("https://www.hybridclr.cn"));
    const auto english=worker(QJsonArray{QJsonObject{{"id",0},{"text","HybridCLR 使用 CLR 和 C++，请先安装 SDK。"}}},"en");
    QVERIFY2(!english.contains("error"),qPrintable(english["error"].toString()));
    const auto output=english["blocks"].toArray()[0].toObject()["text"].toString();
    for(const auto &term:{"HybridCLR","CLR","C++","SDK"})QVERIFY2(output.contains(term),qPrintable(output));
  }
  void cancellationCleansTemporaryText() {
    LocalTranslationJob job; QSignalSpy finished(&job,&LocalTranslationJob::finished);
    job.start(QJsonArray{QJsonObject{{"id",0},{"text","Translate this sentence."}}},"en","zh");
    QVERIFY(job.running()); const auto temporary=job.temporaryPath(); QVERIFY(QFile::exists(temporary+"/request.json"));
    job.cancel(); QVERIFY(!job.running()); QVERIFY(!QFile::exists(temporary)); QTest::qWait(30); QVERIFY(finished.isEmpty());
  }
  void languagePackIntegrityAndUnicodePaths() {
    QTemporaryDir dir; const auto modelRoot=dir.path()+"/中文路径 测试";
    QVERIFY(QDir().mkpath(modelRoot+"/en-zh"));
    QFile original(LocalTranslationJob::modelsPath()+"/models.json"); QVERIFY(original.open(QIODevice::ReadOnly));
    const auto models=QJsonDocument::fromJson(original.readAll()).object();
    QVERIFY(Store::writeJson(modelRoot+"/models.json",models));
    QFile corrupted(modelRoot+"/en-zh/model.bin"); QVERIFY(corrupted.open(QIODevice::WriteOnly)); corrupted.write("invalid"); corrupted.close();
    QVERIFY(Store::writeJson(dir.path()+"/中文请求.json",QJsonObject{{"source","en"},{"target","zh"},
        {"blocks",QJsonArray{QJsonObject{{"id",0},{"text","Save image"}}}}}));
    QProcess process;
    process.start(QCoreApplication::applicationDirPath()+"/FloatShotTranslate.exe",
        {"--input",dir.path()+"/中文请求.json","--output",dir.path()+"/中文结果.json","--models",modelRoot});
    QVERIFY(process.waitForFinished(20000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),1);
    QFile output(dir.path()+"/中文结果.json"); QVERIFY(output.open(QIODevice::ReadOnly));
    const auto value=QJsonDocument::fromJson(output.readAll()).object();
    QVERIFY(value.contains("error")); QVERIFY(!value.contains("blocks"));
    QVERIFY2(value["error"].toString().contains("语言包"),qPrintable(value["error"].toString()));
    output.close();
    // A complete model under a Chinese path must also load, not just the request.
    const auto metadata=models["models"].toObject()["en-zh"].toObject()["checksums"].toObject();
    for(auto it=metadata.begin();it!=metadata.end();++it) {
      const auto target=modelRoot+"/en-zh/"+it.key();
      if(QFile::exists(target))QVERIFY(QFile::remove(target));
      QVERIFY(QFile::copy(LocalTranslationJob::modelsPath()+"/en-zh/"+it.key(),target));
    }
    process.start(QCoreApplication::applicationDirPath()+"/FloatShotTranslate.exe",
        {"--input",dir.path()+"/中文请求.json","--output",dir.path()+"/中文结果.json","--models",modelRoot});
    QVERIFY(process.waitForFinished(60000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
    output.close(); QVERIFY(output.open(QIODevice::ReadOnly));
    const auto success=QJsonDocument::fromJson(output.readAll()).object();
    QVERIFY2(!success.contains("error"),qPrintable(success["error"].toString()));
  }
  void ocrTranslationAndImageExport() {
    const auto image=fixture(); PaddleOcrJob job; QSignalSpy completed(&job,&PaddleOcrJob::finished);
    job.start(image,"common",0,"cpu"); QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),60000);
    const auto ocr=completed.first().first().toJsonObject(); QVERIFY2(!ocr.contains("error"),qPrintable(ocr["error"].toString()));
    TranslationPage page; page.load(image,ocr); QVERIFY(page.blocks.size()>=5);
    const auto result=worker(page.request(),"zh"); QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    for(const auto &block:result["blocks"].toArray())page.apply(block.toObject());
    const auto translated=page.render(image); QVERIFY(translated!=image);
    QVERIFY(image.save(artifact+"/ocr-source.png")); QVERIFY(translated.save(artifact+"/ocr-translated.png"));
    QJsonObject detail{{"ocr",ocr},{"translation",result},{"paragraphs",page.request()}};
    QVERIFY(Store::writeJson(artifact+"/ocr-translation-report.json",detail));
    QVERIFY(Store::writeImage(artifact+"/translated-export.png",translated));
    QCOMPARE(QImage(artifact+"/translated-export.png"),translated);
  }
  void denseScreenshotPipeline() {
    QImage image(1920,1080,QImage::Format_ARGB32); image.fill(QColor("#fafafa"));
    QFont font("Microsoft YaHei UI"); font.setPixelSize(20);
    {
      QPainter p(&image); p.setFont(font); p.setPen(Qt::black);
      const QStringList sentences={"Select an area of the screen to create a screenshot.",
          "Translate the text locally and preserve the original layout.",
          "You can compare the result with the source image at any time.",
          "The image stays on this computer without an internet connection."};
      for(int column=0;column<2;++column) for(int row=0;row<28;++row)
        p.drawText(30+column*950,35+row*36,QString::number(row+1)+". "+sentences[row%sentences.size()]);
    }
    QElapsedTimer timer; timer.start();
    PaddleOcrJob job; QSignalSpy completed(&job,&PaddleOcrJob::finished);
    job.start(image,"common",0,"cpu"); QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),60000);
    const auto ocrMs=timer.elapsed(); const auto ocr=completed.first().first().toJsonObject();
    QVERIFY2(!ocr.contains("error"),qPrintable(ocr["error"].toString()));
    QVERIFY(ocr["lines"].toArray().size()>=40);
    TranslationPage page; page.load(image,ocr);
    const auto result=worker(page.request(),"zh");
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    const auto translationFinished=timer.elapsed();
    for(const auto &block:result["blocks"].toArray())page.apply(block.toObject());
    const auto output=page.render(image); QVERIFY(output!=image); QCOMPARE(output.size(),image.size());
    int clipped=0; for(const auto &block:page.blocks)if(block.clipped)++clipped;
    QVERIFY(Store::writeJson(artifact+"/dense-timing.json",QJsonObject{
        {"width",image.width()},{"height",image.height()},{"ocr_lines",ocr["lines"].toArray().size()},
        {"paragraphs",page.blocks.size()},{"ocr_wall_ms",ocrMs},{"translation_wall_ms",translationFinished-ocrMs},
        {"render_wall_ms",timer.elapsed()-translationFinished},{"total_wall_ms",timer.elapsed()},
        {"overflow_regions",clipped},{"ocr",QJsonObject{{"model",ocr["model"]},{"total_ms",ocr["total_ms"]},
           {"peak_working_set_mb",ocr["peak_working_set_mb"]}}},{"translation",result}}));
    QVERIFY(image.save(artifact+"/dense-source.png")); QVERIFY(output.save(artifact+"/dense-translated.png"));
  }
  void realWorkspacePipelineAndCache() {
    QTemporaryDir dir; Store store(dir.path()); store.config["ocr_acceleration"]="cpu";
    QWidget parent; parent.show(); ImageTranslationOverlay overlay(&parent,&store,[]{return QRect(100,100,1000,650);});
    const auto image=fixture(); overlay.begin(image,true);
    QTRY_VERIFY_WITH_TIMEOUT(overlay.hasCompletedTranslation(),30000);
    QVERIFY(overlay.displayedImage()!=image);
    overlay.setPanelVisible(true); QTest::qWait(50);
    QVERIFY(overlay.grab().save(artifact+"/real-translation-workspace.png"));
    overlay.setShowingOriginal(true); QCOMPARE(overlay.displayedImage(),image);
    overlay.setShowingOriginal(false); const auto result=overlay.displayedImage();
    overlay.finish(); overlay.begin(image,true);
    QVERIFY(overlay.hasCompletedTranslation()); QCOMPARE(overlay.displayedImage(),result);
    QVERIFY(!overlay.findChild<LocalTranslationJob *>()->running());
    overlay.finish();
  }
  void referenceScreenshotTranslation() {
    const auto file=qEnvironmentVariable("PICUT_TRANSLATION_REFERENCE_IMAGE");
    if(file.isEmpty())QSKIP("Set PICUT_TRANSLATION_REFERENCE_IMAGE to check a supplied screenshot.");
    const auto reportSize=report.size();
    const auto privateReport=qScopeGuard([&]{while(report.size()>reportSize)report.removeLast();});
    const QImage image(file); QVERIFY(!image.isNull());
    PaddleOcrJob job; QSignalSpy completed(&job,&PaddleOcrJob::finished);
    job.start(image,"common",0,"cpu"); QTRY_VERIFY_WITH_TIMEOUT(!completed.isEmpty(),30000);
    const auto ocr=completed.first().first().toJsonObject(); QVERIFY2(!ocr.contains("error"),qPrintable(ocr["error"].toString()));
    TranslationPage page; page.load(image,ocr); const auto result=worker(page.request(),"zh");
    QVERIFY2(!result.contains("error"),qPrintable(result["error"].toString()));
    for(const auto &value:result["blocks"].toArray()) {
      const auto block=value.toObject(); const auto original=page.blocks[block["id"].toInt()].original;
      if(original.contains("HybridCLR",Qt::CaseInsensitive))QVERIFY2(block["text"].toString().contains("HybridCLR",Qt::CaseInsensitive),qPrintable(block["text"].toString()));
      page.apply(block);
    }
    const auto translated=page.render(image);
    QVERIFY(translated!=image); QVERIFY(image.save(artifact+"/reference-source.png"));
    QVERIFY(translated.save(artifact+"/reference-translated.png"));
    QVERIFY(Store::writeJson(artifact+"/reference-translation-report.json",QJsonObject{{"ocr",ocr},{"translation",result}}));
  }
  void cleanupTestCase() { QVERIFY(Store::writeJson(artifact+"/translation-report.json",report)); }
};
QTEST_MAIN(TranslationTests)
#include "translation_tests.moc"
