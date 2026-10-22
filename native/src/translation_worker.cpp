#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include "translation_terms.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <Windows.h>
#include <Psapi.h>
#include <iostream>
#include <memory>

static QJsonObject readJson(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("无法读取翻译请求或模型清单。");
  QJsonParseError error;
  const auto value = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !value.isObject())
    throw std::runtime_error("翻译请求或模型清单格式错误。");
  return value.object();
}
static void event(const QJsonObject &value) {
  const auto json = QJsonDocument(value).toJson(QJsonDocument::Compact);
  std::cout.write(json.constData(), json.size()); std::cout << '\n' << std::flush;
}
static bool hasChinese(const QString &text) {
  for (const auto c : text) if (c.unicode() >= 0x3400 && c.unicode() <= 0x9fff) return true;
  return false;
}
static bool hasLetters(const QString &text) {
  for (const auto c : text) if (c.isLetter()) return true;
  return false;
}
static void verifyModel(const QString &folder, const QJsonObject &metadata) {
  const auto hashes = metadata["checksums"].toObject();
  if (!hashes.contains("model.bin") || !hashes.contains("source.spm") || !hashes.contains("target.spm"))
    throw std::runtime_error("本地翻译语言包不完整。");
  for (auto it = hashes.begin(); it != hashes.end(); ++it) {
    if (it.key().contains('/') || it.key().contains('\\') || it.key().contains(".."))
      throw std::runtime_error("翻译语言包清单包含无效路径。");
    QFile file(folder + '/' + it.key());
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("翻译语言包文件缺失，请重新解压试用包。");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()).compare(it.value().toString(), Qt::CaseInsensitive))
      throw std::runtime_error("翻译语言包校验失败，请重新解压试用包。");
  }
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser; parser.addHelpOption();
  for (const auto &name : {"input", "output", "models"})
    parser.addOption(QCommandLineOption(name, name, "path"));
  parser.process(app);
  QJsonObject result; QElapsedTimer total; total.start();
  try {
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    const auto request = readJson(parser.value("input"));
    const auto target = request["target"].toString();
    const auto selectedSource = request["source"].toString("auto");
    if ((target != "zh" && target != "en") ||
        (selectedSource != "auto" && selectedSource != "en" && selectedSource != "zh"))
      throw std::runtime_error("本试用版支持中文和英文互译。");
    const auto modelsRoot = parser.value("models");
    const auto manifest = readJson(modelsRoot + "/models.json")["models"].toObject();
    const auto blocks = request["blocks"].toArray();
    if (blocks.isEmpty() || blocks.size() > 4096) throw std::runtime_error("没有可翻译的文字，或文字区域过多。");
    const QString direction = (target == "zh" ? "en-zh" : "zh-en");
    std::unique_ptr<ctranslate2::Translator> translator;
    sentencepiece::SentencePieceProcessor sourceTokenizer, targetTokenizer;
    QJsonArray translated; int done = 0, outputTokens = 0, protectedTerms = 0, termFallbacks = 0;
    QStringList pageTexts; for(const auto &block:blocks)pageTexts<<block.toObject()["text"].toString();
    const TranslationTerminology terminology(pageTexts);
    const auto load = [&] {
      if (translator) return;
      event({{"phase", "loading"}, {"done", done}, {"total", blocks.size()}});
      const auto folder = modelsRoot + '/' + direction;
      if (!manifest.contains(direction)) throw std::runtime_error("缺少所选方向的离线语言包。");
      verifyModel(folder, manifest[direction].toObject());
      QFile sourceFile(folder + "/source.spm"), targetFile(folder + "/target.spm");
      if (!sourceFile.open(QIODevice::ReadOnly) || !targetFile.open(QIODevice::ReadOnly))
        throw std::runtime_error("翻译分词模型缺失。");
      const auto inputStatus = sourceTokenizer.LoadFromSerializedProto(sourceFile.readAll().toStdString());
      const auto outputStatus = targetTokenizer.LoadFromSerializedProto(targetFile.readAll().toStdString());
      if (!inputStatus.ok() || !outputStatus.ok()) throw std::runtime_error("无法读取翻译分词模型。");
      ctranslate2::ReplicaPoolConfig config;
      config.num_threads_per_replica = qBound(1, QThread::idealThreadCount() / 2, 4);
      translator = std::make_unique<ctranslate2::Translator>(folder.toStdString(), ctranslate2::Device::CPU,
                                    ctranslate2::ComputeType::INT8, std::vector<int>{0}, false, config);
    };
    const auto translateText = [&](const QString &text) {
        load();
        std::vector<std::string> tokens;
        const auto status = sourceTokenizer.Encode(text.toStdString(), &tokens);
        if (!status.ok()) throw std::runtime_error("文字分词失败。");
        QStringList pieces;
        // Split explicitly instead of silently truncating the model input.
        for (size_t start = 0; start < tokens.size();) {
          size_t end = std::min(start + 192, tokens.size());
          if (end < tokens.size()) {
            for (size_t split = end; split > start + 80; --split) {
              const auto &token = tokens[split - 1];
              if (token == "." || token == "!" || token == "?" || token == "。" || token == ";") { end = split; break; }
            }
          }
          std::vector<std::string> input(tokens.begin() + start, tokens.begin() + end);
          // opus-mt-en-zh requires a target-language token in the source.
          // It selects simplified Mandarin and also prevents degenerate output
          // for standalone UI labels. Add it to every explicitly split segment.
          if (target == "zh") input.insert(input.begin(), ">>cmn_Hans<<");
          input.emplace_back("</s>");
          ctranslate2::TranslationOptions options;
          options.beam_size = 2; options.max_input_length = 0;
          options.max_decoding_length = 512;
          const auto results = translator->translate_batch({input}, options);
          if (results.empty() || results[0].hypotheses.empty()) throw std::runtime_error("翻译模型没有返回结果。");
          auto translatedTokens = results[0].hypotheses[0];
          // A sentence hitting the decoding limit is not a complete translation.
          if (translatedTokens.size() >= options.max_decoding_length)
            throw std::runtime_error("该段译文过长，已保留原文。请缩小截图后重试。");
          std::string decoded;
          if (!targetTokenizer.Decode(translatedTokens, &decoded).ok()) throw std::runtime_error("译文解码失败。");
          pieces << QString::fromStdString(decoded); outputTokens += int(translatedTokens.size()); start = end;
        }
        const auto output = pieces.join(target == "zh" ? QString() : QString(" ")).trimmed();
        if (output.isEmpty()) throw std::runtime_error("翻译结果为空，已保留原文。");
        return output;
    };
    for (const auto &value : blocks) {
      const auto block = value.toObject();
      const auto text = block["text"].toString().trimmed();
      if (text.size() > 16000) throw std::runtime_error("单个文字区域过长，请缩小截图区域。");
      const auto language = selectedSource == "auto" ? (hasChinese(text) ? "zh" : "en") : selectedSource;
      const auto protectedText=terminology.protect(text);
      protectedTerms+=int(protectedText.replacements.size());
      QString output = text;
      const bool unchanged = !hasLetters(text) || language == target || protectedText.onlyTerms;
      if (!unchanged) {
        output=translateText(protectedText.masked);
        if(!protectedText.restore(&output)) {
          // A changed, missing or duplicated placeholder must never turn a
          // product name into an invented translation. Fall back to translating
          // prose spans while copying every protected identifier verbatim.
          ++termFallbacks; output.clear();
          for(const auto &piece:protectedText.pieces) {
            const auto part=piece.protectedTerm || !hasLetters(piece.text) ? piece.text : translateText(piece.text.trimmed());
            if(target=="en" && !output.isEmpty() && !part.isEmpty() && output.back().isLetterOrNumber() && part.front().isLetterOrNumber())output+=' ';
            output+=part;
          }
        }
      }
      QJsonObject item{{"id", block["id"]}, {"text", output}, {"source", language}, {"unchanged", unchanged}};
      translated.append(item); ++done;
      event({{"phase", "translating"}, {"done", done}, {"total", blocks.size()}});
    }
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
    result = {{"blocks", translated}, {"elapsed_ms", total.elapsed()}, {"output_tokens", outputTokens},
              {"peak_working_set", qint64(memory.PeakWorkingSetSize)}, {"private_bytes", qint64(memory.PrivateUsage)},
              {"protected_terms",protectedTerms},{"term_fallbacks",termFallbacks},
              {"engine", "CTranslate2 4.6.0 / Ruy / CPU INT8"}};
  } catch (const std::exception &error) {
    result = {{"error", QString::fromUtf8(error.what())}, {"elapsed_ms", total.elapsed()}};
  }
  QSaveFile output(parser.value("output"));
  const auto data = QJsonDocument(result).toJson(QJsonDocument::Compact);
  if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) return 2;
  return result.contains("error") ? 1 : 0;
}
