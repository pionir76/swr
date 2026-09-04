#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "TrendFileWriter.h"   // TndFixedHeader, TndBlockHeader

namespace TrendHandler {

class TrendFileReader
{
public:
    struct FileInfo {
        QString  filename;
        qint64   fileSize    = 0;
        quint32  startTime   = 0;  // unix sec
        quint32  endTime     = 0;  // unix sec
        quint32  recordCount = 0;
        quint16  freq        = 0;
        quint8   chCount     = 0;
    };

    static QList<FileInfo> listFiles(const QString &dirPath, QString &error);
    static QByteArray      readHeader(const QString &path, QString &error);
    static QByteArray      readLatest(const QString &path, QString &error);
    static QByteArray      readSample(const QString &path,
                                      quint32 from, quint32 to, quint16 step,
                                      QString &error);

private:
    static bool    parseHeader(const QString &path, FileInfo &out, QString &error);
    static quint32 stimeToUnix(const quint8 stime[6]);
};

} // namespace TrendHandler
