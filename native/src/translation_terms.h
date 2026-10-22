#pragma once
#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QStringList>

struct TranslationTextPiece { QString text; bool protectedTerm = false; };
struct ProtectedTranslationText {
  QString original, masked;
  QList<TranslationTextPiece> pieces;
  QList<QPair<QString,QString>> replacements;
  bool onlyTerms = false;
  bool restore(QString *translation) const;
};

// Product identifiers learned from the whole page also protect lowercase aliases
// in prose. Ordinary capitalized words still go through the translation model.
class TranslationTerminology {
public:
  explicit TranslationTerminology(const QStringList &page);
  ProtectedTranslationText protect(const QString &text) const;
private:
  QSet<QString> names;
};
