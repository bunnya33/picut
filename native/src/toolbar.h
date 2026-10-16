#pragma once
#include "annotation.h"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFontComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

class Toolbar : public QWidget {
  Q_OBJECT
public:
  explicit Toolbar(QWidget *parent = nullptr);
  void sync(const Annotation &annotation);
  QFrame *mainBar;
  QFrame *optionsBar;
  QToolButton *lineButton;
  QToolButton *fillButton;
  QToolButton *rectButton;
  QToolButton *ellipseButton;
  QToolButton *areaButton;
  QToolButton *areaEllipseButton;
  QToolButton *mosaicButton;
  QToolButton *blurButton;
  QToolButton *widthButton;
  QToolButton *colorButton;
  QToolButton *directionButton;
  QFontComboBox *fontBox;
  QComboBox *fontSize;
  QToolButton *boldButton;
  QToolButton *italicButton;
  QToolButton *outlineButton;
  QWidget *textControls;
  QSlider *strength;
  QLabel *strengthValue;
signals:
  void toolChosen(QString tool);
  void preferencesChanged(QString key, QVariant value);
  void actionRequested(QString action);

private:
  void showWidthMenu();
  QList<QToolButton *> tools;
  QMap<int, QString> groupTools;
  QList<QWidget *> palette;
  QWidget *shapeSeparator, *colorSeparator, *effectSeparator;
  bool syncing = false;
  double currentWidth = 4;
  QString currentTool;
  QString currentAreaShape = "stroke";
  bool currentFilled = false;
};
QIcon toolIcon(const QString &name, QColor color = QColor("#333333"));
