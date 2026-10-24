#include "application.h"
#include "menu.h"
#include "recording_settings.h"
#include "recording_markup.h"
#include "compact_ui.h"
#include "hotkey.h"
#include "panel_theme.h"
#include <future>
#include <iterator>
#include "native_windows.h"
#include "recognition.h"
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QLibrary>
#include <QtCore/QMimeData>
#include <QtCore/QSaveFile>
#include <QtCore/QScopeGuard>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QtMath>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QImageReader>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QShortcut>
#include <QtGui/QTextDocument>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

ApplicationController::ApplicationController(bool test)
    : QObject(), store(), pins(&store, this), testing(test) {
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
          [this] { ++clipboardPinRequest; });
  connect(&store, &Store::historyChanged, this, &ApplicationController::refreshHistory);
  connect(&store, &Store::ioFailed, this, [this](const QString &message) {
    if (!testing && !quitting) QMessageBox::warning(home, "保存失败", message);
  });
  tray.setIcon(picutIcon()); QApplication::setWindowIcon(picutIcon());
  tray.setToolTip("picut");
  trayMenu = new QMenu;
  trayMenu->setObjectName("trayMenu");
  auto add = [this](const QString &label, const QString &hotkey,
                   std::function<void()> callback) {
    auto action = trayMenu->addAction(label, this, std::move(callback));
    action->setProperty("base_label", label);
    action->setProperty("hotkey_action", hotkey);
  };
  add("打开工作台", {}, [this] { showHome(); });
  add("截图", "capture", [this] { beginCapture(); });
  add("贴图", "pin", [this] { pinClipboard(); });
  add("区域录屏", {}, [this] { beginCapture(true); });
  add("整屏录制", {}, [this] {
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    prepareRecord((screen ? screen : QGuiApplication::primaryScreen())->geometry());
  });
  add("暂停 / 继续录屏", "pause", [this] { if (recorder) recorder->pause(); });
  add("结束录屏", "stop", [this] { if (recorder) recorder->stop(); });
  trayMenu->addSeparator();
  add("显示 / 隐藏贴图", "toggle_pins", [this] { pins.toggleAll(); });
  trayMenu->addAction("恢复最近关闭的贴图", &pins, &PinManager::restoreClosed);
  add("切换鼠标穿透", "through", [this] { pins.toggleThrough(); });
  trayMenu->addAction("解除鼠标穿透", &pins, &PinManager::clearThrough);
  trayMenu->addAction("设置", this, &ApplicationController::openSettings);
  trayMenu->addSeparator();
  trayMenu->addAction("退出", this, &ApplicationController::quit);
  connect(trayMenu, &QMenu::aboutToShow, this, &ApplicationController::updateTrayMenu);
  updateTrayMenu();
  tray.setContextMenu(trayMenu);
  connect(&tray, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger)
              beginCapture();
            else if (reason == QSystemTrayIcon::DoubleClick)
              showHome();
            else if (reason == QSystemTrayIcon::MiddleClick)
              pinClipboard();
          });
  pins.output = [this](PinWindow *pin, QString action) {
    imageOutput(pin->displayedImage(), pin->contentGeometry(), action);
  };
  if (!testing) {
    tray.show();
    registerHotkeys();
    if (store.config["restore_pins"].toBool(true))
      pins.restore();
  }
  QApplication::instance()->installNativeEventFilter(this);
}
ApplicationController::~ApplicationController() {
  quitting = true;
  if (recordCountdown) delete recordCountdown;
  QObject::disconnect(&store, nullptr, this, nullptr);
  for (int id : hotkeyIds)
    UnregisterHotKey(nullptr, id);
  QApplication::instance()->removeNativeEventFilter(this);
  pins.persist();
  store.flushIo();
  for (auto pin : pins.windows)
    if (pin)
      pin->close();
  if (overlay)
    overlay->close();
  delete trayMenu;
}
void ApplicationController::updateTrayMenu() {
  auto bindings = store.config["hotkeys"].toObject();
  for (auto action : trayMenu->actions()) {
    auto label = action->property("base_label").toString();
    if (label.isEmpty())
      continue;
    auto key = action->property("hotkey_action").toString();
    action->setText(menuLabel(label, key.isEmpty() ? QString() : bindings[key].toString()));
    if (key == "pause" || key == "stop")
      action->setEnabled(recording() && !recorder->finalizing());
  }
  if (home) {
    for (auto button : home->findChildren<QPushButton *>()) {
      const auto key = button->property("hotkey_action").toString();
      if (key.isEmpty()) continue;
      const auto label = button->property("base_label").toString();
      const auto shortcut = bindings[key].toString();
      button->setText(label + (shortcut.isEmpty() ? QString() : " · " + shortcut));
    }
    home->statusBar()->showMessage(QString("%1 截图 / 录屏选区 · %2 贴图")
        .arg(bindings["capture"].toString(), bindings["pin"].toString()));
  }
}
void ApplicationController::registerHotkeys() {
  updateTrayMenu();
  for (int id : hotkeyIds)
    UnregisterHotKey(nullptr, id);
  hotkeyIds.clear();
  auto bindings = store.config["hotkeys"].toObject();
  QStringList actions{"capture", "pin",   "toggle_pins", "through",
                      "",  "pause", "stop"};
  QStringList failures;
  for (int n = 0; n < actions.size(); ++n) {
    const auto value = bindings[actions[n]].toString();
    if (value.isEmpty()) continue;
    UINT modifiers = 0, vk = 0;
    const bool supported = nativeHotkey(hotkeySequence(value), modifiers, vk);
    if (supported && RegisterHotKey(nullptr, n + 1, modifiers, vk))
      hotkeyIds.append(n + 1);
    else {
      const auto error = supported ? GetLastError() : ERROR_INVALID_PARAMETER;
      failures.append(value);
      qWarning().noquote() << "Hotkey registration failed:" << actions[n]
                           << value << "Windows error:" << error;
    }
  }
  if (!failures.isEmpty())
    notify("快捷键冲突：" + failures.join("、"));
}
bool ApplicationController::nativeEventFilter(const QByteArray &, void *raw,
                                              qintptr *) {
  auto message = static_cast<MSG *>(raw);
  if (message->message == WM_HOTKEY) {
    if (message->wParam < 1 || message->wParam > 7 || message->wParam==5) return false;
    if (hotkeysSuspended) return true;
    switch (message->wParam) {
    case 1:
      beginCapture();
      break;
    case 2:
      qInfo() << "Global pin hotkey received";
      pinClipboard();
      break;
    case 3:
      pins.toggleAll();
      break;
    case 4:
      pins.toggleThrough();
      break;
    case 6:
      if (recorder)
        recorder->pause();
      break;
    case 7:
      if (recorder)
        recorder->stop();
      break;
    }
    return true;
  }
  return false;
}
void ApplicationController::notify(QString text) {
  if (!store.config["notifications_enabled"].toBool(false))
    return;
  if (home && home->isVisible())
    home->statusBar()->showMessage(text, 12000);
  else
    tray.showMessage("picut", text, QSystemTrayIcon::Information, 4000);
}
void ApplicationController::showHome() {
  if (!home) {
    home = new QMainWindow;
    home->setAttribute(Qt::WA_DeleteOnClose);
    home->setWindowTitle("picut · " + QCoreApplication::applicationVersion());
    applyPanelTheme(home);
    home->resize(880, 630);
    auto content = new QWidget;
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(28, 25, 28, 20);
    auto title = new QLabel("把灵感留在屏幕上");
    title->setStyleSheet("font-size:27px;font-weight:bold;");
    layout->addWidget(title);
    layout->addWidget(new QLabel("截图、标注、贴图与录屏"));
    layout->addSpacing(20);
    auto row = new QHBoxLayout;
    for (const auto &pair : QList<QPair<QString, std::function<void()>>>{
             {"截图与标注", [this] { beginCapture(); }},
             {"剪贴板贴图", [this] { pinClipboard(); }},
             {"区域录屏", [this] { beginCapture(true); }},
             {"设置", [this] { openSettings(); }}}) {
      auto b = new QPushButton(pair.first);
      const auto key = QMap<QString, QString>{{"截图与标注", "capture"}, {"剪贴板贴图", "pin"}}.value(pair.first);
      if (!key.isEmpty()) { b->setProperty("hotkey_action", key); b->setProperty("base_label", pair.first); }
      b->setMinimumHeight(60);
      row->addWidget(b);
      connect(b, &QPushButton::clicked, this, pair.second);
    }
    layout->addLayout(row);
    layout->addSpacing(20);
    layout->addWidget(new QLabel("历史记录"));
    historyList = new QListWidget;
    historyList->setIconSize({96, 60});
    layout->addWidget(historyList);
    connect(historyList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *row) {
              auto item = row->data(Qt::UserRole).toJsonObject();
              if (item["kind"] == "video")
                previewVideo(item["path"].toString());
              else
                pins.create(QImage(item["path"].toString()));
            });
    auto actions = new QHBoxLayout;
    for (auto name : QStringList{"复制", "贴图", "打开目录", "移除记录"}) {
      auto b = new QPushButton(name);
      actions->addWidget(b);
      connect(b, &QPushButton::clicked, this, [this, name] {
        auto row = historyList->currentItem();
        if (!row)
          return;
        auto item = row->data(Qt::UserRole).toJsonObject();
        auto path = item["path"].toString();
        if (name == "移除记录") {
          store.removeRecord(item["id"].toString());
          refreshHistory();
        } else if (name == "打开目录")
          QDesktopServices::openUrl(
              QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        else if (name == "贴图" && item["kind"] == "image")
          pins.create(QImage(path));
        else if (name == "复制") {
          if (item["kind"] == "image")
            QApplication::clipboard()->setImage(QImage(path));
          else {
            auto mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(path)});
            QApplication::clipboard()->setMimeData(mime);
          }
        }
      });
    }
    layout->addLayout(actions);
    layout->addWidget(new QLabel("关闭工作台后常驻托盘 · 退出请使用托盘菜单"));
    home->setCentralWidget(content);
    historyDirty = true;
  }
  home->show();
  updateTrayMenu();
  if (historyDirty) refreshHistory();
  home->raise();
  home->activateWindow();
}
void ApplicationController::refreshHistory() {
  historyDirty = true;
  if (!historyList || !home || !home->isVisible())
    return;
  historyDirty = false;
  historyList->clear();
  for (const auto &value : store.history) {
    auto item = value.toObject();
    auto file = item[item["kind"] == "video" ? "thumbnail" : "path"].toString();
    auto key = item["id"].toString() + "|" + file;
    auto thumbnail = historyThumbnails.object(key);
    if (!thumbnail) {
      QImageReader reader(file);
      auto size = reader.size();
      size.scale(96, 60, Qt::KeepAspectRatio);
      reader.setScaledSize(size);
      ++historyThumbnailReads;
      auto image = reader.read();
      thumbnail = new QPixmap(QPixmap::fromImage(image));
      historyThumbnails.insert(key, thumbnail);
    }
    auto row =
        new QListWidgetItem(QIcon(*thumbnail),
                            QString("%1 · %2\n%3 × %4")
                                .arg(item["kind"] == "video" ? "录屏" : "截图",
                                     item["created"].toString())
                                .arg(item["width"].toInt())
                                .arg(item["height"].toInt()));
    row->setData(Qt::UserRole, item);
    historyList->addItem(row);
  }
}
void ApplicationController::beginCapture(bool recordingMode) {
  if (recordSetup)
    return;
  if (recording()) {
    if (recordOverlay)
      recordOverlay->show();
    notify("正在录屏，请先停止录制。");
    return;
  }
  if (overlay) {
    if (overlay->scrollCapture) overlay->scrollCapture->raise();
    else overlay->raise();
    return;
  }
  if (pending)
    return;
  ++clipboardPinRequest;
  pending = true;
  bool visible = home && home->isVisible();
  if (home)
    home->hide();
  int delay = visible ? 130 : 0;
  if (!recordingMode)
    delay += store.config["capture_delay"].toInt() * 1000;
  QTimer::singleShot(delay, this, [this, recordingMode] {
    pending = false;
    try {
      overlay = new CaptureOverlay(&store, recordingMode);
      overlay->output = [this](const QImage &image, const QRect &region,
                               const QString &action) {
        return imageOutput(image, region, action);
      };
      connect(overlay, &CaptureOverlay::finished, this, [this] {
        overlay = nullptr;
      });
      connect(overlay, &CaptureOverlay::recordRegion, this,
              [this](QRect region) {
                auto source=overlay;
                auto painter=source && source->annotation?recordingPainter(*source->annotation,physicalRect(region).size()):RecordingPainter();
                prepareRecord(region,false,std::move(painter),source);
              });
      overlay->show();
      overlay->activateWindow();
      overlay->setFocus();
    } catch (const std::exception &e) {
      QMessageBox::warning(nullptr, "无法截图", QString::fromUtf8(e.what()));
    }
  });
}
bool ApplicationController::imageOutput(const QImage &image, QRect region,
                                        QString action) {
  if (image.isNull())
    return false;
  if (action == "edit") {
    auto pin = pins.create(image);
    pin->startEditing();
    return true;
  }
  if (action == "pin") {
    pins.create(image, region);
    auto output = store.config["auto_save"].toBool()
        ? store.reserveOutputPath(store.config["image_dir"].toString(), "截图", "png") : QString();
    store.addImageAsync(image, output);
    notify("已贴到桌面");
    return true;
  }
  QString output;
  if (action == "save") {
    auto path =
        store.outputPath(store.config["image_dir"].toString(), "截图", "png");
    output = QFileDialog::getSaveFileName(
        nullptr, "保存截图", path,
        "PNG 图片 (*.png);;JPEG 图片 (*.jpg);;WebP 图片 (*.webp)");
    if (output.isEmpty())
      return false;
    if (QFileInfo(output).suffix().isEmpty())
      output += ".png";
  } else if (action == "quick_save")
    output =
        store.outputPath(store.config["image_dir"].toString(), "截图", "png");
  else if (action == "copy") {
    if (store.config["copy_image_as_file"].toBool()) {
      auto file = store.path("history/clipboard.png");
      image.save(file);
      auto mime = new QMimeData;
      mime->setImageData(image);
      mime->setUrls({QUrl::fromLocalFile(file)});
      QApplication::clipboard()->setMimeData(mime);
    } else
      QApplication::clipboard()->setImage(image);
  }
  if (store.config["auto_save"].toBool() && output.isEmpty())
    output =
        store.outputPath(store.config["image_dir"].toString(), "截图", "png");
  if (!output.isEmpty() && !image.save(output)) {
    QMessageBox::warning(nullptr, "保存失败",
                         "无法保存图片，请检查目录权限。原选区已保留。");
    return false;
  }
  try {
    store.addImage(image, output);
  } catch (const std::exception &e) {
    QMessageBox::warning(nullptr, "历史保存失败", QString::fromUtf8(e.what()));
    return false;
  }
  refreshHistory();
  notify(action == "pin"    ? "已贴到桌面"
         : action == "copy" ? "截图已复制"
                            : "截图已保存");
  return true;
}
void ApplicationController::pinClipboard() {
  const auto request = ++clipboardPinRequest;
  if (overlay) {
    overlay->complete("pin");
    return;
  }
  if (auto translation=qobject_cast<ImageTranslationOverlay *>(QApplication::activeModalWidget())) {
    const auto image=translation->displayedImage();
    if (!image.isNull() && imageOutput(image,translation->sourceGlobalRect(),"pin")) translation->finish();
    return;
  }
  qInfo() << "Clipboard pin requested";
  tryPinClipboard(request, QGuiApplication::platformName() == "windows"
                               ? GetClipboardSequenceNumber() : 0, 0);
}
void ApplicationController::tryPinClipboard(quint64 request, quint32 sequence,
                                           int attempt) {
  // Never complete an old request after a new copy, a second F3 or a capture.
  const bool nativeClipboard = QGuiApplication::platformName() == "windows";
  if (quitting || request != clipboardPinRequest || overlay || pending ||
      hotkeysSuspended || (nativeClipboard && sequence != GetClipboardSequenceNumber()))
    return;
  auto retry = [this, request, sequence, attempt] {
    static constexpr int delays[] = {40, 100, 200, 400};
    if (attempt >= int(std::size(delays))) {
      qWarning() << "Clipboard image unavailable; pin retry limit reached";
      notify("暂时无法读取剪贴板图片，请重新复制后再贴图。");
      return;
    }
    QTimer::singleShot(delays[attempt], this, [this, request, sequence, attempt] {
      tryPinClipboard(request, sequence, attempt + 1);
    });
  };
  // An OpenClipboard(nullptr) lock has no HWND, so GetOpenClipboardWindow alone
  // cannot distinguish it from an unlocked clipboard. Probe without reading or
  // changing any content, then release immediately before Qt/OLE reads it.
  if (nativeClipboard) {
    if (!OpenClipboard(nullptr)) { retry(); return; }
    SetLastError(ERROR_SUCCESS);
    const int formats = CountClipboardFormats();
    const auto error = GetLastError();
    CloseClipboard();
    if (!formats && error != ERROR_SUCCESS) { retry(); return; }
    if (!formats) { pins.restoreClosed(); return; }
  }
  auto clipboard = QApplication::clipboard();
  auto mime = clipboard->mimeData();
  if (!mime || mime->formats().isEmpty()) {
    if (nativeClipboard) retry();
    else pins.restoreClosed();
    return;
  }
  const bool hasImage = mime->hasImage();
  // Read the file list before image conversion; an empty image must not prevent
  // the local-file fallback (including picut's own "copy as file" format).
  const auto urls = mime->urls();
  QImage image = hasImage ? clipboard->image() : QImage();
  if (image.isNull()) {
    for (auto url : urls)
      if (url.isLocalFile()) {
        image.load(url.toLocalFile());
        if (!image.isNull()) break;
      }
  }
  // A successful delayed render can advance the native sequence during image().
  // The sequence guard belongs before a retry; accept the image we just read.
  if (request != clipboardPinRequest)
    return;
  if (!image.isNull()) {
    if (pins.create(image)) {
      qInfo() << "Clipboard image pin created; read attempts:" << attempt + 1;
      notify("已贴到桌面");
    }
    return;
  }
  if (hasImage ||
      (nativeClipboard &&
       (GetOpenClipboardWindow() || IsClipboardFormatAvailable(CF_DIB) ||
        IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_BITMAP)))) {
    retry();
    return;
  }
  auto text = mime->hasText() ? mime->text() : QString();
  if (text.isEmpty()) {
    pins.restoreClosed();
    return;
  }
  QColor color(text.trimmed());
  if (color.isValid()) {
    image = QImage(300, 180, QImage::Format_ARGB32);
    image.fill(color);
    QPainter p(&image);
    p.fillRect(0, 130, 300, 50, QColor(0, 0, 0, 150));
    p.setPen(Qt::white);
    p.drawText(QRect(0, 130, 300, 50), Qt::AlignCenter, color.name().toUpper());
  } else {
    QTextDocument doc;
    doc.setDefaultFont(QFont("Microsoft Yahei UI", 12));
    if (mime->hasHtml())
      doc.setHtml(mime->html());
    else
      doc.setPlainText(text);
    doc.setTextWidth(480);
    image = QImage(500, qMax(60, qCeil(doc.size().height()) + 20),
                   QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    p.translate(10, 10);
    doc.drawContents(&p);
  }
  pins.create(image);
  notify("剪贴板内容已贴到桌面");
}
void ApplicationController::openSettings() {
  if (hotkeysSuspended) return;
  ++clipboardPinRequest;
  hotkeysSuspended = true;
  for (int id : hotkeyIds) UnregisterHotKey(nullptr, id);
  hotkeyIds.clear();
  auto resumeHotkeys = qScopeGuard([this] {
    hotkeysSuspended = false;
    if (!testing) registerHotkeys();
  });
  QDialog dialog(home);
  dialog.setObjectName("settingsDialog");
  dialog.setWindowTitle("picut · 设置");
  applyPanelTheme(&dialog);
  dialog.resize(640, 560);
  auto root = new QVBoxLayout(&dialog);
  auto tabs = new QTabWidget;
  root->addWidget(tabs);
  auto general = new QWidget;
  auto form = new QFormLayout(general);
  QMap<QString, QCheckBox *> checks;
  for (const auto &pair : QList<QPair<QString, QString>>{
           {"restore_pins", "启动时恢复贴图"},
           {"auto_save", "完成截图后自动保存"},
           {"notifications_enabled", "显示操作通知（截图、贴图、保存等）"},
           {"confirm_capture_exit", "已有标注时，退出截图前提示确认"},
           {"magnifier_enabled", "自动显示截图放大镜"},
           {"capture_cursor", "截图包含鼠标指针"},
           {"copy_image_as_file", "复制图片时同时复制图片文件"}}) {
    auto check = new QCheckBox(pair.second);
    check->setChecked(store.config[pair.first].toBool());
    form->addRow(check);
    checks[pair.first] = check;
  }
  QMap<QString, QSpinBox *> spins;
  for (const auto &pair :
       QList<QPair<QString, QString>>{{"history_limit", "历史记录数量"},
                                      {"cache_limit_mb", "缓存上限（MB）"},
                                      {"closed_pin_limit", "可恢复贴图数量"},
                                      {"capture_delay", "截图延时（秒）"}}) {
    auto spin = new QSpinBox;
    spin->setRange(
        pair.first == "history_limit" || pair.first == "cache_limit_mb" ? 1 : 0,
        pair.first == "cache_limit_mb" ? 10240 : 1000);
    spin->setValue(store.config[pair.first].toInt());
    form->addRow(pair.second, spin);
    spins[pair.first] = spin;
  }
  QMap<QString, QLineEdit *> paths;
  for (auto pair : QList<QPair<QString, QString>>{{"image_dir", "图片目录"},
                                                  {"video_dir", "视频目录"}}) {
    auto row = new QWidget;
    auto layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto edit = new QLineEdit(store.config[pair.first].toString());
    paths[pair.first] = edit;
    auto browse = new QPushButton("浏览");
    layout->addWidget(edit);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, edit] {
      auto selected =
          QFileDialog::getExistingDirectory(&dialog, "保存目录", edit->text());
      if (!selected.isEmpty())
        edit->setText(selected);
    });
    form->addRow(pair.second, row);
  }
  tabs->addTab(general, "常规与存储");
  auto hotkeys = new QWidget;
  auto hotkeyForm = new QFormLayout(hotkeys);
  QMap<QString, HotkeyEdit *> bindings;
  for (const auto &pair :
       QList<QPair<QString, QString>>{{"capture", "截图 / 录屏选区"},
                                      {"pin", "贴图"},
                                      {"toggle_pins", "隐藏 / 显示贴图"},
                                      {"through", "鼠标穿透"},
                                      {"pause", "暂停 / 继续"},
                                      {"stop", "停止录屏"}}) {
    auto edit = new HotkeyEdit(
        store.config["hotkeys"].toObject()[pair.first].toString());
    edit->setObjectName("hotkey_" + pair.first);
    bindings[pair.first] = edit;
    hotkeyForm->addRow(pair.second, edit);
  }
  auto hotkeyHelp = new QLabel("点击快捷键框后直接按组合键，按 Tab 切换下一项；点击 × 可停用。设置期间暂停全局热键，保存或取消后恢复。");
  hotkeyHelp->setWordWrap(true); hotkeyForm->addRow(hotkeyHelp);
  auto hotkeyError = new QLabel;
  hotkeyError->setObjectName("hotkeyError"); hotkeyError->setWordWrap(true);
  hotkeyError->setStyleSheet("color:#b00020;"); hotkeyError->hide();
  hotkeyForm->addRow(hotkeyError);
  for (auto edit : bindings) connect(edit,&QKeySequenceEdit::keySequenceChanged,hotkeyError,&QWidget::hide);
  tabs->addTab(hotkeys, "全局快捷键");
  auto ocr = new QWidget;
  auto ocrForm = new QFormLayout(ocr);
  auto ocrEngine = new QLabel("PaddleOCR · Small（离线识别）");
  auto ocrProfile = new QComboBox;
  auto ocrModels = PaddleOcrJob::profiles();
  for (auto it = ocrModels.begin(); it != ocrModels.end(); ++it)
    ocrProfile->addItem(it.value().toObject()["label"].toString(), it.key());
  ocrProfile->setCurrentIndex(qMax(0, ocrProfile->findData(store.config["ocr_profile"].toString("common"))));
  ocrForm->addRow("识别器", ocrEngine);
  ocrForm->addRow("文字语言组", ocrProfile);
  auto ocrAcceleration=new QComboBox;
  ocrAcceleration->addItem("自动 · 大图显卡加速","auto");
  ocrAcceleration->addItem("CPU","cpu");
  ocrAcceleration->addItem("显卡","gpu");
  ocrAcceleration->setCurrentIndex(qMax(0,ocrAcceleration->findData(store.config["ocr_acceleration"].toString("auto"))));
  ocrForm->addRow("运算设备",ocrAcceleration);
  auto ocrHelp = new QLabel("所有识别都在本机完成，不上传截图。PaddleOCR 使用 Small 模型，按需启动，识别结束后退出释放内存。支持中简繁、英文、日文及拉丁字母语种；结果不会自动纠正或猜测数字。");
  ocrHelp->setWordWrap(true); ocrForm->addRow(ocrHelp);
  tabs->addTab(ocr, "文字识别");
  auto buttons =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Save)->setText("保存设置");
  buttons->button(QDialogButtonBox::Cancel)->setText("取消");
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    QSet<quint64> used;
    for (auto edit : bindings) {
      const auto sequence = edit->keySequence();
      if (sequence.isEmpty()) continue;
      UINT modifiers = 0, vk = 0;
      const bool valid = nativeHotkey(sequence, modifiers, vk);
      const quint64 signature = (quint64(modifiers) << 32) | vk;
      if (!valid || used.contains(signature)) {
        hotkeyError->setText(valid ? "快捷键重复，请为不同操作选择不同组合键。" : "此按键不能用作全局快捷键，请重新按下组合键。");
        hotkeyError->show(); tabs->setCurrentWidget(hotkeys); edit->setFocus(); return;
      }
      used.insert(signature);
    }
    dialog.accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  auto config = store.config;
  for (auto i = checks.begin(); i != checks.end(); ++i)
    config[i.key()] = i.value()->isChecked();
  for (auto i = spins.begin(); i != spins.end(); ++i)
    config[i.key()] = i.value()->value();
  for (auto i = paths.begin(); i != paths.end(); ++i)
    config[i.key()] = i.value()->text();
  QJsonObject keys;
  for (auto i = bindings.begin(); i != bindings.end(); ++i)
    keys[i.key()] = i.value()->keySequence().toString(QKeySequence::PortableText);
  config["hotkeys"] = keys;
  config["ocr_engine"] = "paddle";
  config["ocr_profile"] = ocrProfile->currentData().toString();
  config["ocr_acceleration"]=ocrAcceleration->currentData().toString();
  store.config = config;
  store.saveConfig();
  updateTrayMenu();
  if (home && !config["notifications_enabled"].toBool())
    home->statusBar()->clearMessage();
  notify("设置已保存");
}
void ApplicationController::makeRecorder() {
  if (recorder)
    return;
  recorder = std::make_unique<Recorder>(store.path("recording"));
  connect(recorder.get(), &Recorder::warning, this,
          &ApplicationController::notify);
  connect(recorder.get(), &Recorder::status, this,
          &ApplicationController::notify);
  connect(recorder.get(), &Recorder::failed, this, [this](QString text) {
    recordOverlay.reset();
    const auto source=recordSource;
    restoreRecordSource();
    QMessageBox::warning(source?static_cast<QWidget *>(source.data()):home.data(), "录屏未完成", text);
  });
  connect(recorder.get(), &Recorder::started, this, [this] {
    const auto source=recordSource; recordSource=nullptr;
    if (source) source->close();
  });
  connect(recorder.get(), &Recorder::finished, this, [this](QJsonObject item) {
    recordOverlay.reset();
    item["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    store.addVideo(item);
    refreshHistory();
    notify("录屏已保存");
    previewVideo(item["path"].toString());
  });
}
void ApplicationController::restoreRecordSource() {
  const auto source=recordSource; recordSource=nullptr;
  if (!source || quitting) return;
  source->setRecordingMode(false); source->show(); source->raise(); source->activateWindow(); source->setFocus();
}
void ApplicationController::prepareRecord(QRect region, bool showSettings,
                                           RecordingPainter painter, CaptureOverlay *source) {
  if (recording() || recordSetup || pending || (overlay && overlay!=source)) return;
  recordSource=source;
  auto screen=QGuiApplication::screenAt(region.center());
  if (!screen || !screen->geometry().contains(region)) {
    restoreRecordSource();
    QMessageBox::warning(source?static_cast<QWidget *>(source):home.data(),"录制区域","录屏区域需要位于一个显示器内。"); return;
  }
  recordSetup=true;
  if (showSettings) {
    RecordingSettingsDialog dialog(&store,region,true,home);
    if (dialog.exec()!=QDialog::Accepted) { recordSetup=false; restoreRecordSource(); return; }
  }
  auto options=savedRecordingOptions(store.config,physicalRect(region),
      store.outputPath(store.config["video_dir"].toString(),"录屏","mp4"));
  options.painter=std::move(painter);
  makeRecorder();
  int seconds=qBound(0,store.config["countdown"].toInt(3),10);
  auto counter=new QLabel(nullptr,Qt::FramelessWindowHint|Qt::Tool|Qt::WindowStaysOnTopHint);
  recordCountdown=counter; counter->setObjectName("recordCountdownWindow");
  counter->setAttribute(Qt::WA_DeleteOnClose); counter->setAlignment(Qt::AlignCenter);
  counter->setStyleSheet("background:rgba(0,0,0,170);color:white;font-size:60px;");
  counter->setGeometry(QRect(region.center()-QPoint(60,60),QSize(120,120)));
  connect(counter,&QObject::destroyed,this,[this] {
    if (recordSetup) { recordOverlay.reset(); recordSetup=false; restoreRecordSource(); }
  });
  auto escape=new QShortcut(QKeySequence(Qt::Key_Escape),counter);
  escape->setContext(Qt::ApplicationShortcut);
  connect(escape,&QShortcut::activated,counter,&QWidget::close);
  try {
    recordOverlay=std::make_unique<RecordingOverlay>(recorder.get(),region,options.systemAudio,options.microphone);
    if (source && source->annotation && options.painter) {
      QImage artwork(options.region.size(),QImage::Format_ARGB32_Premultiplied);
      artwork.fill(Qt::transparent); options.painter(artwork); recordOverlay->setArtwork(artwork);
    }
    recordOverlay->setCountdown(seconds);
    connect(recordOverlay.get(),&RecordingOverlay::countdownCanceled,counter,&QWidget::close);
    if (!recordOverlay->show()) throw std::runtime_error("无法从录制画面排除控制条。请在 Windows 10 2004 或更新版本的本地桌面会话中录制。");
  } catch (const std::exception &e) {
    recordOverlay.reset(); recordSetup=false; counter->close();
    restoreRecordSource();
    QMessageBox::warning(source?static_cast<QWidget *>(source):home.data(),"无法录屏",QString::fromUtf8(e.what())); return;
  }
  auto start=[this,counter,options] {
    recordSetup=false; counter->close();
    try { recordOverlay->setCountdown(-1); recorder->start(options); }
    catch (const std::exception &e) {
      recordOverlay.reset(); restoreRecordSource();
      QMessageBox::warning(overlay?static_cast<QWidget *>(overlay.data()):home.data(),"无法录屏",QString::fromUtf8(e.what()));
    }
  };
  if (!seconds) { start(); return; }
  counter->setText(QString::number(seconds)); counter->show(); counter->activateWindow();
  excludeCapture(counter);
  auto timer=new QTimer(counter); timer->setInterval(1000);
  connect(timer,&QTimer::timeout,counter,[this,counter,timer,start,seconds]() mutable {
    --seconds;
    if (seconds<=0) { timer->stop(); start(); }
    else { counter->setText(QString::number(seconds)); recordOverlay->setCountdown(seconds); }
  });
  timer->start();
}
void ApplicationController::quit() {
  if (recording()) {
    QMessageBox::information(home, "录制进行中",
                             "请先停止录制并等待保存完成。");
    return;
  }
  quitting = true;
  pins.persist();
  QApplication::quit();
}
bool verifyOcrMemory(ApplicationController &, const QString &folder) {
  QJsonArray stages{QJsonObject{{"stage", "startup"}, {"memory", memoryUsage()}}};
  QImage image(660,120,QImage::Format_RGB32); image.fill(Qt::white);
  { QPainter p(&image); QFont font("Microsoft YaHei UI"); font.setPixelSize(32);
    p.setFont(font); p.setPen(Qt::black); p.drawText(24,75,"中文截图识别 0123456789"); }
  stages.append(QJsonObject{{"stage","input_prepared"},{"memory",memoryUsage()}});
  QJsonArray runs; bool passed=true;
  for (int i=0;i<3;++i) {
    PaddleOcrJob job; QEventLoop loop; QJsonObject result;
    QObject::connect(&job,&PaddleOcrJob::finished,&loop,[&](QJsonObject value){ result=value; loop.quit(); });
    QTimer sampler; sampler.setInterval(50);
    QObject::connect(&sampler,&QTimer::timeout,&loop,[&]{ stages.append(QJsonObject{{"stage",QString("during_%1").arg(i+1)},{"memory",memoryUsage()}}); });
    sampler.start(); job.start(image); if (job.running()) loop.exec(); sampler.stop();
    passed &= !result.contains("error") && result["text"].toString().contains("0123456789") && !job.running();
    runs.append(result);
    stages.append(QJsonObject{{"stage",QString("after_%1").arg(i+1)},{"memory",memoryUsage()}});
  }
  bool loaded = GetModuleHandleW(L"onnxruntime.dll") != nullptr;
  passed &= !loaded;
  return Store::writeJson(folder+"/ocr-memory-report.json",QJsonObject{{"passed",passed},{"onnx_loaded_in_main",loaded},
    {"synthetic_only",true},{"stages",stages},{"runs",runs}}) && passed;
}
bool verifyNativeFeatures(ApplicationController &controller,
                          const QString &folder, bool memory) {
  QJsonArray stages;
  stages.append(QJsonObject{{"stage", "startup"}, {"memory", memoryUsage()}});
  QImage image(1920, 1080, QImage::Format_ARGB32);
  image.fill(Qt::white);
  int count = memory ? 20 : 2;
  for (int n = 0; n < count; ++n) {
    DesktopSnapshot desktop;
    desktop.bounds = {0, 0, 3840, 1080};
    desktop.frames = {{QRect(0, 0, 1920, 1080), image},
                      {QRect(1920, 0, 1920, 1080), image.copy()}};
    auto selector =
        new CaptureOverlay(&controller.store, false, std::move(desktop));
    selector->selectRegion({100, 100, 1200, 700});
    selector->chooseTool("arrow");
    auto mark = selector->annotation->preferences;
    mark.points = {{40, 80}, {300, 80}};
    selector->annotation->add(mark);
    for (auto head : QStringList{"tapered", "filled", "hollow"}) {
      selector->annotation->preferences.arrowHead = head;
      selector->annotation->applyPreferences();
    }
    if (paintedHit(selector->annotation->marks.first(), {150, 80}))
      return false;
    selector->output = [](const QImage &, const QRect &, const QString &) {
      return true;
    };
    selector->complete("copy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    if (n < 2 || n == count - 1)
      stages.append(
          QJsonObject{{"stage", QString("after_capture_%1").arg(n + 1)},
                      {"memory", memoryUsage()}});
  }
  auto pin = controller.pins.create(image, QRect(100, 100, 1920, 1080));
  if (pin->contentGeometry() != QRect(100, 100, 1920, 1080))
    return false;
  auto second =
      controller.pins.create(image.copy(), QRect(2020, 100, 1920, 1080));
  QApplication::processEvents();
  stages.append(QJsonObject{{"stage", "two_pins"}, {"memory", memoryUsage()}});
  controller.pins.handle(pin, "destroy");
  controller.pins.handle(second, "destroy");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  stages.append(
      QJsonObject{{"stage", "pins_destroyed"}, {"memory", memoryUsage()}});
  QJsonObject report{
      {"passed", true},
      {"runtime", "C++ / Qt Widgets"},
      {"python_dependency", false},
      {"synthetic_only", true},
      {"stages", stages},
      {"four_reference_lines", true},
      {"hollow_outline_hit", true},
      {"capture_pin_position", true},
      {"notifications_default_off",
       !controller.store.config["notifications_enabled"].toBool()}};
  return Store::writeJson(
      folder + "/" +
          (memory ? "memory-report.json" : "feature-verification.json"),
      report);
}

void ApplicationController::previewVideo(const QString &file) {
  static QLibrary library(QCoreApplication::applicationDirPath() +
                          "/NativeVideoPreview.dll");
  using Creator = QWidget *(*)(const QString &, QWidget *);
  auto create =
      reinterpret_cast<Creator>(library.resolve("createVideoPreview"));
  if (create)
    create(file, home);
  else
    QMessageBox::warning(home, "无法打开预览", library.errorString());
}

bool verifyNativeUi(ApplicationController &controller, const QString &folder) {
  QDir().mkpath(folder);
  QImage image(800, 700, QImage::Format_ARGB32);
  image.fill(Qt::white);
  DesktopSnapshot desktop;
  desktop.bounds = image.rect();
  desktop.frames = {{image.rect(), image}};
  auto overlay = new CaptureOverlay(&controller.store, false, desktop);
  overlay->selectRegion({30, 40, 498, 442});
  overlay->chooseTool("arrow");
  int n = 0;
  for (auto style : QStringList{"tapered", "filled", "hollow", "line"}) {
    Mark mark;
    mark.kind = style == "line" ? "line" : "arrow";
    mark.arrowHead = style;
    mark.width = 9;
    mark.points = {{65, 65 + n * 95.0}, {365, 80 + n * 95.0}};
    overlay->annotation->add(mark);
    ++n;
  }
  overlay->annotation->preferences.width = 9;
  overlay->showToolbar();
  overlay->show();
  QMouseEvent move(QEvent::MouseMove, QPointF(730, 650), QPointF(730, 650),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(overlay, &move);
  QApplication::processEvents();
  auto crop = overlay->contentRect().adjusted(-12, -30, 12, 100);
  bool ok = overlay->grab(crop).save(folder + "/native-reference-lines.png");
  overlay->toolbar->lineButton->menu()->popup(
      overlay->toolbar->lineButton->mapToGlobal(QPoint(0, 24)));
  QApplication::processEvents();
  if (auto popup = QApplication::activePopupWidget())
    ok = popup->grab().save(folder + "/native-line-dropdown.png") && ok;
  overlay->toolbar->lineButton->menu()->hide();
  overlay->close();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QImage pinImage(240, 180, QImage::Format_ARGB32);
  pinImage.fill(QColor("#e7eef4"));
  auto pin = controller.pins.create(pinImage, {100, 100, 240, 180});
  QApplication::processEvents();
  QImage composed(pin->size(), QImage::Format_RGB32);
  composed.fill(Qt::white);
  {
    QPainter painter(&composed);
    painter.drawPixmap(0, 0, pin->grab());
  }
  ok = composed.save(folder + "/native-pin-border.png") && ok;
  controller.pins.handle(pin, "destroy");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  controller.showHome();
  QApplication::processEvents();
  ok = controller.home->grab().save(folder + "/native-home.png") && ok;
  QTimer::singleShot(30, [folder] {
    if (auto dialog =
            qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
      dialog->grab().save(folder + "/native-settings.png");
      dialog->reject();
    }
  });
  controller.openSettings();
  return Store::writeJson(
      folder + "/ui-report.json",
      QJsonObject{{"passed", ok}, {"synthetic_only", true}});
}

bool verifyDesktopMemory(ApplicationController &controller,
                         const QString &folder) {
  QJsonArray stages, timings, screens, details;
  auto sample = [&](const QString &stage) {
    stages.append(QJsonObject{{"stage", stage}, {"memory", memoryUsage()}});
  };
  for (auto screen : QGuiApplication::screens()) {
    auto physical = physicalRect(screen->geometry());
    screens.append(QJsonObject{{"logical_width", screen->geometry().width()},
                               {"logical_height", screen->geometry().height()},
                               {"physical_width", physical.width()},
                               {"physical_height", physical.height()},
                               {"dpr", screen->devicePixelRatio()}});
  }
  sample("startup_without_home_or_tray");
  for (int n = 1; n <= 20; ++n) {
    QElapsedTimer timer;
    timer.start();
    auto desktop = DesktopSnapshot::capture();
    auto detail = desktop.timings;
    double snapshotTime = timer.nsecsElapsed() / 1000000.0;
    auto target = QGuiApplication::primaryScreen()->geometry();
    desktop.foreground =
        0; // This memory check measures capture, not control detection.
    auto selector =
        new CaptureOverlay(&controller.store, false, std::move(desktop));
    timings.append(timer.nsecsElapsed() / 1000000.0);
    detail["overlay_construction_ms"] =
        timer.nsecsElapsed() / 1000000.0 - snapshotTime;
    details.append(detail);
    selector->selectRegion(QRect(target.topLeft() + QPoint(40, 40),
                                 QSize(qMin(1200, target.width() - 80),
                                       qMin(700, target.height() - 80)))
                               .translated(-selector->pos()));
    selector->chooseTool("rect");
    auto mark = selector->annotation->preferences;
    mark.points = {{20, 20}, {200, 160}};
    selector->annotation->add(mark);
    if (n == 1)
      sample("first_capture_open");
    selector->output = [](const QImage &, const QRect &, const QString &) {
      return true;
    };
    selector->complete("copy");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
    if (n == 1 || n == 2 || n == 20)
      sample(QString("after_capture_%1").arg(n));
  }
  auto target = QGuiApplication::primaryScreen()->geometry();
  QImage image(1200, 700, QImage::Format_ARGB32);
  image.fill(Qt::white);
  auto first = controller.pins.create(
      image, QRect(target.topLeft() + QPoint(40, 40), image.size()));
  auto second = controller.pins.create(
      image.copy(), QRect(target.topLeft() + QPoint(100, 100), image.size()));
  image = {};
  QApplication::processEvents();
  sample("two_independent_pins_1200x700");
  controller.pins.handle(first, "destroy");
  controller.pins.handle(second, "destroy");
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  sample("pins_destroyed");
  return Store::writeJson(folder + "/desktop-memory-report.json",
                          QJsonObject{{"passed", true},
                                      {"screens", screens},
                                      {"stages", stages},
                                      {"snapshot_and_overlay_ms", timings},
                                      {"timing_details", details},
                                      {"screen_pixels_saved", false},
                                      {"clipboard_changed", false}});
}

// Synthetic fixture: the same F3 handler as interactive capture, with no desktop reads.
bool verifyPinLatency(ApplicationController &controller, const QString &folder) {
  QImage image(2560, 1440, QImage::Format_RGB32);
  image.fill(QColor("#f5f6f8"));
  {
    QPainter p(&image);
    QLinearGradient gradient(0, 0, 2560, 1440);
    gradient.setColorAt(0, QColor("#e3e9f4"));
    gradient.setColorAt(1, QColor("#d9eee4"));
    p.fillRect(image.rect(), gradient);
    p.setFont(QFont("Microsoft Yahei UI", 13));
    for (int row = 0; row < 60; ++row) {
      p.setPen(QColor(20 + row % 80, 45, 70));
      p.drawText(24, 24 + row * 23,
                 QString("FloatShot synthetic screen %1 / 截图贴图性能测试 / 0123456789").arg(row).repeated(6));
    }
  }
  controller.store.config["auto_save"] = false;
  controller.store.config["history_limit"] = 100;
  controller.store.config["cache_limit_mb"] = 512;
  controller.store.config["confirm_capture_exit"] = false;
  auto fixture = controller.store.path("fixture.png");
  if (!image.save(fixture)) return false;
  controller.store.history = {};
  for (int n = 0; n < 90; ++n) {
    auto file = controller.store.path(QString("history/fixture-%1.png").arg(n));
    QFile::remove(file);
    if (!QFile::copy(fixture, file)) return false;
    controller.store.history.append(QJsonObject{{"id", QString("fixture-%1").arg(n)},
        {"kind", "image"}, {"path", file}, {"created", "synthetic"},
        {"width", 2560}, {"height", 1440}});
  }
  controller.showHome();
  QApplication::processEvents();
  controller.home->hide();
  class FirstPaint : public QObject {
  public:
    QElapsedTimer elapsed;
    double ms = -1;
    bool eventFilter(QObject *object, QEvent *event) override {
      if (ms < 0 && event->type() == QEvent::Paint && qobject_cast<PinWindow *>(object))
        ms = elapsed.nsecsElapsed() / 1e6;
      return false;
    }
  } paint;
  QApplication::instance()->installEventFilter(&paint);
  QJsonArray runs;
  bool passed = true;
  for (int n = 0; n < 5; ++n) {
    // Deterministically keep image IO blocked until the pin has painted.
    auto release = std::make_shared<std::promise<void>>();
    auto ready = release->get_future().share();
    controller.store.submitIo([ready] { ready.wait(); return QString(); });
    DesktopSnapshot desktop;
    desktop.bounds = image.rect();
    desktop.frames = {{desktop.bounds, image}};
    auto overlay = new CaptureOverlay(&controller.store, false, desktop);
    overlay->selectRegion({100, 80, 1800, 1100});
    overlay->output = [&](const QImage &pixels, const QRect &region, const QString &action) {
      return controller.imageOutput(pixels, region, action);
    };
    QObject::connect(overlay, &CaptureOverlay::finished, &controller,
                     [&] { controller.refreshHistory(); });
    overlay->show();
    QApplication::processEvents();
    paint.ms = -1;
    auto reads = controller.historyThumbnailReads;
    paint.elapsed.start();
    QKeyEvent key(QEvent::KeyPress, Qt::Key_F3, Qt::NoModifier);
    QApplication::sendEvent(overlay, &key);
    double handler = paint.elapsed.nsecsElapsed() / 1e6;
    while (paint.ms < 0 && paint.elapsed.elapsed() < handler + 5000)
      QApplication::processEvents(QEventLoop::AllEvents, 10);
    release->set_value();
    auto pin = controller.pins.windows.isEmpty() ? nullptr : controller.pins.windows.last().data();
    bool position = pin && pin->contentGeometry() == QRect(100, 80, 1800, 1100);
    passed &= paint.ms >= 0 && position;
    runs.append(QJsonObject{{"handler_ms", handler}, {"first_paint_ms", paint.ms},
        {"hidden_history_image_reads", double(controller.historyThumbnailReads - reads)},
        {"original_position", position}});
    controller.pins.timer.stop();
  }
  QElapsedTimer persist;
  persist.start();
  controller.pins.persist();
  double persistMs = persist.nsecsElapsed() / 1e6;
  controller.store.flushIo();
  auto savedPins = controller.store.workspace().size();
  auto readsBeforeReopen = controller.historyThumbnailReads;
  controller.showHome();
  auto reopenReads = controller.historyThumbnailReads - readsBeforeReopen;
  controller.home->hide();
  passed &= savedPins == 5;
  QApplication::instance()->removeEventFilter(&paint);
  QJsonObject report{{"version", QCoreApplication::applicationVersion()},
      {"passed", passed}, {"synthetic_only", true}, {"paint_before_image_io_unblocked", true}, {"initial_history_records", 90},
      {"input_size", QJsonArray{2560,1440}}, {"pin_size", QJsonArray{1800,1100}},
      {"runs", runs}, {"persist_five_pins_handler_ms", persistMs},
      {"durable_pin_count", savedPins}, {"reopen_history_image_reads", double(reopenReads)}};
  return Store::writeJson(folder + "/pin-latency-report.json", report) && passed;
}
