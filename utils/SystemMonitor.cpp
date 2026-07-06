#include "SystemMonitor.h"

#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <sys/statvfs.h>

namespace Util {

SystemMonitor::SystemMonitor(const QStringList &diskMounts,
                             const QStringList &netIfaces,
                             int intervalSeconds,
                             QObject *parent)
    : QObject(parent)
    , m_diskMounts(diskMounts)
    , m_netIfaces(netIfaces)
{
    m_prevCpu = readCpuStat();

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SystemMonitor::sample);
    timer->start(intervalSeconds * 1000);

    sample();
}

SystemResources SystemMonitor::resources() const
{
    QMutexLocker lock(&m_mutex);
    return m_cache;
}

// ---------------------------------------------------------------------------
// private slot
// ---------------------------------------------------------------------------

void SystemMonitor::sample()
{
    SystemResources res;
    res.cachedAt = QDateTime::currentDateTime();

    // CPU
    const CpuStat cur       = readCpuStat();
    const qint64 totalDelta = cur.total() - m_prevCpu.total();
    const qint64 busyDelta  = cur.busy()  - m_prevCpu.busy();
    res.cpuUsagePercent = (totalDelta > 0)
        ? qBound(0.0, 100.0 * busyDelta / totalDelta, 100.0)
        : 0.0;
    m_prevCpu = cur;

    // Load average
    {
        QFile f(QStringLiteral("/proc/loadavg"));
        if (f.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> parts =
                f.readAll().trimmed().split(' ');
            if (parts.size() >= 3) {
                res.loadAvg1  = parts[0].toDouble();
                res.loadAvg5  = parts[1].toDouble();
                res.loadAvg15 = parts[2].toDouble();
            }
        }
    }

    res.cpuTempCelsius = readTemperature();
    res.uptimeSeconds  = readUptime();

    readMemory(res);
    readDisks(res);
    readNetwork(res);

    QMutexLocker lock(&m_mutex);
    m_cache = res;
}

// ---------------------------------------------------------------------------
// private static
// ---------------------------------------------------------------------------

CpuStat SystemMonitor::readCpuStat()
{
    CpuStat stat;
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly))
        return stat;

    QTextStream stream(f.readLine());
    QString label;
    stream >> label
           >> stat.user >> stat.nice >> stat.system >> stat.idle
           >> stat.iowait >> stat.irq >> stat.softirq >> stat.steal;
    return stat;
}

double SystemMonitor::readTemperature()
{
    QFile f(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (!f.open(QIODevice::ReadOnly))
        return 0.0;
    return f.readAll().trimmed().toDouble() / 1000.0;
}

qint64 SystemMonitor::readUptime()
{
    QFile f(QStringLiteral("/proc/uptime"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    return static_cast<qint64>(
        f.readAll().trimmed().split(' ').first().toDouble());
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void SystemMonitor::readMemory(SystemResources &res) const
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return;

    qint64 memTotal = 0, memAvailable = 0, swapTotal = 0, swapFree = 0;

    QTextStream stream(&f);
    QString line;
    while (stream.readLineInto(&line)) {
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 2) continue;

        const QString &key = parts[0];
        const qint64   val = parts[1].toLongLong();

        if      (key == QLatin1String("MemTotal:"))     memTotal     = val;
        else if (key == QLatin1String("MemAvailable:")) memAvailable = val;
        else if (key == QLatin1String("SwapTotal:"))    swapTotal    = val;
        else if (key == QLatin1String("SwapFree:"))     swapFree     = val;
    }

    res.memTotalKb      = memTotal;
    res.memUsedKb       = memTotal - memAvailable;
    res.memUsagePercent = memTotal > 0
        ? qBound(0.0, 100.0 * res.memUsedKb / memTotal, 100.0) : 0.0;

    res.swapTotalKb      = swapTotal;
    res.swapUsedKb       = swapTotal - swapFree;
    res.swapUsagePercent = swapTotal > 0
        ? qBound(0.0, 100.0 * res.swapUsedKb / swapTotal, 100.0) : 0.0;
}

void SystemMonitor::readDisks(SystemResources &res) const
{
    for (const QString &mount : m_diskMounts) {
        struct statvfs st;
        if (::statvfs(mount.toLocal8Bit().constData(), &st) != 0)
            continue;

        const qint64 totalBytes = static_cast<qint64>(st.f_blocks) * st.f_frsize;
        const qint64 availBytes = static_cast<qint64>(st.f_bavail) * st.f_frsize;
        const qint64 usedBytes  = totalBytes - availBytes;

        DiskStat disk;
        disk.mount        = mount;
        disk.totalMb      = totalBytes / (1024 * 1024);
        disk.usedMb       = usedBytes  / (1024 * 1024);
        disk.usagePercent = totalBytes > 0
            ? qBound(0.0, 100.0 * usedBytes / totalBytes, 100.0) : 0.0;
        res.disks.append(disk);
    }
}

void SystemMonitor::readNetwork(SystemResources &res) const
{
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly))
        return;

    f.readLine(); // header 1
    f.readLine(); // header 2

    while (!f.atEnd()) {
        const QString line = QString::fromLatin1(f.readLine()).trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;

        const QString iface = line.left(colon).trimmed();
        if (!m_netIfaces.contains(iface))
            continue;

        const QStringList parts = line.mid(colon + 1).trimmed()
            .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 9)
            continue;

        NetStat net;
        net.iface   = iface;
        
        //---------------------------------------------------------//
        // TODO: Currently reports cumulative bytes since boot, 
        // not real-time bandwidth.To compute bytes/sec, store 
        // previous sample (m_prevNet) and calculate delta per interval, 
        // similar to how CPU usage is computed via m_prevCpu.
        //---------------------------------------------------------//
        net.rxBytes = parts[0].toLongLong();
        net.txBytes = parts[8].toLongLong();
        res.network.append(net);
    }
}

} // namespace Util
