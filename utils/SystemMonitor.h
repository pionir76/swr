#pragma once

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace Util {

struct CpuStat {
    qint64 user    = 0;
    qint64 nice    = 0;
    qint64 system  = 0;
    qint64 idle    = 0;
    qint64 iowait  = 0;
    qint64 irq     = 0;
    qint64 softirq = 0;
    qint64 steal   = 0;

    qint64 total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
    qint64 busy()  const { return user + nice + system + irq + softirq + steal; }
};

struct DiskStat {
    QString mount;
    qint64  totalMb      = 0;
    qint64  usedMb       = 0;
    double  usagePercent = 0.0;
};

struct NetStat {
    QString iface;
    qint64  rxBytes = 0;
    qint64  txBytes = 0;
};

struct SystemResources {
    double  cpuUsagePercent  = 0.0;
    double  loadAvg1         = 0.0;
    double  loadAvg5         = 0.0;
    double  loadAvg15        = 0.0;
    double  cpuTempCelsius   = 0.0;

    qint64  memTotalKb       = 0;
    qint64  memUsedKb        = 0;
    double  memUsagePercent  = 0.0;
    qint64  swapTotalKb      = 0;
    qint64  swapUsedKb       = 0;
    double  swapUsagePercent = 0.0;

    QList<DiskStat> disks;
    QList<NetStat>  network;

    qint64    uptimeSeconds = 0;
    QDateTime cachedAt;
};

class SystemMonitor : public QObject
{
    Q_OBJECT
public:
    explicit SystemMonitor(const QStringList &diskMounts,
                           const QStringList &netIfaces,
                           int intervalSeconds = 3,
                           QObject *parent = nullptr);

    SystemResources resources() const;

private slots:
    void sample();

private:
    static CpuStat readCpuStat();
    static double  readTemperature();
    static qint64  readUptime();
    void           readMemory(SystemResources &res) const;
    void           readDisks(SystemResources &res) const;
    void           readNetwork(SystemResources &res) const;

    QStringList     m_diskMounts;
    QStringList     m_netIfaces;
    CpuStat         m_prevCpu;
    SystemResources m_cache;
    mutable QMutex  m_mutex;
};

} // namespace Util
