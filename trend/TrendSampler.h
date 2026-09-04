#pragma once

#include <QObject>
#include <QTimer>
#include <QList>

#include "TrendDatabase.h"
#include "../config/AppConfig.h"
#include "../data_collection/store/RegisterTable.h"

namespace Trend {

class TrendSampler : public QObject
{
    Q_OBJECT

public:
    explicit TrendSampler(DataCollection::Store::RegisterTable *regTable,
                          TrendDatabase *trendDb,
                          QObject *parent = nullptr);

    void applyConfig(const TrendConfig &config);
    bool isRunning() const { return m_running; }

private:
    void start();
    void stop();

private slots:
    void onSampleTimer();
    void onFlushTimer();

private:
    void flush();

    DataCollection::Store::RegisterTable *m_regTable;
    TrendDatabase *m_trendDb;

    QTimer m_sampleTimer;
    QTimer m_flushTimer;
    QList<int> m_channelIds;
    QList<TrendRawPoint> m_buffer;
    int m_intervalSec = 10;
    bool m_running = false;

    static constexpr int kMaxBuffer = 100;
    static constexpr int kFlushSec  = 30;
};

} // namespace Trend
