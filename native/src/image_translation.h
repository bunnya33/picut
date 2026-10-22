#pragma once
#include "translation.h"
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>
#include <functional>

class Store;
class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QToolButton;
class TranslationCanvas;

class ImageTranslationOverlay : public QWidget {
  Q_OBJECT
public:
  ImageTranslationOverlay(QWidget *parent, Store *store, std::function<QRect()> imageRect);
  ~ImageTranslationOverlay() override;
  void begin(const QImage &image, bool recognize = true);
  void finish();
  void setOcrResult(const QJsonObject &result, bool translate = true);
  void setTranslationResult(const QJsonObject &result);
  const TranslationPage &page() const { return document; }
  const QImage &sourceImage() const { return source; }
  const QImage &translatedImage() const { return translated; }
  bool showingOriginal() const;
  bool hasCompletedTranslation() const { return !completedKey.isEmpty(); }
  QImage displayedImage() const { return hasCompletedTranslation() ? (showingOriginal() ? source : translated) : QImage(); }
  QRect sourceGlobalRect() const;
  void setShowingOriginal(bool original);
  void setPanelVisible(bool visible);
  QPointF imageToView(QPointF point) const;
signals:
  void closed();
protected:
  bool eventFilter(QObject *object, QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
private:
  Store *store;
  std::function<QRect()> imageRect;
  PaddleOcrJob ocr;
  LocalTranslationJob translator;
  TranslationPage document;
  QImage source, translated;
  TranslationCanvas *canvas;
  QPlainTextEdit *editor;
  QLabel *status;
  QComboBox *sourceLanguage, *targetLanguage;
  QToolButton *compare, *panelToggle, *copy, *save, *retry;
  bool active = false, ready = false, syncing = false;
  QString completedKey;
  QString cachedProfile, cachedAcceleration;
  QHash<QString, TranslationPage> cache;
  QTimer renderTimer;
  void translate();
  void refresh();
  void refreshEditor();
  void selectBlock(int index);
  void editorSelectionChanged();
  void copyImage();
  void saveImage();
  void place();
};
