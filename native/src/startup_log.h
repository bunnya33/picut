#pragma once
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMutex>
#include <QtCore/QSysInfo>
#include <QtCore/QDebug>

inline QString &startupLogPath() { static QString path; return path; }
inline void installStartupLog() {
  auto root=qEnvironmentVariable("FLOATSHOT_DATA_DIR");
  if (root.isEmpty()) root=qEnvironmentVariable("LOCALAPPDATA")+"/FloatShot/FloatShotNative-ImageTextTrial";
  if (!QDir().mkpath(root+"/logs")) root=QDir::tempPath()+"/picut";
  QDir().mkpath(root+"/logs"); startupLogPath()=root+"/logs/startup.log";
  if (QFileInfo(startupLogPath()).size()>1024*1024) {
    QFile::remove(startupLogPath()+".previous"); QFile::rename(startupLogPath(),startupLogPath()+".previous");
  }
  qInstallMessageHandler([](QtMsgType type,const QMessageLogContext &,const QString &message) {
    static QMutex mutex; QMutexLocker lock(&mutex);
    QFile file(startupLogPath());
    if (file.open(QIODevice::WriteOnly|QIODevice::Append)) {
      file.write((QDateTime::currentDateTime().toString(Qt::ISODate)+" ["+QString::number(type)+"] "+message+"\n").toUtf8());
    }
  });
  qInfo().noquote()<<"picut 0.5.16 startup; Windows"<<QSysInfo::kernelVersion()<<QSysInfo::currentCpuArchitecture();
}
