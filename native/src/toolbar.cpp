#include "toolbar.h"
#include "compact_ui.h"
#include <QtCore/QSignalBlocker>
#include <QtGui/QDoubleValidator>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidgetAction>

QIcon toolIcon(const QString &name, QColor color) {
  static QMap<QString, QIcon> cache;
  const auto key = name + color.name(QColor::HexArgb);
  if (cache.contains(key))
    return cache.value(key);
  QPixmap glyph(":/toolbar/" + name + ".png");
  if (!glyph.isNull()) {
    QIcon icon;
    for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
      for (int size : {16, 20, 24, 28, 32, 48, 64}) {
        auto image = glyph.scaled(size, size, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
        QPainter p(&image);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(image.rect(), mode == QIcon::Disabled ? QColor("#7b7b7b")
                                                       : color);
        p.end();
        icon.addPixmap(image, mode);
      }
    }
    cache.insert(key, icon);
    return icon;
  }
  QPixmap image(24, 24);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  p.setBrush(Qt::NoBrush);
  if (name == "rect" || name == "shapes") {
    p.drawRect(QRectF(4, 5, 15, 13));
    if (name == "shapes")
      p.drawEllipse(QRectF(11, 12, 10, 8));
  } else if (name == "ellipse")
    p.drawEllipse(QRectF(4, 4, 16, 16));
  else if (name == "line")
    p.drawLine(QPointF(4, 19), QPointF(20, 4));
  else if (name == "arrow") {
    Mark m;
    m.width = 2.5;
    m.points = {{3, 20}, {20, 3}};
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(arrowPath(m));
  } else if (name == "pen" || name == "marker") {
    p.translate(12, 12);
    p.rotate(45);
    p.drawRect(QRectF(-3, -9, 6, 14));
    p.drawPolyline(QPolygonF{{-3, 5}, {0, 9}, {3, 5}});
    if (name == "marker")
      p.fillRect(QRectF(-3, -9, 6, 14), color);
  } else if (name == "mosaic") {
    for (int x = 0; x < 2; ++x)
      for (int y = 0; y < 2; ++y)
        p.fillRect(4 + x * 9, 4 + y * 9, 7, 7,
                   (x + y) % 2 ? QColor("#888888") : color);
  } else if (name == "eraser") {
    p.translate(12, 12);
    p.rotate(35);
    p.drawRoundedRect(QRectF(-5, -8, 10, 16), 1, 1);
    p.drawLine(-5, 1, 5, 1);
  } else if (name == "select_text") {
    p.drawPolyline(QPolygonF{{3, 8}, {3, 3}, {8, 3}});
    p.drawPolyline(QPolygonF{{16, 3}, {21, 3}, {21, 8}});
    p.drawPolyline(QPolygonF{{3, 16}, {3, 21}, {8, 21}});
    p.drawPolyline(QPolygonF{{16, 21}, {21, 21}, {21, 16}});
    auto f = p.font(); f.setPixelSize(15); f.setBold(true); p.setFont(f);
    p.drawText(image.rect(), Qt::AlignCenter, "T");
  } else if (name == "number") {
    p.drawEllipse(QRectF(3, 3, 18, 18));
    auto f = p.font();
    f.setBold(true);
    f.setPixelSize(15);
    p.setFont(f);
    p.drawText(image.rect(), Qt::AlignCenter, "1");
  } else if (name == "text") {
    auto f = p.font();
    f.setBold(true);
    f.setPixelSize(20);
    p.setFont(f);
    p.drawText(image.rect(), Qt::AlignCenter, "T");
  } else if (name == "undo" || name == "redo") {
    if (name == "redo") {
      p.translate(24, 0);
      p.scale(-1, 1);
    }
    p.drawArc(6, 7, 14, 13, 20 * 16, 230 * 16);
    p.setBrush(color);
    p.drawPolygon(QPolygonF{{3, 9}, {9, 3}, {9, 13}});
  } else if (name == "cancel") {
    p.drawLine(6, 6, 18, 18);
    p.drawLine(6, 18, 18, 6);
  } else if (name == "copy") {
    p.drawRect(7, 7, 13, 14);
    p.drawPolyline(QPolygonF{{4, 16}, {4, 3}, {16, 3}});
  } else if (name == "save") {
    p.drawRect(4, 4, 16, 16);
    p.drawRect(8, 4, 8, 6);
    p.drawRect(8, 13, 8, 7);
  } else if (name == "pin") {
    p.translate(12, 11);
    p.rotate(35);
    p.setBrush(color);
    p.drawPolygon(
        QPolygonF{{-5, -7}, {5, -7}, {3, -1}, {6, 2}, {-6, 2}, {-3, -1}});
    p.drawLine(0, 2, 0, 11);
  } else {
    p.drawLine(4, 12, 20, 12);
    p.drawLine(12, 4, 12, 20);
  }
  return QIcon(image);
}
// Parameter glyphs stay small and monochrome, matching the main toolbar.
static QIcon optionIcon(const QString &name, QColor color) {
  QPixmap image(16, 16);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setPen(QPen(color, 1));
  if (name == "fill" || name == "area") {
    for (int y = 2; y < 14; ++y)
      for (int x = 1; x < 15; ++x)
        if ((x + y) % 2 == 0)
          p.drawPoint(x, y);
  } else if (name == "rect")
    p.drawRect(1, 3, 14, 10);
  else if (name == "ellipse") {
    p.setRenderHint(QPainter::Antialiasing);
    p.drawEllipse(QRectF(1, 3, 14, 10));
  } else if (name == "blur") {
    QRadialGradient gradient(QPointF(8, 8), 8);
    gradient.setColorAt(0, color);
    gradient.setColorAt(1, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(gradient);
    p.drawEllipse(0, 0, 16, 16);
  } else
    return toolIcon(name, color);
  return QIcon(image);
}
static QIcon lineIcon(const QString &type, QColor color = QColor("#333333")) {
  QImage image(48, 22, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  Mark mark;
  mark.kind = type.startsWith("line") ? "line" : "arrow";
  mark.arrowHead = type.section(':', 1);
  mark.width = 1.8;
  mark.color = color;
  mark.points = {{4, 11}, {43, 11}};
  paintMark(image, mark);
  return QIcon(QPixmap::fromImage(image));
}
static QIcon swatchIcon(QColor color, int size, bool custom = false) {
  QPixmap image(size, size);
  QPainter p(&image);
  for (int y = 0; y < size; y += 4)
    for (int x = 0; x < size; x += 4)
      p.fillRect(x, y, 4, 4, (x / 4 + y / 4) % 2 ? QColor("#aaa")
                                                               : Qt::white);
  p.fillRect(image.rect(), color);
  p.setPen(QColor("#888888"));
  p.drawRect(0, 0, size - 1, size - 1);
  if (custom) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2080f0"));
    p.drawPolygon(QPolygon{{size - 8, size}, {size, size - 8}, {size, size}});
    p.setBrush(Qt::yellow);
    p.drawPolygon(QPolygon{{size - 5, size}, {size, size - 5}, {size, size}});
  }
  return QIcon(image);
}
Toolbar::Toolbar(QWidget *parent) : QWidget(parent) {
  setCursor(Qt::ArrowCursor);
  setAttribute(Qt::WA_TranslucentBackground);
  setStyleSheet(compactToolbarStyle()+
      "QComboBox,QSpinBox{color:#333;background:#fff;border:1px solid "
      "#c5c5c5;padding:2px;}QFontComboBox{color:#333;}QSlider::groove:"
      "horizontal{height:3px;background:#aaa;}QSlider::handle:horizontal{width:"
      "8px;height:8px;margin:-4px 0;background:white;border:2px solid #2080f0;"
      "border-radius:6px;}");
  auto root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(4);
  mainBar = new QFrame;
  mainBar->setObjectName("annotationToolbar");
  auto row = new QHBoxLayout(mainBar);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);
  QStringList names{"shapes", "arrow", "pen",   "marker",
                    "mosaic", "text",  "eraser", "number"};
  QStringList titles{"形状",          "线条 / 箭头", "画笔",  "马克笔",
                     "马赛克 / 模糊", "文字",        "橡皮擦", "序号"};
  for (int n = 0; n < names.size(); ++n) {
    auto b = new QToolButton;
    b->setObjectName("tool_" + names[n]);
    b->setIcon(toolIcon(names[n]));
    b->setIconSize({16, 16});
    b->setFixedSize(32, 32);
    b->setCheckable(true);
    b->setToolTip(titles[n] + " · Ctrl+" + QString::number(n + 1));
    row->addWidget(b);
    tools.append(b);
    connect(b, &QToolButton::clicked, this, [this, n] {
      emit toolChosen(groupTools.value(n, QStringList{"rect", "arrow", "pen", "marker", "mosaic",
                                                      "text", "eraser", "number"}[n]));
    });
  }
  auto separator = [&] {
    auto gap = new QWidget;
    gap->setFixedSize(5, 32);
    auto line = new QFrame(gap);
    line->setObjectName("toolbarSeparator");
    line->setGeometry(2, 9, 1, 14);
    line->setStyleSheet("background:#858585;border:0;");
    row->addWidget(gap);
  };
  separator();
  for (const auto &action :
       QStringList{"undo", "redo", "select_text", "cancel", "pin", "save", "copy"}) {
    if (action == "cancel")
      separator();
    auto b = new QToolButton;
    b->setObjectName("action_" + action);
    b->setIcon(toolIcon(action));
    b->setIconSize({16, 16});
    b->setFixedSize(32, 32);
    b->setToolTip(QMap<QString, QString>{{"undo", "撤销 · Ctrl+Z"},
                                         {"redo", "重做 · Ctrl+Y"},
                                         {"select_text", "文字识别 · F4"},
                                         {"cancel", "取消 · Esc"},
                                         {"pin", "贴图 · F3"},
                                         {"save", "保存 · Ctrl+S"},
                                         {"copy", "复制 · Enter"}}[action]);
    row->addWidget(b);
    connect(b, &QToolButton::clicked, this,
            [this, action] { emit actionRequested(action); });
  }
  root->addWidget(mainBar, 0, Qt::AlignLeft);
  optionsBar = new QFrame;
  optionsBar->setObjectName("annotationOptions");
  auto options = new QHBoxLayout(optionsBar);
  options->setSizeConstraint(QLayout::SetFixedSize);
  options->setContentsMargins(0, 0, 3, 0);
  options->setSpacing(0);
  widthButton = new QToolButton;
  widthButton->setObjectName("annotationWidth");
  widthButton->setFixedSize(32, 32);
  widthButton->setFocusPolicy(Qt::NoFocus);
  widthButton->setToolTip("笔尖粗细 · 滚轮 / 1、2");
  options->addWidget(widthButton);
  connect(widthButton, &QToolButton::clicked, this, [this] {
    if (currentTool == "rect" || currentTool == "ellipse") {
      if (currentFilled)
        emit preferencesChanged("filled", false);
    } else if (QStringList{"marker", "mosaic", "blur", "eraser"}.contains(currentTool)) {
      if (currentAreaShape != "stroke")
        emit preferencesChanged("area_shape", "stroke");
    } else {
      showWidthMenu();
    }
  });
  widthButton->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(widthButton, &QWidget::customContextMenuRequested, this,
          [this] { showWidthMenu(); });
  auto option = [&](const QString &name, const QString &tip) {
    auto button = new QToolButton;
    button->setObjectName(name);
    button->setFixedSize(32, 32);
    button->setIconSize({16, 16});
    button->setFocusPolicy(Qt::NoFocus);
    button->setCheckable(true);
    button->setToolTip(tip);
    options->addWidget(button);
    return button;
  };
  auto optionSeparator = [&](const QString &name) {
    auto gap = new QWidget;
    gap->setObjectName(name);
    gap->setFixedSize(5, 32);
    auto line = new QFrame(gap);
    line->setGeometry(2, 9, 1, 14);
    line->setStyleSheet("background:#858585;border:0;");
    options->addWidget(gap);
    return gap;
  };
  fillButton = option("shapeFill", "填充 · 再次点击切换空心 / 实心");
  connect(fillButton, &QToolButton::clicked, this,
          [this](bool checked) { emit preferencesChanged("filled", checked); });
  shapeSeparator = optionSeparator("shapeSeparator");
  rectButton = option("shapeRect", "矩形");
  ellipseButton = option("shapeEllipse", "椭圆");
  connect(rectButton, &QToolButton::clicked, this, [this] { emit toolChosen("rect"); });
  connect(ellipseButton, &QToolButton::clicked, this, [this] { emit toolChosen("ellipse"); });
  lineButton = new QToolButton;
  lineButton->setObjectName("lineStyle");
  lineButton->setFixedSize(58, 24);
  lineButton->setIconSize({40, 20});
  lineButton->setFocusPolicy(Qt::NoFocus);
  lineButton->setToolTip("线条 / 箭头样式");
  lineButton->setStyleSheet("QToolButton{background:white;border:1px solid #c5c5c5;}"
                            "QToolButton::menu-indicator{width:10px;}");
  auto lineMenu = new QMenu(lineButton);
  lineMenu->setObjectName("lineStyleMenu");
  const QStringList lineTypes{"line:", "arrow:tapered", "arrow:filled", "arrow:hollow"};
  const QStringList lineTitles{"直线", "渐细箭头", "实心箭头", "空心箭头"};
  for (int n = 0; n < lineTypes.size(); ++n) {
    auto action = lineMenu->addAction(lineIcon(lineTypes[n]), lineTitles[n]);
    action->setData(lineTypes[n]);
    action->setCheckable(true);
    connect(action, &QAction::triggered, this,
            [this, type = lineTypes[n]] { emit preferencesChanged("line_type", type); });
  }
  lineButton->setMenu(lineMenu);
  lineButton->setPopupMode(QToolButton::InstantPopup);
  options->addWidget(lineButton);
  directionButton = new QToolButton;
  directionButton->setText("→");
  directionButton->setObjectName("arrowDirection");
  directionButton->setFixedSize(32, 32);
  directionButton->setFocusPolicy(Qt::NoFocus);
  directionButton->setToolTip("箭头方向：正向 / 反向 / 双向");
  options->addWidget(directionButton);
  connect(directionButton, &QToolButton::clicked, this,
          [this] { emit preferencesChanged("direction_cycle", true); });
  areaButton = option("areaRect", "矩形区域 · 再次点击切回画笔");
  areaEllipseButton = option("areaEllipse", "椭圆区域 · 再次点击切回画笔");
  connect(areaButton, &QToolButton::clicked, this, [this](bool checked) {
    emit preferencesChanged("area_shape", checked ? "rect" : "stroke");
  });
  connect(areaEllipseButton, &QToolButton::clicked, this, [this](bool checked) {
    emit preferencesChanged("area_shape", checked ? "ellipse" : "stroke");
  });
  effectSeparator = optionSeparator("effectSeparator");
  mosaicButton = option("effectMosaic", "马赛克");
  blurButton = option("effectBlur", "模糊");
  connect(mosaicButton, &QToolButton::clicked, this, [this] { emit toolChosen("mosaic"); });
  connect(blurButton, &QToolButton::clicked, this, [this] { emit toolChosen("blur"); });
  strength = new QSlider(Qt::Horizontal);
  strength->setRange(2, 40);
  strength->setFixedWidth(90);
  strength->setToolTip("处理强度");
  options->addWidget(strength);
  strengthValue = new QLabel;
  strengthValue->setObjectName("effectStrengthValue");
  strengthValue->setAlignment(Qt::AlignCenter);
  strengthValue->setFixedWidth(27);
  strengthValue->setStyleSheet("color:#111;border:0;background:transparent;");
  options->addWidget(strengthValue);
  connect(strength, &QSlider::valueChanged, this, [this](int v) {
    if (!syncing)
      emit preferencesChanged("intensity", v);
  });
  textControls = new QWidget;
  auto textRow = new QHBoxLayout(textControls);
  textRow->setContentsMargins(0, 0, 0, 0);
  textRow->setSpacing(0);
  boldButton = new QToolButton;
  boldButton->setObjectName("textBold");
  boldButton->setToolTip("加粗");
  boldButton->setCheckable(true);
  italicButton = new QToolButton;
  italicButton->setObjectName("textItalic");
  italicButton->setToolTip("斜体");
  italicButton->setCheckable(true);
  outlineButton = new QToolButton;
  outlineButton->setObjectName("textOutline");
  outlineButton->setToolTip("文字描边");
  outlineButton->setCheckable(true);
  for (auto button : {boldButton, italicButton, outlineButton}) {
    button->setFixedSize(32, 30);
    button->setFocusPolicy(Qt::NoFocus);
    textRow->addWidget(button);
  }
  for (auto button : {boldButton, italicButton, outlineButton})
    button->setIconSize({16, 16});
  connect(boldButton, &QToolButton::toggled, this, [this](bool v) {
    if (!syncing)
      emit preferencesChanged("bold", v);
  });
  connect(italicButton, &QToolButton::toggled, this, [this](bool v) {
    if (!syncing)
      emit preferencesChanged("italic", v);
  });
  connect(outlineButton, &QToolButton::toggled, this, [this](bool v) {
    if (!syncing)
      emit preferencesChanged("text_outline_enabled", v);
  });
  textRow->addSpacing(5);
  fontBox = new QFontComboBox;
  fontBox->setObjectName("textFontFamily");
  fontBox->setToolTip("字体");
  fontBox->setFixedSize(139, 22);
  textRow->addWidget(fontBox);
  connect(fontBox, &QFontComboBox::currentFontChanged, this,
          [this](const QFont &f) {
            if (!syncing)
              emit preferencesChanged("font_family", f.family());
          });
  textRow->addSpacing(5);
  fontSize = new QComboBox;
  fontSize->setObjectName("textFontSize");
  fontSize->setToolTip("字号（磅），支持输入小数");
  fontSize->setEditable(true);
  fontSize->setInsertPolicy(QComboBox::NoInsert);
  fontSize->setFixedSize(59, 22);
  for (double size : {5., 6., 7., 8., 8.75, 9., 10., 10.5, 11., 12., 14.,
                      16., 18., 20., 24., 28., 32., 36., 48., 72.})
    fontSize->addItem(QString::number(size, 'g', 5));
  auto validator = new QDoubleValidator(1, 400, 2, fontSize);
  validator->setNotation(QDoubleValidator::StandardNotation);
  validator->setLocale(QLocale::c());
  fontSize->lineEdit()->setValidator(validator);
  textRow->addWidget(fontSize);
  auto applySize = [this] {
    bool ok = false;
    const auto value = fontSize->currentText().toDouble(&ok);
    if (!syncing && ok && value >= 1 && value <= 400)
      emit preferencesChanged("font_size", value * 4.0 / 3);
  };
  connect(fontSize, &QComboBox::activated, this, [applySize](int) { applySize(); });
  connect(fontSize->lineEdit(), &QLineEdit::editingFinished, this, applySize);
  options->addWidget(textControls);
  colorSeparator = optionSeparator("colorSeparator");
  colorButton = new QToolButton;
  colorButton->setFocusPolicy(Qt::NoFocus);
  colorButton->setObjectName("annotationColor");
  colorButton->setFixedSize(24, 24);
  colorButton->setIconSize({24, 24});
  colorButton->setToolTip("自定义颜色与透明度");
  options->addWidget(colorButton);
  connect(colorButton, &QToolButton::clicked, this, [this] {
    auto value = QColorDialog::getColor(
        QColor(colorButton->property("color").toString()), this, "标注颜色",
        QColorDialog::ShowAlphaChannel);
    if (value.isValid())
      emit preferencesChanged("color", value);
  });
  auto colors = new QWidget;
  colors->setObjectName("annotationPalette");
  auto grid = new QGridLayout(colors);
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setSpacing(1);
  QStringList values{"#000000", "#7f7f7f", "#880015", "#ed1c24", "#ff7f27",
                     "#fff200", "#22b14c", "#00a2e8", "#3f48cc", "#a349a4",
                     "#ffffff", "#c3c3c3", "#b97a57", "#ffaec9",
                     "#ffc90e", "#efe4b0", "#b5e61d", "#99d9ea", "#7092be",
                     "#c8bfe7"};
  for (int n = 0; n < values.size(); ++n) {
    auto b = new QToolButton;
    b->setFixedSize(12, 12);
    b->setFocusPolicy(Qt::NoFocus);
    b->setProperty("color", values[n]);
    b->setIconSize({12, 12});
    b->setIcon(swatchIcon(QColor(values[n]), 12));
    grid->addWidget(b, n / 10, n % 10);
    connect(b, &QToolButton::clicked, this, [this, value = values[n]] {
      emit preferencesChanged("color", QColor(value));
    });
  }
  options->addWidget(colors);
  palette.append(colors);
  root->addWidget(optionsBar, 0, Qt::AlignLeft);
  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, &QWidget::customContextMenuRequested, this,
          [this] { emit actionRequested("more"); });
}
void Toolbar::showWidthMenu() {
  if (!widthButton->isEnabled())
    return;
  QMenu menu(this);
  menu.setObjectName("annotationWidthMenu");
  auto slider = new QSlider(Qt::Horizontal);
  slider->setObjectName("annotationWidthSlider");
  slider->setRange(currentTool == "number" ? 8 : 1,
                   currentTool == "number" ? 200 : 100);
  slider->setValue(qRound(currentWidth));
  slider->setFixedSize(150, 26);
  auto action = new QWidgetAction(&menu);
  action->setDefaultWidget(slider);
  menu.addAction(action);
  connect(slider, &QSlider::valueChanged, this, [this](int value) {
    emit preferencesChanged(currentTool == "number" ? "font_size" : "width",
                            currentTool == "marker" ? value / 3.0 : value);
  });
  menu.exec(widthButton->mapToGlobal(QPoint(0, widthButton->height())));
}
void Toolbar::sync(const Annotation &a) {
  syncing = true;
  currentTool = a.tool;
  currentAreaShape = a.preferences.areaShape;
  currentFilled = a.preferences.filled;
  currentWidth = a.tool == "number" ? a.preferences.fontSize
                 : a.tool == "marker" ? a.preferences.width * 3
                                      : a.preferences.width;
  int group = a.tool == "rect" || a.tool == "ellipse" ? 0
              : a.tool == "line" || a.tool == "arrow" || a.tool == "polyline"
                  ? 1
              : a.tool == "pen"                        ? 2
              : a.tool == "marker"                     ? 3
              : a.tool == "mosaic" || a.tool == "blur" ? 4
              : a.tool == "text"                       ? 5
              : a.tool == "eraser"                     ? 6
              : a.tool == "number"                     ? 7
                                                       : -1;
  if (group >= 0 && a.tool != "polyline")
    groupTools[group] = a.tool;
  for (int n = 0; n < tools.size(); ++n) {
    tools[n]->setChecked(n == group);
    tools[n]->setIcon(toolIcon(QStringList{"shapes", "arrow", "pen", "marker",
                                           "mosaic", "text", "eraser", "number"}[n],
                               n == group ? Qt::white : QColor("#333333")));
  }
  auto buttonState = [](QToolButton *button, const QString &glyph, bool visible,
                        bool checked) {
    button->setVisible(visible);
    button->setChecked(checked);
    button->setIcon(optionIcon(glyph, checked ? Qt::white : QColor("#333333")));
  };
  buttonState(fillButton, "fill", group == 0, a.preferences.filled);
  shapeSeparator->setVisible(group == 0);
  buttonState(rectButton, "rect", group == 0, a.tool == "rect");
  buttonState(ellipseButton, "ellipse", group == 0, a.tool == "ellipse");
  lineButton->setVisible(group == 1);
  const auto lineType = a.tool == "line" ? "line:" : "arrow:" + a.preferences.arrowHead;
  lineButton->setIcon(lineIcon(lineType));
  for (auto action : lineButton->menu()->actions())
    action->setChecked(action->data().toString() == lineType);
  directionButton->setVisible(group == 1);
  directionButton->setEnabled(a.tool == "arrow");
  directionButton->setText(a.preferences.arrowMode == "both"      ? "↔"
                           : a.preferences.arrowMode == "reverse" ? "←"
                                                                  : "→");
  buttonState(areaButton, "area", group == 3 || group == 4 || group == 6,
              a.preferences.areaShape == "rect");
  buttonState(areaEllipseButton, "ellipse", group == 4 || group == 6,
              a.preferences.areaShape == "ellipse");
  effectSeparator->setVisible(group == 4);
  buttonState(mosaicButton, "mosaic", group == 4, a.tool == "mosaic");
  buttonState(blurButton, "blur", group == 4, a.tool == "blur");
  strength->setVisible(group == 4);
  strength->setValue(a.preferences.intensity);
  strengthValue->setVisible(group == 4);
  strengthValue->setText(QString::number(a.preferences.intensity));
  textControls->setVisible(group == 5);
  widthButton->setVisible(group >= 0 && group != 5);
  auto options = qobject_cast<QHBoxLayout *>(optionsBar->layout());
  options->setContentsMargins(0, 0, 3, 0);
  qobject_cast<QVBoxLayout *>(layout())->setSpacing(2);
  boldButton->setChecked(a.preferences.bold);
  italicButton->setChecked(a.preferences.italic);
  outlineButton->setChecked(a.preferences.outlineWidth > 0);
  boldButton->setIcon(toolIcon("text-bold", a.preferences.bold ? Qt::white
                                                             : QColor("#333333")));
  italicButton->setIcon(toolIcon("text-italic", a.preferences.italic ? Qt::white
                                                                 : QColor("#333333")));
  outlineButton->setIcon(toolIcon("text-outline", a.preferences.outlineWidth ? Qt::white
                                                                          : QColor("#333333")));
  fontBox->setCurrentFont(QFont(a.preferences.fontFamily));
  auto sizeLabel = QString::number(a.preferences.fontSize * .75, 'f', 2);
  while (sizeLabel.endsWith('0'))
    sizeLabel.chop(1);
  if (sizeLabel.endsWith('.'))
    sizeLabel.chop(1);
  fontSize->setCurrentText(sizeLabel);
  bool color = group != 4 && group != 6 && group >= 0;
  colorSeparator->setVisible(color);
  colorButton->setVisible(color);
  for (auto w : palette)
    w->setVisible(color);
  colorButton->setProperty("color", a.preferences.color.name(QColor::HexArgb));
  auto displayColor = a.preferences.color;
  if (group == 3)
    displayColor.setAlpha(qMin(100, displayColor.alpha()));
  colorButton->setIcon(swatchIcon(displayColor, 24, true));
  for (auto widget : palette)
    for (auto button : widget->findChildren<QToolButton *>()) {
      auto value = QColor(button->property("color").toString());
      if (group == 3)
        value.setAlpha(qMin(100, a.preferences.color.alpha()));
      button->setIcon(swatchIcon(value, 12));
    }
  QPixmap dot(22, 22);
  dot.fill(Qt::transparent);
  QPainter p(&dot);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(group == 0 && a.preferences.filled ? QColor("#333333") : Qt::white);
  double r = qBound(1.5, a.preferences.width / 2, 9.0);
  p.drawEllipse(QPointF(11, 11), r, r);
  p.end();
  const bool numericWidth = group == 3 || group == 4 || group == 6 || group == 7;
  widthButton->setIcon(numericWidth ? QIcon() : QIcon(dot));
  widthButton->setText(numericWidth ? QString::number(qRound(currentWidth)) : QString());
  const bool activeWidth = !(group == 0 && a.preferences.filled) &&
                          !((group == 3 || group == 4 || group == 6) &&
                            a.preferences.areaShape != "stroke");
  widthButton->setStyleSheet(QString("background:%1;color:%2;border:0;")
                                .arg(activeWidth ? "#2080f0" : "#f2f2f2",
                                     activeWidth ? "white" : "#111"));
  widthButton->setEnabled(!(a.tool == "pen" && a.draft));
  widthButton->setToolTip(group == 0 ? "空心描边 · 单击选择；滚轮 / 1、2 或右键调整粗细"
                         : group == 3 ? "直线画笔 · 单击选择；滚轮 / 1、2 或右键调整粗细"
                         : group == 4 || group == 6 ? "画笔模式 · 单击选择；滚轮 / 1、2 或右键调整粗细"
                         : group == 7 ? "序号大小 · 单击或右键打开滑块，滚轮 / 1、2；下一个序号：" +
                                            QString::number(a.preferences.number)
                         : a.tool == "pen" ? "画笔粗细 · 绘制前用滚轮 / 1、2 或右键调整"
                                           : "笔尖粗细 · 滚轮 / 1、2 或右键调整");
  optionsBar->setVisible(group >= 0);
  findChild<QToolButton *>("action_undo")->setEnabled(!a.undoStates.isEmpty());
  findChild<QToolButton *>("action_redo")->setEnabled(!a.redoStates.isEmpty());
  optionsBar->layout()->activate();
  layout()->activate();
  adjustSize();
  syncing = false;
}
