#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include "ocr_shared_image.h"
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <windows.h>
#include <psapi.h>
#include <dxgi1_2.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace {
bool serving = false;
qint64 requestId = 0;
void reply(const QJsonObject &object) {
  const auto data=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
  std::fwrite(data.constData(),1,size_t(data.size()),stdout); std::fflush(stdout);
}
void reportProgress(const QString &phase,int done=0,int total=0) {
  QJsonObject item{{"phase",phase},{"done",done},{"total",total}};
  if (serving) { item["event"]="progress"; item["id"]=requestId; }
  reply(item);
}
void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
QJsonObject readObject(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) fail("无法读取 OCR 配置：" + path);
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(f.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) fail("OCR 配置格式错误：" + path);
  return doc.object();
}
void writeObject(const QString &path, const QJsonObject &object) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(object).toJson()) < 0 || !f.commit())
    fail("无法保存 OCR 结果：" + path);
}
class Network {
  std::vector<float> data;
public:
  Ort::Session session;
  std::string input, output;
  std::vector<int64_t> shape;
  Network(Ort::Env &env, const QString &path, const Ort::SessionOptions &options)
      : session(env, reinterpret_cast<const wchar_t *>(path.utf16()), options) {
    Ort::AllocatorWithDefaultOptions allocator;
    input = session.GetInputNameAllocated(0, allocator).get();
    output = session.GetOutputNameAllocated(0, allocator).get();
    shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
  }
  Ort::Value run(const cv::Mat &image, int width, int height, bool padding = false, bool imagenet = false, bool rgb = false) {
    return runBatch({image},width,height,padding,imagenet,rgb);
  }
  Ort::Value runBatch(const std::vector<cv::Mat> &images, int width, int height, bool padding = false, bool imagenet = false, bool rgb = false) {
    size_t imageElements = size_t(3)*width*height;
    data.assign(imageElements*images.size(), 0.f);
    for (size_t n=0;n<images.size();++n) {
      const auto &image=images[n]; cv::Mat resized;
      int resizedWidth = padding ? std::min(width, int(std::ceil(double(height) * image.cols / image.rows))) : width;
      cv::resize(image, resized, {resizedWidth, height});
      for (int y = 0; y < height; ++y) {
        auto row = resized.ptr<cv::Vec3b>(y);
        for (int x = 0; x < resizedWidth; ++x)
          for (int c = 0; c < 3; ++c) {
            constexpr float mean[]{.485f,.456f,.406f}, std[]{.229f,.224f,.225f};
            float pixel = row[x][rgb ? 2-c : c];
            data[n*imageElements+size_t(c) * width * height + size_t(y) * width + x] = imagenet ? (pixel / 255.f - mean[c]) / std[c] : pixel / 127.5f - 1.f;
          }
        }
      }
    std::array<int64_t, 4> dims{int64_t(images.size()), 3, height, width};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto tensor = Ort::Value::CreateTensor<float>(memory, data.data(), data.size(), dims.data(), dims.size());
    const char *inputs[]{input.c_str()}, *outputs[]{output.c_str()};
    auto results = session.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
    return std::move(results.front());
  }
};
using Quad = std::array<cv::Point2f, 4>;
struct Box { Quad quad; cv::Rect bounds; float score; };
struct Token { QString text; int first, last; };
struct DecodedLine {
  QString text;
  double confidence = 0;
  std::vector<Token> tokens;
  double contentSteps = 1;
};
Quad ordered(cv::RotatedRect rectangle) {
  cv::Point2f p[4]; rectangle.points(p);
  std::sort(p, p + 4, [](auto a, auto b) { return a.x < b.x; });
  auto tl = p[0].y < p[1].y ? p[0] : p[1];
  auto bl = p[0].y < p[1].y ? p[1] : p[0];
  auto tr = p[2].y < p[3].y ? p[2] : p[3];
  auto br = p[2].y < p[3].y ? p[3] : p[2];
  return {tl, tr, br, bl};
}
cv::Mat imagePixels(const QImage &source) {
  if (!source.hasAlphaChannel()) {
    const auto opaque=source.convertToFormat(QImage::Format_RGB32);
    cv::Mat bgra(opaque.height(),opaque.width(),CV_8UC4,const_cast<uchar *>(opaque.constBits()),opaque.bytesPerLine()), bgr;
    cv::cvtColor(bgra,bgr,cv::COLOR_BGRA2BGR); return bgr;
  }
  QImage opaque(source.size(), QImage::Format_RGB888);
  opaque.fill(Qt::white);
  { QPainter painter(&opaque); painter.drawImage(0, 0, source); }
  cv::Mat rgb(opaque.height(), opaque.width(), CV_8UC3, opaque.bits(), opaque.bytesPerLine()), bgr;
  cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR); return bgr;
}
cv::Mat loadImage(const QString &path) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  auto size = reader.size();
  if (size.isValid() && qint64(size.width()) * size.height() > 40000000)
    fail("图片超过 4000 万像素，请分区域识别。");
  auto source = reader.read();
  if (source.isNull()) fail("无法读取图片：" + reader.errorString());
  return imagePixels(source);
}
class Pipeline {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "FloatShotOCR"};
  Ort::SessionOptions options;
  std::unique_ptr<Network> detector, classifier, recognizer;
  QStringList dictionary;
  QString modelName, profileName;
  int recognitionBatch;
  QString deviceName = "CPU";
  bool gpuEnabled;
public:
  Pipeline(const QString &directory, const QString &profile, int threads = 4, bool arena = false, bool spinning = true, int batch = 1, bool gpu = false) : recognitionBatch(batch), gpuEnabled(gpu) {
    env.DisableTelemetryEvents();
    options.SetIntraOpNumThreads(threads);
    options.SetInterOpNumThreads(1);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (!arena) options.DisableCpuMemArena();
    else if (!gpu) {
      const auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
      Ort::ArenaCfg config(0,1,-1,-1);
      env.CreateAndRegisterAllocator(memory,config);
      options.AddConfigEntry("session.use_env_allocators","1");
    }
    if (!spinning) {
      options.AddConfigEntry("session.intra_op.allow_spinning", "0");
      options.AddConfigEntry("session.inter_op.allow_spinning", "0");
    }
    options.DisableMemPattern();
    auto directionOptions=options.Clone();
    if(gpu) directionOptions.SetIntraOpNumThreads(2);
    if (gpu) {
      using AppendDml = OrtStatus *(ORT_API_CALL *)(OrtSessionOptions *, int);
      auto append = reinterpret_cast<AppendDml>(GetProcAddress(GetModuleHandleW(L"onnxruntime.dll"),"OrtSessionOptionsAppendExecutionProvider_DML"));
      if (!append) fail("GPU 识别运行库不可用。");
      Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
      if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) fail("无法枚举显卡。");
      int selected=-1; size_t largest=0;
      for(UINT index=0;;++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
        if(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),nullptr))) continue;
        if(selected<0 || desc.DedicatedVideoMemory>largest) { selected=int(index); largest=desc.DedicatedVideoMemory; deviceName=QString::fromWCharArray(desc.Description); }
      }
      if(selected<0) fail("没有可用的 DirectX 12 显卡。");
      options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      Ort::ThrowOnError(append(options,selected));
    }
    auto config = readObject(directory + "/models.json");
    auto models = config["profiles"].toObject();
    if (!models.contains(profile)) fail("不支持的 OCR 语言组：" + profile);
    auto entry = models[profile].toObject();
    modelName = entry["model"].toString(); profileName = profile;
    detector = std::make_unique<Network>(env, directory + "/" + config["det"].toString(), options);
    classifier = std::make_unique<Network>(env, directory + "/" + config["cls"].toString(), directionOptions);
    recognizer = std::make_unique<Network>(env, directory + "/" + entry["rec"].toString(), options);
    QFile dict(directory + "/" + entry["dict"].toString());
    if (!dict.open(QIODevice::ReadOnly)) fail("OCR 字典缺失。");
    dictionary.append(QString());
    while (!dict.atEnd()) {
      auto line = dict.readLine();
      while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
      dictionary.append(QString::fromUtf8(line));
    }
    dictionary.append(" ");
  }
  QJsonObject inspect() {
    QJsonObject result{{"model", modelName}, {"dictionary_size", dictionary.size()}};
    for (auto item : {std::pair{"det", detector.get()}, std::pair{"cls", classifier.get()}, std::pair{"rec", recognizer.get()}}) {
      QJsonArray shape; for (auto n : item.second->shape) shape.append(double(n));
      result[item.first] = shape;
    }
    return result;
  }
  void warmup() {
    const cv::Mat blank(64,320,CV_8UC3,cv::Scalar(255,255,255));
    detector->run(blank,64,64,false,true);
    classifier->run(blank,classifier->shape[3]>0?int(classifier->shape[3]):192,
        classifier->shape[2]>0?int(classifier->shape[2]):48,false,true,true);
    recognizer->run(blank,320,recognizer->shape[2]>0?int(recognizer->shape[2]):48,true);
  }
  std::vector<Box> detect(const cv::Mat &image, cv::Point offset) {
    double scale = std::min(2., 736. / std::min(image.cols, image.rows));
    scale = std::max(1., scale);
    scale = std::min(scale, 1600. / std::max(image.cols, image.rows));
    int width = std::max(32, int(std::round(image.cols * scale / 32)) * 32);
    int height = std::max(32, int(std::round(image.rows * scale / 32)) * 32);
    auto result = detector->run(image, width, height, false, true);
    auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4) fail("文字检测模型输出尺寸错误。");
    int h = int(shape[2]), w = int(shape[3]);
    cv::Mat probability(h, w, CV_32F, result.GetTensorMutableData<float>());
    cv::Mat binary;
    cv::compare(probability, .2, binary, cv::CMP_GT);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<Box> boxes;
    for (auto &contour : contours) {
      if (contour.size() < 3) continue;
      auto rect = cv::minAreaRect(contour);
      if (std::min(rect.size.width, rect.size.height) < 3) continue;
      auto region = cv::boundingRect(contour) & cv::Rect(0, 0, w, h);
      std::vector<cv::Point> local; for (auto p : contour) local.push_back(p - region.tl());
      cv::Mat mask = cv::Mat::zeros(region.size(), CV_8U);
      cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{local}, cv::Scalar(255));
      float confidence = float(cv::mean(probability(region), mask)[0]);
      if (confidence < .45f) continue;
      float distance = rect.size.area() * 1.4f / (2 * (rect.size.width + rect.size.height));
      rect.size.width += 2 * distance; rect.size.height += 2 * distance;
      auto quad = ordered(rect);
      for (auto &p : quad) {
        p.x = std::clamp(p.x * image.cols / w, 0.f, float(image.cols - 1)) + offset.x;
        p.y = std::clamp(p.y * image.rows / h, 0.f, float(image.rows - 1)) + offset.y;
      }
      auto bounds = cv::boundingRect(std::vector<cv::Point2f>(quad.begin(), quad.end()));
      if (bounds.width < 4 || bounds.height < 4) continue;
      boxes.push_back({quad, bounds, confidence});
      if (boxes.size() >= 1000) break;
    }
    return boxes;
  }
  cv::Mat crop(const cv::Mat &image, const Quad &p, Quad &readingQuad) {
    readingQuad = p;
    int w = std::max(4, int(std::max(cv::norm(p[0]-p[1]), cv::norm(p[2]-p[3]))));
    int h = std::max(4, int(std::max(cv::norm(p[0]-p[3]), cv::norm(p[1]-p[2]))));
    Quad destination{cv::Point2f(0,0), cv::Point2f(float(w-1),0), cv::Point2f(float(w-1),float(h-1)), cv::Point2f(0,float(h-1))};
    cv::Mat out;
    cv::warpPerspective(image, out, cv::getPerspectiveTransform(p.data(), destination.data()), {w,h}, cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    if (h >= w * 1.5) {
      cv::rotate(out, out, cv::ROTATE_90_COUNTERCLOCKWISE);
      readingQuad = {p[1], p[2], p[3], p[0]};
    }
    return out;
  }
  void orient(std::vector<cv::Mat> &images, std::vector<Quad> &quads) {
    int ch = classifier->shape[2] > 0 ? int(classifier->shape[2]) : 48;
    int cw = classifier->shape[3] > 0 ? int(classifier->shape[3]) : 192;
    for (size_t start=0;start<images.size();start+=16) {
      size_t end=std::min(start+16,images.size());
      std::vector<cv::Mat> batch(images.begin()+start,images.begin()+end);
      auto result = classifier->runBatch(batch, cw, ch, false, true, true);
      if (result.GetTensorTypeAndShapeInfo().GetElementCount() != batch.size()*2) fail("方向模型输出错误。");
      auto values = result.GetTensorData<float>();
      for (size_t i=0;i<batch.size();++i)
        if (values[i*2+1] > values[i*2] && values[i*2+1] > .9) {
          cv::rotate(images[start+i], images[start+i], cv::ROTATE_180);
          auto p = quads[start+i];
          quads[start+i] = {p[2], p[3], p[0], p[1]};
        }
    }
  }
  std::vector<DecodedLine> recognize(const std::vector<cv::Mat> &images) {
    std::vector<DecodedLine> decoded(images.size());
    std::vector<size_t> order(images.size()); std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return double(images[a].cols)/images[a].rows<double(images[b].cols)/images[b].rows;});
    int height = recognizer->shape[2] > 0 ? int(recognizer->shape[2]) : 48;
    for (size_t start=0;start<order.size();) {
      int narrowWidth=std::max(320,int(std::ceil(double(height)*images[order[start]].cols/images[order[start]].rows)));
      size_t end=start+1;
      // Group similar widths and limit tensor area so a single long line cannot
      // inflate every other line's padding or the worker's memory peak.
      while(end<order.size() && end-start<size_t(recognitionBatch)) {
        int candidate=int(std::ceil(double(height)*images[order[end]].cols/images[order[end]].rows));
        if(candidate>narrowWidth*1.25 || size_t(candidate)*(end-start+1)>size_t(gpuEnabled ? 12288 : 6400)) break;
        ++end;
      }
      int width=std::max(320,int(std::ceil(double(height)*images[order[end-1]].cols/images[order[end-1]].rows)));
      if(gpuEnabled) width=int(std::ceil(width/64.))*64;
      if (width > 8192) fail("单行文字过长，请分区域识别。");
      if (recognizer->shape[3] > 0) width = int(recognizer->shape[3]);
      std::vector<cv::Mat> batch; for(size_t i=start;i<end;++i) batch.push_back(images[order[i]]);
      auto result = recognizer->runBatch(batch, width, height, true);
      auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
      if (shape.size() != 3 || shape[2] != dictionary.size() || shape[0]!=int64_t(batch.size())) fail("文字模型与字典不匹配。");
      auto values = result.GetTensorData<float>();
      int classes = int(shape[2]);
      for(size_t n=0;n<batch.size();++n) {
        int previous = -1, count = 0; QString text; double score = 0;
        std::vector<Token> tokens;
        for (int64_t t = 0; t < shape[1]; ++t) {
          auto row = values + (n*shape[1]+t)*classes;
          int current = int(std::max_element(row, row + classes) - row);
          if (current != 0 && current != previous) {
            text += dictionary[current]; score += row[current]; ++count;
            tokens.push_back({dictionary[current], int(t), int(t)});
          } else if (current != 0 && !tokens.empty()) {
            tokens.back().last = int(t);
          }
          previous = current;
        }
        while (!tokens.empty() && tokens.front().text.trimmed().isEmpty()) tokens.erase(tokens.begin());
        while (!tokens.empty() && tokens.back().text.trimmed().isEmpty()) tokens.pop_back();
        int resizedWidth = std::min(width, int(std::ceil(double(height) * batch[n].cols / batch[n].rows)));
        decoded[order[start+n]]={text.trimmed(), count ? score / count : 0,
                                std::move(tokens), double(shape[1]) * resizedWidth / width};
      }
      start=end;
      reportProgress("recognize",int(end),int(order.size()));
    }
    return decoded;
  }
  QJsonObject process(const QString &path, int rotation) {
    QElapsedTimer decoding; decoding.start();
    auto image=loadImage(path);
    return process(std::move(image),rotation,decoding.elapsed());
  }
  QJsonObject process(cv::Mat image, int rotation, qint64 inputMs=0) {
    QElapsedTimer timer; timer.start();
    const int originalWidth = image.cols, originalHeight = image.rows;
    if (rotation == 90) cv::rotate(image, image, cv::ROTATE_90_CLOCKWISE);
    if (rotation == 180) cv::rotate(image, image, cv::ROTATE_180);
    if (rotation == 270) cv::rotate(image, image, cv::ROTATE_90_COUNTERCLOCKWISE);
    auto decodeMs = timer.elapsed();
    int tileCount = 0;
    std::vector<Box> boxes;
    constexpr int tileSize = 1600, stride = 1408;
    int totalTiles=std::max(1,int(std::ceil(double(image.cols-tileSize)/stride))+1)*std::max(1,int(std::ceil(double(image.rows-tileSize)/stride))+1);
    reportProgress("detect",0,totalTiles);
    for (int y = 0; y < image.rows; y += stride) {
      for (int x = 0; x < image.cols; x += stride) {
        cv::Rect region{x, y, std::min(tileSize, image.cols-x), std::min(tileSize, image.rows-y)};
        auto detected = detect(image(region), region.tl());
        ++tileCount;
        reportProgress("detect",tileCount,totalTiles);
        for (auto &box : detected) {
          auto duplicate = std::find_if(boxes.begin(), boxes.end(), [&](auto &existing) {
            double intersection = (box.bounds & existing.bounds).area();
            return intersection / std::max(1, std::min(box.bounds.area(), existing.bounds.area())) > .65;
          });
          if (duplicate == boxes.end()) boxes.push_back(box);
          else if (box.bounds.area() > duplicate->bounds.area()) *duplicate = box;
        }
        if (x + tileSize >= image.cols) break;
      }
      if (y + tileSize >= image.rows) break;
    }
    auto detectMs = timer.elapsed()-decodeMs;
    // Join horizontal text split at tile boundaries before recognition, otherwise
    // overlap pixels would be decoded twice or a long line would lose its middle.
    for (size_t i = 0; i < boxes.size(); ++i) {
      for (size_t j = i+1; j < boxes.size();) {
        auto &a = boxes[i]; auto &b = boxes[j];
        auto intersection = a.bounds & b.bounds;
        bool horizontal = std::abs(a.quad[1].y-a.quad[0].y) < a.bounds.height*.3 &&
                          std::abs(b.quad[1].y-b.quad[0].y) < b.bounds.height*.3;
        bool sameRow = std::abs((a.bounds.y+a.bounds.height*.5)-(b.bounds.y+b.bounds.height*.5)) < std::min(a.bounds.height,b.bounds.height)*.3;
        if (intersection.width > 0 && sameRow && horizontal) {
          std::vector<cv::Point2f> points(a.quad.begin(),a.quad.end()); points.insert(points.end(),b.quad.begin(),b.quad.end());
          a.quad=ordered(cv::minAreaRect(points)); a.bounds=cv::boundingRect(points); a.score=std::max(a.score,b.score);
          boxes.erase(boxes.begin()+j); j=i+1;
        } else ++j;
      }
    }
    std::sort(boxes.begin(), boxes.end(), [](auto &a, auto &b) { return a.bounds.y < b.bounds.y; });
    // Sort separately within each visual row; avoid a non-transitive tolerant comparator.
    for (size_t start = 0; start < boxes.size();) {
      size_t end = start + 1;
      while (end < boxes.size() && std::abs(boxes[end].bounds.y - boxes[start].bounds.y) < std::min(boxes[end].bounds.height, boxes[start].bounds.height) * .5) ++end;
      std::sort(boxes.begin()+start, boxes.begin()+end, [](auto &a, auto &b) { return a.bounds.x < b.bounds.x; });
      start = end;
    }
    QJsonArray lines; QStringList text;
    auto before=timer.elapsed(); std::vector<cv::Mat> crops;
    reportProgress("recognize",0,int(boxes.size()));
    std::vector<Quad> readingQuads;
    for (const auto &box : boxes) {
      Quad quad;
      crops.push_back(crop(image,box.quad,quad));
      readingQuads.push_back(quad);
    }
    orient(crops,readingQuads); auto directionMs=timer.elapsed()-before;
    before=timer.elapsed(); auto decoded=recognize(crops); auto recognitionMs=timer.elapsed()-before;
    for (size_t i=0;i<boxes.size();++i) {
      const auto &recognized = decoded[i];
      const auto &line = recognized.text;
      const auto confidence = recognized.confidence;
      if (line.isEmpty() || confidence < .45) continue;
      auto originalPoint = [&](cv::Point2f p) -> cv::Point2f {
        if (rotation == 90) return {p.y, originalHeight - 1.f - p.x};
        if (rotation == 180) return {originalWidth - 1.f - p.x, originalHeight - 1.f - p.y};
        if (rotation == 270) return {originalWidth - 1.f - p.y, p.x};
        return p;
      };
      auto jsonQuad = [&](const std::vector<cv::Point2f> &points) {
        QJsonArray result;
        for (auto p : points) {
          p = originalPoint(p);
          result.append(QJsonArray{qBound(0., double(p.x), double(originalWidth)),
                                   qBound(0., double(p.y), double(originalHeight))});
        }
        return result;
      };
      const auto &reading = readingQuads[i];
      Quad unit{cv::Point2f(0,0), cv::Point2f(1,0), cv::Point2f(1,1), cv::Point2f(0,1)};
      auto transform = cv::getPerspectiveTransform(unit.data(), reading.data());
      QJsonArray characters;
      const auto &tokens = recognized.tokens;
      auto center = [&](size_t n) {
        return std::clamp((tokens[n].first + tokens[n].last + 1.) /
                              (2 * recognized.contentSteps), 0., 1.);
      };
      for (size_t n = 0; n < tokens.size(); ++n) {
        double left = n ? (center(n-1) + center(n)) / 2 : 0;
        double right = n+1 < tokens.size() ? (center(n) + center(n+1)) / 2 : 1;
        std::vector<cv::Point2f> band{{float(left),0},{float(right),0},{float(right),1},{float(left),1}}, mapped;
        cv::perspectiveTransform(band, mapped, transform);
        characters.append(QJsonObject{{"text", tokens[n].text}, {"quad", jsonQuad(mapped)}});
      }
      lines.append(QJsonObject{{"text", line}, {"confidence", confidence},
                               {"quad", jsonQuad(std::vector<cv::Point2f>(reading.begin(),reading.end()))},
                               {"characters", characters}});
      text.append(line);
    }
    return {{"text", text.join('\n')}, {"lines", lines}, {"model", modelName}, {"profile", profileName}, {"elapsed_ms", double(timer.elapsed()+inputMs)}, {"rotation", rotation},
      {"timing",QJsonObject{{"decode_ms",double(decodeMs+inputMs)},{"detect_ms",double(detectMs)},{"direction_ms",double(directionMs)},{"recognition_ms",double(recognitionMs)}}},
      {"tiles",tileCount},{"detected_lines",int(boxes.size())},{"device",deviceName},{"gpu",gpuEnabled},
      {"image_width",originalWidth},{"image_height",originalHeight},{"coordinate_space","original"}};
  }
};

class ParentLease {
  HANDLE parent=nullptr;
  std::jthread watcher;
public:
  explicit ParentLease(DWORD pid) {
    if (!pid || pid==GetCurrentProcessId() || !(parent=OpenProcess(SYNCHRONIZE,FALSE,pid)))
      fail("无法监控 OCR 主程序。");
    watcher=std::jthread([this](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (WaitForSingleObject(parent,100)==WAIT_OBJECT_0) { TerminateProcess(GetCurrentProcess(),0); return; }
      }
    });
  }
  ~ParentLease() { watcher.request_stop(); if(watcher.joinable())watcher.join(); if(parent)CloseHandle(parent); }
};
int serve(const QCommandLineParser &parser) {
  serving=true;
  try {
    ParentLease parent(parser.value("parent-pid").toUInt());
    cv::setNumThreads(1);
    const bool gpu=parser.isSet("gpu");
    const auto profile=parser.value("profile").isEmpty()?QString("common"):parser.value("profile");
    QElapsedTimer loading; loading.start();
    Pipeline pipeline(parser.value("models"),profile,4,!gpu,false,gpu?4:1,gpu);
    pipeline.warmup();
    reply({{"event","ready"},{"load_ms",loading.elapsed()},{"pid",qint64(GetCurrentProcessId())}});
    std::array<char,65536> line{};
    while (std::fgets(line.data(),int(line.size()),stdin)) {
      const QByteArray input(line.data());
      QJsonParseError error; const auto document=QJsonDocument::fromJson(input,&error);
      if (!input.endsWith('\n') || error.error!=QJsonParseError::NoError || !document.isObject())
        fail("OCR 请求格式无效。");
      const auto request=document.object(); requestId=request["id"].toInteger();
      if (requestId<=0) fail("OCR 请求编号无效。");
      QElapsedTimer timer; timer.start(); QJsonObject result;
      try {
        const auto rotation=request["rotation"].toInt();
        if (rotation!=0 && rotation!=90 && rotation!=180 && rotation!=270) fail("OCR 旋转角度无效。");
        OcrSharedImage shared;
        auto image=imagePixels(shared.open(request));
        const auto decoding=timer.elapsed();
        result=pipeline.process(std::move(image),rotation,decoding);
      } catch (const std::exception &failure) { result={{"error",QString::fromUtf8(failure.what())}}; }
      result["total_ms"]=double(timer.elapsed());
      PROCESS_MEMORY_COUNTERS_EX memory{};
      if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),sizeof(memory))) {
        result["worker_private_mb"]=double(memory.PrivateUsage)/1048576.;
        result["peak_working_set_mb"]=double(memory.PeakWorkingSetSize)/1048576.;
      }
      reply({{"event","result"},{"id",requestId},{"result",result}});
    }
    return 0;
  } catch (const std::exception &error) {
    reply({{"event","error"},{"error",QString::fromUtf8(error.what())}}); return 1;
  }
}
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  for (const auto &name : {"input", "models", "output", "profile", "rotation", "batch", "threads", "rec-batch", "parent-pid"})
    parser.addOption(QCommandLineOption(name, name, "value"));
  parser.addOption(QCommandLineOption("inspect", "Inspect model dimensions"));
  parser.addOption(QCommandLineOption("arena", "Benchmark reusable CPU allocations"));
  parser.addOption(QCommandLineOption("no-spinning", "Benchmark non-spinning thread pools"));
  parser.addOption(QCommandLineOption("gpu", "Use DirectML hardware acceleration"));
  parser.addOption(QCommandLineOption("serve", "Serve shared-memory OCR requests until the parent exits"));
  parser.process(app);
  if (parser.isSet("serve")) return serve(parser);
  QElapsedTimer total; total.start();
  QJsonObject result; int exitCode = 0;
  try {
    cv::setNumThreads(1);
    auto profile = parser.value("profile"); if (profile.isEmpty()) profile = "common";
    int threads = parser.isSet("threads") ? std::clamp(parser.value("threads").toInt(),1,16) : 4;
    int batch = parser.isSet("rec-batch") ? std::clamp(parser.value("rec-batch").toInt(),1,16) : (parser.isSet("gpu") ? 4 : 1);
    reportProgress("load");
    Pipeline pipeline(parser.value("models"), profile, threads, parser.isSet("arena"), !parser.isSet("no-spinning"), batch,parser.isSet("gpu"));
    auto loadMs=total.elapsed();
    if (parser.isSet("inspect")) result = pipeline.inspect();
    else if (parser.isSet("batch")) {
      QJsonArray results;
      for (auto value : readObject(parser.value("batch"))["cases"].toArray()) {
        auto item = value.toObject();
        auto one = pipeline.process(item["input"].toString(), item["rotation"].toInt());
        one["id"] = item["id"]; results.append(one);
      }
      result["cases"] = results;
    } else result = pipeline.process(parser.value("input"), parser.value("rotation").toInt());
    result["model_load_ms"]=double(loadMs);
  } catch (const std::exception &error) { result["error"] = QString::fromUtf8(error.what()); exitCode = 1; }
  result["total_ms"] = double(total.elapsed());
  PROCESS_MEMORY_COUNTERS_EX memory{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
    result["peak_working_set_mb"] = double(memory.PeakWorkingSetSize) / 1048576.;
  try { writeObject(parser.value("output"), result); }
  catch (const std::exception &) { return 2; }
  return exitCode;
}
