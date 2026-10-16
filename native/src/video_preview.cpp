#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>
#include <QtMultimediaWidgets/QVideoWidget>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

extern "C" __declspec(dllexport) QWidget *
createVideoPreview(const QString &path, QWidget *parent) {
  auto dialog = new QDialog(parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle("picut · 录屏预览");
  dialog->resize(860, 590);
  dialog->setStyleSheet("QDialog{background:#101820;color:white;}QPushButton{"
                        "padding:8px 14px;}QLabel{color:white;}");
  auto root = new QVBoxLayout(dialog);
  root->addWidget(new QLabel(QFileInfo(path).fileName()));
  auto video = new QVideoWidget;
  root->addWidget(video, 1);
  auto player = new QMediaPlayer(dialog);
  auto audio = new QAudioOutput(dialog);
  player->setAudioOutput(audio);
  player->setVideoOutput(video);
  player->setSource(QUrl::fromLocalFile(path));
  auto row = new QHBoxLayout;
  root->addLayout(row);
  auto pause = new QPushButton("播放 / 暂停");
  row->addWidget(pause);
  QObject::connect(pause, &QPushButton::clicked, player, [player] {
    player->playbackState() == QMediaPlayer::PlayingState ? player->pause()
                                                          : player->play();
  });
  auto folder = new QPushButton("打开目录");
  row->addWidget(folder);
  QObject::connect(folder, &QPushButton::clicked, dialog, [path] {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
  });
  auto copy = new QPushButton("复制视频文件");
  row->addWidget(copy);
  QObject::connect(copy, &QPushButton::clicked, dialog, [path] {
    auto mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(mime);
  });
  auto save = new QPushButton("另存为");
  row->addWidget(save);
  QObject::connect(save, &QPushButton::clicked, dialog, [dialog, path] {
    auto target = QFileDialog::getSaveFileName(
        dialog, "另存视频", QFileInfo(path).fileName(), "MP4 视频 (*.mp4)");
    if (target.isEmpty())
      return;
    if (QFileInfo::exists(target)) {
      QMessageBox::warning(dialog, "另存失败",
                           "目标文件已存在，请换一个文件名。");
      return;
    }
    if (!QFile::copy(path, target))
      QMessageBox::warning(dialog, "另存失败",
                           "无法复制视频文件，请检查目录权限。");
  });
  QObject::connect(dialog, &QDialog::finished, player, [player] {
    player->stop();
    player->setSource({});
  });
  dialog->show();
  return dialog;
}

extern "C" __declspec(dllexport) QJsonObject
verifyVideoPlayback(const QString &path) {
  QMediaPlayer player;
  QVideoSink sink;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  int frames = 0;
  QString error;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                   [&](const QVideoFrame &frame) {
                     if (frame.isValid() && ++frames >= 2)
                       loop.quit();
                   });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                   [&](QMediaPlayer::Error, const QString &text) {
                     error = text;
                     loop.quit();
                   });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  player.setVideoSink(&sink);
  player.setSource(QUrl::fromLocalFile(path));
  player.play();
  timeout.start(10000);
  loop.exec();
  player.stop();
  return {{"passed", frames >= 2 && error.isEmpty()},
          {"decoded_frames", frames},
          {"duration_ms", player.duration()},
          {"error", error}};
}
