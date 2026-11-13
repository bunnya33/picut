#include "recording_settings.h"
#include "panel_theme.h"
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

QString recordingSummary(const QJsonObject &config) {
  const int quality=config["quality"].toInt(22);
  return QString("%1 fps · %2 · 系统声%3 · 麦克风%4 · 鼠标%5 · 点击反馈%6 · 倒计时 %7 秒")
      .arg(config["fps"].toInt(30)).arg(quality<=18?"更清晰":quality>=28?"小文件":"清晰")
      .arg(config["system_audio"].toBool()?"开":"关")
      .arg(config["microphone"].toBool()?"开":"关")
      .arg(config["cursor"].toBool(true)?"开":"关")
      .arg(config["click_feedback"].toBool(false)?"开":"关")
      .arg(qBound(0,config["countdown"].toInt(3),10));
}
RecordOptions savedRecordingOptions(const QJsonObject &config, QRect region,const QString &output) {
  RecordOptions options; options.region=region; options.output=output;
  options.systemAudio=config["system_audio"].toBool(); options.microphone=config["microphone"].toBool();
  options.speakerId=config["speaker_id"].toString(); options.micId=config["mic_id"].toString();
  options.cursor=config["cursor"].toBool(true); options.fps=config["fps"].toInt(30)==15?15:30;
  options.clickFeedback=config["click_feedback"].toBool(false);
  options.quality=qBound(18,config["quality"].toInt(22),28); return options;
}
RecordingSettingsDialog::RecordingSettingsDialog(Store *store,QRect region,bool start,QWidget *parent)
    : QDialog(parent) {
  setObjectName("recordingSettings"); setWindowTitle("picut · 录屏设置");
  applyPanelTheme(this);
  auto root=new QVBoxLayout(this); auto form=new QFormLayout; root->addLayout(form);
  form->addRow(new QLabel(QString("录制区域 %1 × %2").arg(region.width()).arg(region.height())));
  auto system=new QCheckBox("录入系统声音"); system->setObjectName("recordSystemAudio");
  auto mic=new QCheckBox("录入麦克风"); mic->setObjectName("recordMicrophone");
  system->setChecked(store->config["system_audio"].toBool()); mic->setChecked(store->config["microphone"].toBool());
  auto speakers=new QComboBox; speakers->setObjectName("recordSpeaker");
  auto microphones=new QComboBox; microphones->setObjectName("recordMicDevice");
  speakers->addItem("默认输出设备",QString()); microphones->addItem("默认麦克风",QString());
  try {
    for (auto device:audioDevices(false)) speakers->addItem(device.name,device.id);
    for (auto device:audioDevices(true)) microphones->addItem(device.name,device.id);
  } catch (const std::exception &e) { root->addWidget(new QLabel(QString::fromUtf8(e.what()))); }
  for (auto pair:{qMakePair(speakers,QString("speaker_id")),qMakePair(microphones,QString("mic_id"))}) {
    auto id=store->config[pair.second].toString(); int index=pair.first->findData(id);
    if (index<0 && !id.isEmpty()) { pair.first->addItem("已保存设备（当前不可用）",id); index=pair.first->count()-1; }
    pair.first->setCurrentIndex(qMax(0,index));
  }
  form->addRow(system); form->addRow("声音输出设备",speakers);
  form->addRow(mic); form->addRow("麦克风设备",microphones);
  speakers->setEnabled(system->isChecked()); microphones->setEnabled(mic->isChecked());
  connect(system,&QCheckBox::toggled,speakers,&QWidget::setEnabled);
  connect(mic,&QCheckBox::toggled,microphones,&QWidget::setEnabled);
  auto cursor=new QCheckBox("显示鼠标指针"); cursor->setObjectName("recordCursor");
  cursor->setChecked(store->config["cursor"].toBool(true)); form->addRow(cursor);
  auto clicks=new QCheckBox("鼠标点击反馈"); clicks->setObjectName("recordClickFeedback");
  clicks->setToolTip("在录制视频中显示短暂的点击圆点动画，支持鼠标左键、右键和中键。");
  clicks->setChecked(store->config["click_feedback"].toBool(false)); form->addRow(clicks);
  auto fps=new QComboBox; fps->setObjectName("recordFps"); fps->addItem("30 fps",30); fps->addItem("15 fps",15);
  fps->setCurrentIndex(store->config["fps"].toInt(30)==15?1:0); form->addRow("帧率",fps);
  auto quality=new QComboBox; quality->setObjectName("recordQuality");
  quality->addItem("清晰 · 推荐",22); quality->addItem("更清晰",18); quality->addItem("更小文件",28);
  quality->setCurrentIndex(qMax(0,quality->findData(store->config["quality"].toInt(22)))); form->addRow("画质",quality);
  auto countdown=new QSpinBox; countdown->setObjectName("recordCountdown"); countdown->setRange(0,10);
  countdown->setValue(store->config["countdown"].toInt(3)); form->addRow("倒计时",countdown);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Ok)->setText(start?"保存并开始录制":"保存设置");
  buttons->button(QDialogButtonBox::Cancel)->setText("取消"); root->addWidget(buttons);
  connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,this,[=,this] {
    auto &config=store->config;
    config["system_audio"]=system->isChecked(); config["microphone"]=mic->isChecked();
    config["speaker_id"]=speakers->currentData().toString(); config["mic_id"]=microphones->currentData().toString();
    config["cursor"]=cursor->isChecked(); config["fps"]=fps->currentData().toInt();
    config["click_feedback"]=clicks->isChecked();
    config["quality"]=quality->currentData().toInt(); config["countdown"]=countdown->value();
    store->saveConfig(); accept();
  });
}
