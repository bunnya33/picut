#pragma once
#include <QtGui/QPalette>
#include <QtWidgets/QWidget>

inline void applyPanelTheme(QWidget *widget) {
  auto palette = widget->palette();
  palette.setColor(QPalette::Window, QColor("#f0f0f0"));
  palette.setColor(QPalette::WindowText, QColor("#202020"));
  palette.setColor(QPalette::Base, Qt::white);
  palette.setColor(QPalette::AlternateBase, QColor("#f7f7f7"));
  palette.setColor(QPalette::Text, QColor("#202020"));
  palette.setColor(QPalette::Button, QColor("#fafafa"));
  palette.setColor(QPalette::ButtonText, QColor("#202020"));
  palette.setColor(QPalette::Highlight, QColor("#0078d7"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#888888"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#888888"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#888888"));
  widget->setPalette(palette);
  widget->setStyleSheet(
    "QMainWindow,QDialog{background:#f0f0f0;}"
    "QWidget{font-family:'Microsoft YaHei UI';font-size:13px;color:#202020;}"
    "QPushButton{background:#fafafa;border:1px solid #c5c5c5;border-radius:3px;padding:5px 12px;}"
    "QPushButton:hover{background:#eaf3fc;border-color:#7eb4ea;}"
    "QPushButton:pressed{background:#d8e9fa;}QPushButton:default{border-color:#0078d7;}"
    "QPushButton:disabled{color:#888;background:#f0f0f0;border-color:#ddd;}"
    "QLineEdit,QSpinBox,QComboBox,QKeySequenceEdit{background:white;border:1px solid #c8c8c8;"
    "border-radius:0;padding:3px;selection-background-color:#0078d7;selection-color:white;}"
    "QKeySequenceEdit{border:0;padding:0;background:transparent;}"
    "QKeySequenceEdit QLineEdit{border:1px solid #c8c8c8;padding:3px;}"
    "QSpinBox::up-button{width:18px;subcontrol-origin:border;subcontrol-position:top right;background:#fafafa;border-left:1px solid #ddd;}"
    "QSpinBox::down-button{width:18px;subcontrol-origin:border;subcontrol-position:bottom right;background:#fafafa;border-left:1px solid #ddd;}"
    "QSpinBox::up-arrow{image:url(:/toolbar/panel-up.png);width:9px;height:9px;}"
    "QSpinBox::down-arrow,QComboBox::down-arrow{image:url(:/toolbar/panel-down.png);width:9px;height:9px;}"
    "QComboBox::drop-down{width:20px;border-left:1px solid #ddd;}"
    "QLineEdit:focus,QSpinBox:focus,QComboBox:focus,QKeySequenceEdit:focus{border-color:#0078d7;}"
    "QLineEdit:disabled,QSpinBox:disabled,QComboBox:disabled{background:#f0f0f0;color:#888;}"
    "QListWidget{background:white;border:1px solid #d2d2d2;outline:0;}"
    "QListWidget::item{padding:6px;}QListWidget::item:selected{background:#cce8ff;color:#202020;}"
    "QTabWidget::pane{background:#f8f8f8;border:1px solid #d8d8d8;}"
    "QTabBar::tab{background:#ededed;border:1px solid #d8d8d8;padding:5px 12px;}"
    "QTabBar::tab:selected{background:#f8f8f8;border-bottom-color:#f8f8f8;}"
    "QTabBar::tab:hover{background:#e5f1fb;}"
    "QCheckBox{spacing:6px;background:transparent;}"
    "QCheckBox::indicator{width:12px;height:12px;border:1px solid #888;border-radius:2px;background:white;image:none;}"
    "QCheckBox::indicator:checked{border-color:#0067c0;background:#0067c0;image:url(:/toolbar/panel-check.png);}"
    "QCheckBox::indicator:disabled{border-color:#bbb;background:#eee;}"
    "QGroupBox{border:1px solid #d8d8d8;margin-top:8px;padding-top:8px;}"
    "QMenu{background:#f8f8f8;border:1px solid #c8c8c8;}"
    "QMenu::item:selected{background:#cce8ff;}QStatusBar{background:#f0f0f0;color:#555;}"
  );
}
