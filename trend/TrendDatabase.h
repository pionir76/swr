#pragma once

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QtGlobal>

namespace Trend {

struct TrendPoint {
    qint64  ts  = 0;
    quint16 avg = 0;   // raw quint16; frontend applies scale for display
    quint16 min = 0;
    quint16 max = 0;
};

struct TrendRawPoint {
    int     regId = 0;
    qint64  ts    = 0;
    quint16 value = 0; // raw register value (rawRegisters[0])
};

class TrendDatabase
{
public:
    TrendDatabase();
    ~TrendDatabase();

    bool open(const QString &dbPath, QString &error);
    void close();
    bool isOpen() const;

    // Data write — triggers 5m/10m rollup automatically
    bool insertBatch(const QList<TrendRawPoint> &points, QString &error);

    // Data read
    // registers: empty = all channels recorded in [from, to]
    // resolution: "raw" | "5m" | "10m"
    // Each channel is downsampled to ≤1,000 points server-side
    QMap<int, QList<TrendPoint>> query(qint64 from, qint64 to,
                                       const QString &resolution,
                                       const QList<int> &registers,
                                       QString &error) const;

    // Delete rows older than retention limits; call from a 1-hour QTimer
    void purgeExpired();

    // Delete all trend data (called when channel config changes)
    bool deleteAll(QString &error);

    // Returns {minTs, maxTs} of trend_data; both 0 if no data exists
    QPair<qint64, qint64> dataRange() const;

private:
    bool initSchema(QString &error);
    void rollup5m(int regId, qint64 bucketTs);   // bucket = floor(ts/300)*300
    void rollup10m(int regId, qint64 bucketTs);  // bucket = floor(ts/600)*600

    QString m_connectionName;
};

} // namespace Trend
