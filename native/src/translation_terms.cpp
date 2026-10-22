#include "translation_terms.h"
#include <QtCore/QRegularExpression>
#include <algorithm>

static const QRegularExpression &wordPattern() {
  static const QRegularExpression pattern(
      R"((?:https?://|www\.)[^\s<>"“”]+|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}|[A-Za-z]:[\\/][^\s<>"|]+|(?:[A-Za-z0-9-]+\.)+(?:com|cn|net|org|io|dev|ai|app|edu|gov)(?:/[^\s<>"“”]*)?|\.NET(?:\s+(?:Core|Framework))?|[CF](?:\+\+|#)|[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)*)",
      QRegularExpression::CaseInsensitiveOption);
  return pattern;
}
static bool intrinsicTerm(QString word) {
  static const QSet<QString> ordinary=[] { const auto words=QString(
      "A I AN THE AND OR OF TO IN ON AT IS ARE WAS WERE BE BEEN BY AS FOR FROM WITH THIS THAT THESE THOSE "
      "IT ITS NOT NO YES OK OFF OPEN CLOSE SAVE COPY PASTE SETTINGS CANCEL INSTALL INSTALLATION INTRODUCTION "
      "GUIDE LOGO MY SITE HIGH QUICK START NATIVE CODE MEMORY PERFORMANCE TEST HELLO WORLD IMAGE TEXT "
      "HOME HELP FILE EDIT VIEW TOOLS ABOUT NEW ALL GET SET ADD REMOVE DELETE UPDATE MORE RESULTS").split(' ');
      return QSet<QString>(words.begin(),words.end()); }();
  if(word.contains(QRegularExpression(R"([:/\\@.+#])")))return true;
  if(word.contains('_') || word.contains(QRegularExpression("[0-9]")))return true;
  if(word.contains(QRegularExpression("[a-z][A-Z]")))return true;
  return word.size()>=2 && word.size()<=12 && word==word.toUpper() && !ordinary.contains(word);
}
TranslationTerminology::TranslationTerminology(const QStringList &page) {
  for(const auto &text:page) {
    auto matches=wordPattern().globalMatch(text);
    while(matches.hasNext()) {
      auto word=matches.next().captured();
      if(intrinsicTerm(word))names.insert(word.toCaseFolded());
    }
  }
}
ProtectedTranslationText TranslationTerminology::protect(const QString &text, const QString &targetLanguage) const {
  ProtectedTranslationText result; result.original=text;
  struct Span { qsizetype start, end; };
  QList<Span> spans;
  auto matches=wordPattern().globalMatch(text);
  while(matches.hasNext()) {
    const auto match=matches.next(); auto word=match.captured();
    // Sentence punctuation is outside a URL/path. Keep it available to the model.
    while(word.size()>1 && QString(".,;:!?)]}").contains(word.back()))word.chop(1);
    if(!intrinsicTerm(word) && !names.contains(word.toCaseFolded()))continue;
    spans.append({match.capturedStart(),match.capturedStart()+word.size()});
    ++result.technicalTerms;
  }
  // Existing text in the target language is kept verbatim even when OCR joins
  // it to a sentence in the other language (for example a Chinese citation).
  if(targetLanguage=="zh" || targetLanguage=="en") {
    const QRegularExpression targetText(targetLanguage=="zh" ? QString("\\p{Han}+") :
        QString(R"([A-Za-z][A-Za-z0-9\s.,:;!?'"()\[\]/+#_\-]*)"));
    auto targets=targetText.globalMatch(text);
    while(targets.hasNext()) {
      const auto match=targets.next(); spans.append({match.capturedStart(),match.capturedEnd()});
      ++result.preservedLanguageSpans;
    }
  }
  std::sort(spans.begin(),spans.end(),[](const auto &a,const auto &b){return a.start<b.start;});
  QList<Span> merged;
  for(const auto &span:spans) {
    if(!merged.isEmpty() && span.start<=merged.last().end) merged.last().end=qMax(merged.last().end,span.end);
    else merged.append(span);
  }
  qsizetype previous=0; QString natural, prefix="ZXQ";
  while(text.contains(prefix,Qt::CaseInsensitive))prefix+='X';
  for(const auto &span:merged) {
    const auto word=text.mid(span.start,span.end-span.start);
    const auto gap=text.mid(previous,span.start-previous);
    if(!gap.isEmpty())result.pieces.append({gap,false});
    natural+=gap; result.masked+=gap;
    const auto marker=prefix+QString::number(result.replacements.size()).rightJustified(3,'0')+"QXZ";
    result.replacements.append({marker,word}); result.masked+=marker;
    result.pieces.append({word,true}); previous=span.end;
  }
  const auto tail=text.mid(previous); result.masked+=tail; natural+=tail;
  if(!tail.isEmpty())result.pieces.append({tail,false});
  result.onlyTerms=!result.replacements.isEmpty() && !natural.contains(QRegularExpression("\\p{L}"));
  return result;
}
bool ProtectedTranslationText::restore(QString *translation) const {
  auto value=*translation;
  for(const auto &entry:replacements) {
    QString pattern;
    for(const auto c:entry.first){if(!pattern.isEmpty())pattern+="\\s*";pattern+=QRegularExpression::escape(QString(c));}
    const QRegularExpression marker(pattern,QRegularExpression::CaseInsensitiveOption);
    auto matches=marker.globalMatch(value);
    if(!matches.hasNext())return false;
    const auto found=matches.next(); if(matches.hasNext())return false;
    value.replace(found.capturedStart(),found.capturedLength(),entry.second);
  }
  *translation=value; return true;
}
