#pragma once
#include "recorder.h"
#include "store.h"
#include <QtWidgets/QDialog>

QString recordingSummary(const QJsonObject &config);
RecordOptions savedRecordingOptions(const QJsonObject &config, QRect physicalRegion,
                                    const QString &output);
class RecordingSettingsDialog : public QDialog {
public:
  RecordingSettingsDialog(Store *store, QRect logicalRegion, bool startAfterSave,
                          QWidget *parent = nullptr);
};
