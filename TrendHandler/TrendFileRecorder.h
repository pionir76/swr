#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QList>

#include "TrendFileWriter.h"
#include "../config/AppConfig.h"
#include "../data_collection/store/RegisterTable.h"

namespace TrendHandler {

class TrendFileRecorder : public QObject
{
    Q_OBJECT

public:
    struct Status {
        bool    recording   = false;
        QString filename;
        qint64  elapsedSec  = 0;
        qint64  fileSize    = 0;
        quint32 recordCount = 0;
        int     freq        = 0;
    };

    explicit TrendFileRecorder(DataCollection::Store::RegisterTable *regTable,
                               QObject *parent = nullptr);

    bool   start(const TrendConfig &config, QString &error);
    void   stop();
    bool   isRecording() const { return m_recording; }
    Status status() const;

private slots:
    void onTimer();

private:
    void buildWriterConfig(const TrendConfig &config, TndWriterConfig &out) const;
    static quint8 scaleToDotpos(double scale);

    DataCollection::Store::RegisterTable *m_regTable;
    TrendFileWriter m_writer;
    QTimer          m_timer;

    bool      m_recording    = false;
    QString   m_filename;
    QDateTime m_startTime;
    int       m_freq         = 10;
    QList<int> m_channelIds;
    quint32   m_lastRecordCount = 0;
    qint64    m_lastFileSize    = 0;
};

} // namespace TrendHandler
