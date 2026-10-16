#include <future>
#include "annotation.h"
#include "application.h"
#include "capture.h"
#include "pin.h"
#include "recognition.h"
#include "recorder.h"
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
#include <QtCore/QJsonArray>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>
#include <QtGui/QFontDatabase>
#include <QtGui/QImageReader>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QInputMethodEvent>
#include <QtWidgets/QGraphicsTextItem>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QScrollBar>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <winrt/base.h>

class NativeTests : public QObject {
  Q_OBJECT
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
    auto editor=text->resultEditor();
    QVERIFY(editor->isReadOnly());
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
    QVERIFY(!text->findChild<QToolButton *>("imageTextCopy")->isEnabled());
    QTest::keyClick(text,Qt::Key_Escape);
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
    auto temporary=job->temporaryPath(); QVERIFY(QFileInfo::exists(temporary));
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
    QTest::mouseClick(text.findChild<QToolButton *>("imageTextExpand"),Qt::LeftButton);
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
    overlay->selectRegion({160, 100, 480, 250});
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
  void toolbarMatchesProvidedReference() {
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    Annotation a(image);
    Toolbar bar;
    bar.sync(a);
    bar.show();
    QApplication::processEvents();
    for (auto button : bar.mainBar->findChildren<QToolButton *>()) {
      button->setAttribute(Qt::WA_UnderMouse, false);
      QCOMPARE(button->size(), QSize(32, 32));
      QCOMPARE(button->iconSize(), QSize(16, 16));
    }
    const auto rendered = bar.mainBar->grab().toImage();
    const QImage expected(":/fixtures/toolbar-reference.png");
    QVERIFY(!expected.isNull());
    // Number and image-text tools extend the unchanged original glyphs.
    QCOMPARE(rendered.size(), expected.size() + QSize(64, 0));
    const QList<int> insertedAt{bar.findChild<QToolButton *>("tool_number")->x(),
                                bar.findChild<QToolButton *>("action_select_text")->x()};
    int different = 0;
    for (int y = 0; y < expected.height(); ++y)
      for (int x = 0; x < expected.width(); ++x) {
        int actualX = x;
        for (int insertion : insertedAt)
          if (actualX >= insertion) actualX += 32;
        auto actual = rendered.pixelColor(actualX, y),
             target = expected.pixelColor(x, y);
        if (qAbs(actual.red() - target.red()) > 3 ||
            qAbs(actual.green() - target.green()) > 3 ||
            qAbs(actual.blue() - target.blue()) > 3)
          ++different;
      }
    QVERIFY(savePreview(bar.mainBar, "toolbar-reference-match.png"));
    QVERIFY2(different < expected.width() * expected.height() / 500,
             qPrintable(QString("Toolbar differs from reference at %1 pixels")
                            .arg(different)));
    QSignalSpy chosen(&bar, &Toolbar::toolChosen);
    QTest::mouseClick(bar.findChild<QToolButton *>("tool_pen"), Qt::LeftButton);
    QCOMPARE(chosen.takeFirst().first().toString(), QString("pen"));
    a.chooseTool("text");
    bar.sync(a);
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
    QVERIFY(bar->widthButton->styleSheet().contains("#2080f0"));
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
    QVERIFY(!imported.config["startup"].toBool());
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
    QCOMPARE(actions["record"]->text(), QString("区域录屏\tCtrl+Alt+R"));
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
