#include "recognition.h"
#include "image_text.h"
#include "store.h"
#include <BarcodeFormat.h>
#include <ImageView.h>
#include <QtCore/QThread>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <ReadBarcode.h>
#include <ReaderOptions.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

QString PaddleOcrJob::modelsPath() {
  return QCoreApplication::applicationDirPath() + "/ocr";
}
QJsonObject PaddleOcrJob::profiles() {
  QFile file(modelsPath() + "/models.json");
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll()).object()["profiles"].toObject();
}
PaddleOcrJob::PaddleOcrJob(QObject *parent,QString gpuWorker) : QObject(parent),gpuProgram(gpuWorker.isEmpty()?QCoreApplication::applicationDirPath()+"/gpu/FloatShotPaddleOcr.exe":gpuWorker) {
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, this, [this] {
    cancel(); emit finished({{"error", "识别超过 3 分钟，已停止。请缩小截图区域后重试。"}});
  });
  connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart && active) {
      if(gpuAttempt && selectedAcceleration=="auto") {
        gpuError=process.errorString(); emit progress("fallback",0,0);
        auto attempt=generation;
        QTimer::singleShot(0,this,[this,attempt]{if(active && generation==attempt)launch(false);});
      }
      else complete({{"error", "无法启动文字识别组件：" + process.errorString()}});
    }
  });
  connect(&process,&QProcess::readyReadStandardOutput,this,[this] {
    progressBuffer+=process.readAllStandardOutput();
    int newline;
    while((newline=progressBuffer.indexOf('\n'))>=0) {
      auto item=QJsonDocument::fromJson(progressBuffer.left(newline)).object(); progressBuffer.remove(0,newline+1);
      if(active && item.contains("phase")) emit progress(item["phase"].toString(),item["done"].toInt(),item["total"].toInt());
    }
  });
  connect(&process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
    if (!active) return;
    QFile output(temporary->path() + "/result.json");
    QJsonObject result;
    if (output.open(QIODevice::ReadOnly)) {
      result = QJsonDocument::fromJson(output.readAll()).object(); output.close();
    }
    if (result.isEmpty() || status != QProcess::NormalExit || (exitCode != 0 && !result.contains("error")))
      result = {{"error", "识别组件未正常完成。请检查 OCR 模型和运行库是否完整。"}};
    if(result.contains("error") && gpuAttempt && selectedAcceleration=="auto") {
      gpuError=result["error"].toString(); emit progress("fallback",0,0); launch(false); return;
    }
    if(!gpuError.isEmpty()) result["acceleration_fallback"]=true;
    complete(result);
  });
}
PaddleOcrJob::~PaddleOcrJob() { cancel(); }
void PaddleOcrJob::complete(QJsonObject result) {
  if (!active) return;
  active = false; timeout.stop(); temporary.reset(); emit finished(result);
}
void PaddleOcrJob::cancel() {
  ++generation;
  active = false; timeout.stop();
  if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
  temporary.reset();
}
void PaddleOcrJob::start(const QImage &image, const QString &profile, int rotation,const QString &acceleration) {
  cancel(); active = true; gpuError.clear(); progressBuffer.clear();
  selectedProfile=profile; selectedRotation=rotation; selectedAcceleration=acceleration;
  if (image.isNull()) { complete({{"error", "没有可识别的图片。"}}); return; }
  if (!profiles().contains(profile)) { complete({{"error", "所选语言组的 OCR 模型缺失，请重新解压完整安装包。"}}); return; }
  temporary = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/FloatShot-OCR-XXXXXX");
  if (!temporary->isValid() || !image.save(temporary->path() + "/input.png")) {
    complete({{"error", "无法准备识别图片，请检查临时目录。"}}); return;
  }
  bool useGpu=acceleration=="gpu" || (acceleration=="auto" && image.height()>=200 && image.width()>=200 && qint64(image.width())*image.height()>=100000 && QFileInfo::exists(gpuProgram));
  launch(useGpu);
  timeout.start(180000);
}
void PaddleOcrJob::launch(bool gpu) {
  gpuAttempt=gpu; progressBuffer.clear();
  QFile::remove(temporary->path()+"/result.json");
  process.setProcessChannelMode(QProcess::SeparateChannels);
  // The child has a GPU-specific runtime beside its executable; Qt DLLs are in
  // the package root. This makes it independent of the developer's SDK PATH.
  process.setWorkingDirectory(QCoreApplication::applicationDirPath());
  QStringList arguments=
                {"--input", temporary->path() + "/input.png", "--models", modelsPath(),
                 "--output", temporary->path() + "/result.json", "--profile", selectedProfile,
                 "--rotation", QString::number(selectedRotation)};
  if(gpu) arguments.append("--gpu");
  process.start(gpu ? gpuProgram : QCoreApplication::applicationDirPath()+"/FloatShotPaddleOcr.exe",arguments);
}
QString recognizeText(const QImage &image) {
  PaddleOcrJob job; QEventLoop loop; QJsonObject result;
  QObject::connect(&job, &PaddleOcrJob::finished, &loop, [&](QJsonObject value) { result = value; loop.quit(); });
  job.start(image);
  if (job.running()) loop.exec();
  if (result.contains("error")) throw std::runtime_error(result["error"].toString().toUtf8().constData());
  return result["text"].toString();
}
QString recognizeWindowsText(const QImage &source) {
  using namespace winrt::Windows::Media::Ocr;
  using namespace winrt::Windows::Graphics::Imaging;
  using namespace winrt::Windows::Storage::Streams;
  auto image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  int maximum = int(OcrEngine::MaxImageDimension());
  if (image.width() > maximum || image.height() > maximum)
    image = image.scaled(maximum, maximum, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
  auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
  if (!engine)
    throw std::runtime_error(
        "没有可用的 Windows OCR 语言，请在系统设置中安装语言包。");
  DataWriter writer;
  writer.WriteBytes(winrt::array_view<const uint8_t>(
      image.constBits(), image.constBits() + image.sizeInBytes()));
  auto bitmap = SoftwareBitmap::CreateCopyFromBuffer(
      writer.DetachBuffer(), BitmapPixelFormat::Bgra8, image.width(),
      image.height(), BitmapAlphaMode::Premultiplied);
  auto result = engine.RecognizeAsync(bitmap).get();
  return QString::fromWCharArray(result.Text().c_str());
}
QString recognizeCodes(const QImage &source) {
  auto image = source.convertToFormat(QImage::Format_Grayscale8);
  auto results = ZXing::ReadBarcodes(
      ZXing::ImageView(image.constBits(), image.width(), image.height(),
                       ZXing::ImageFormat::Lum, image.bytesPerLine()),
      ZXing::ReaderOptions().setTryHarder(true).setTryRotate(true).setTryInvert(
          true));
  QStringList text;
  for (const auto &result : results)
    if (result.isValid())
      text.append(QString::fromStdString(ZXing::ToString(result.format())) +
                  ":\n" + QString::fromStdString(result.text()));
  return text.join("\n\n");
}
void showRecognition(const QImage &image, bool codes, QWidget *parent, Store *store) {
  if (!codes) {
    ImageTextOverlay workspace(parent,store,[] { return QRect(); });
    QEventLoop loop;
    QObject::connect(&workspace,&ImageTextOverlay::closed,&loop,&QEventLoop::quit);
    workspace.begin(image);
    if (!image.isNull()) loop.exec();
    return;
  }
  QDialog dialog(parent); dialog.setWindowTitle("浮光 · 扫码"); dialog.resize(640,430);
  auto root=new QVBoxLayout(&dialog);
  auto status=new QLabel("正在识别二维码 / 条形码…"); root->addWidget(status);
  auto text=new QPlainTextEdit; text->setObjectName("recognitionText");
  text->setReadOnly(true); root->addWidget(text);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Close);
  buttons->button(QDialogButtonBox::Close)->setText("关闭");
  auto copy=buttons->addButton("复制结果",QDialogButtonBox::ActionRole);
  copy->setEnabled(false); root->addWidget(buttons);
  QObject::connect(copy,&QPushButton::clicked,text,[text]{QApplication::clipboard()->setText(text->toPlainText());});
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  QTimer::singleShot(0,&dialog,[&] {
    auto result=std::make_shared<QString>();
    auto thread=QThread::create([image,result] {
      try { *result=recognizeCodes(image); }
      catch (const std::exception &error) { *result=QString::fromUtf8(error.what()); }
    });
    QObject::connect(thread,&QThread::finished,&dialog,[=] {
      text->setPlainText(*result); copy->setEnabled(!result->isEmpty());
      status->setText(result->isEmpty()?"没有识别到二维码或条形码。":"识别完成");
    });
    QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);
    thread->start();
  });
  dialog.exec();
}
