#import <CoreML/MLCPUComputeDevice.h>
#import <Vision/Vision.h>
#include <QtCore/QJsonArray>
#include <QtCore/QThread>
#include <QtGui/QTransform>
#include <memory>

#include "recognition.h"

namespace {
NSArray<NSString *> *languages(const QString &profile) {
  if (profile == "japanese") return @[@"ja-JP", @"en-US"];
  if (profile == "latin") return @[@"en-US", @"fr-FR", @"de-DE", @"es-ES", @"it-IT", @"pt-BR"];
  return @[@"zh-Hans", @"zh-Hant", @"en-US"];
}

QJsonArray quad(VNRectangleObservation *box, QSize pixels, const QTransform &toOriginal) {
  QJsonArray result;
  for (auto point : {box.topLeft, box.topRight, box.bottomRight, box.bottomLeft}) {
    const auto mapped = toOriginal.map(QPointF(point.x * pixels.width(), (1 - point.y) * pixels.height()));
    result.append(QJsonArray{mapped.x(), mapped.y()});
  }
  return result;
}

QJsonObject recognize(const QImage &source, const QString &profile, int rotation) {
  @autoreleasepool {
    QTransform rotate;
    rotate.rotate(rotation);
    const auto image = rotation ? source.transformed(rotate) : source;
    const auto toOriginal = QImage::trueMatrix(rotate, source.width(), source.height()).inverted();
    auto cg = image.toCGImage();
    if (!cg) return {{"error", "无法读取待识别图片。"}};
    auto handler = [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
    CGImageRelease(cg);
    auto request = [[VNRecognizeTextRequest alloc] init];
    request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    request.usesLanguageCorrection = NO;
    // Vision's automatic device selection can stall or fail in the text reader.
    const auto devices = [request supportedComputeStageDevicesAndReturnError:nil];
    for (VNComputeStage stage in devices) {
      for (id<MLComputeDeviceProtocol> device in devices[stage]) {
        if ([device isKindOfClass:MLCPUComputeDevice.class]) {
          [request setComputeDevice:device forComputeStage:stage];
          break;
        }
      }
    }
    const auto available = [request supportedRecognitionLanguagesAndReturnError:nil];
    NSMutableArray<NSString *> *selected = [NSMutableArray array];
    for (NSString *language in languages(profile))
      if ([available containsObject:language]) [selected addObject:language];
    if (selected.count) request.recognitionLanguages = selected;
    NSError *error = nil;
    if (![handler performRequests:@[request] error:&error])
      return {{"error", QString::fromNSString(error.localizedDescription)}};

    QJsonArray lines;
    QStringList text;
    for (VNRecognizedTextObservation *line in request.results) {
      auto best = [line topCandidates:1].firstObject;
      if (!best.string.length) continue;
      const auto value = QString::fromNSString(best.string);
      __block QJsonArray characters;
      [best.string enumerateSubstringsInRange:NSMakeRange(0, best.string.length)
          options:NSStringEnumerationByComposedCharacterSequences
          usingBlock:^(NSString *character, NSRange range, NSRange, BOOL *) {
        // Use Vision's per-character coordinates when the engine provides them.
        auto box = [best boundingBoxForRange:range error:nil];
        if (box)
          characters.append(QJsonObject{
            {"text", QString::fromNSString(character)}, {"quad", quad(box, image.size(), toOriginal)}});
      }];
      text.append(value);
      lines.append(QJsonObject{{"text", value}, {"quad", quad(line, image.size(), toOriginal)},
                              {"characters", characters}, {"confidence", double(best.confidence)}});
    }
    return {{"text", text.join('\n')}, {"lines", lines}};
  }
}
}

PaddleOcrJob::PaddleOcrJob(QObject *parent, QString) : QObject(parent) {}
PaddleOcrJob::~PaddleOcrJob() { cancel(); }
QString PaddleOcrJob::modelsPath() { return {}; }
QJsonObject PaddleOcrJob::profiles() {
  return {{"common", QJsonObject{{"label", "简繁中文 / 英文"}}},
          {"japanese", QJsonObject{{"label", "日文 / 英文"}}},
          {"latin", QJsonObject{{"label", "拉丁字母语种"}}}};
}
void PaddleOcrJob::prewarm(const QString &, const QString &) {}
void PaddleOcrJob::releaseIdleModel() {}
void PaddleOcrJob::complete(quint64 token, QJsonObject result) {
  if (!active || token != generation) return;
  active = false;
  emit finished(result);
}
void PaddleOcrJob::cancel() {
  ++generation;
  active = false;
}
void PaddleOcrJob::start(const QImage &image, const QString &profile, int rotation, const QString &) {
  cancel();
  active = true;
  const auto token = generation;
  if (image.isNull() || qint64(image.width()) * image.height() > 40000000) {
    complete(token, {{"error", image.isNull() ? "没有可识别的图片。" : "图片超过 4000 万像素，请分区域识别。"}});
    return;
  }
  auto result = std::make_shared<QJsonObject>();
  auto thread = QThread::create([image, profile, rotation, result] {
    *result = recognize(image, profile, rotation);
  });
  connect(thread, &QThread::finished, this, [this, token, result] { complete(token, *result); });
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  thread->start();
}
