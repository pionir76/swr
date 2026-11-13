#include "NetworkPorts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>

#include <arpa/inet.h>

namespace Util {

namespace {

struct PortDef {
    const char *role;
    const char *platformDevice;  // tail of the /sys/class/net/<if>/device link
};

// Same matching as meta-swr 10-swr-field.network / 20-swr-service.network.
constexpr PortDef kPorts[] = {
    { "field",   "/2188000.ethernet" },  // FEC1, upper RJ45
    { "service", "/20b4000.ethernet" },  // FEC2, lower RJ45
};

QString readSysFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(f.readAll()).trimmed();
}

QString findIface(const char *platformDevice)
{
    const QString tail = QLatin1String(platformDevice);
    const QDir netDir(QStringLiteral("/sys/class/net"));
    const QStringList names = netDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);

    for (const QString &name : names) {
        const QString device =
            QFileInfo(netDir.filePath(name) + QStringLiteral("/device")).canonicalFilePath();
        if (device.endsWith(tail))
            return name;
    }
    return {};
}

// /proc/net/route lists "Iface Destination Gateway ..." with addresses in hex.
QString defaultGateway(const QString &iface)
{
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly))
        return {};

    f.readLine();  // header
    while (!f.atEnd()) {
        const QStringList cols = QString::fromLatin1(f.readLine()).split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (cols.size() < 3 || cols.at(0).trimmed() != iface || cols.at(1) != QLatin1String("00000000"))
            continue;

        bool ok = false;
        const quint32 raw = cols.at(2).toUInt(&ok, 16);
        if (!ok || raw == 0)
            continue;

        // The kernel prints the address as a native-endian 32-bit word.
        in_addr addr{};
        addr.s_addr = raw;
        return QString::fromLatin1(inet_ntoa(addr));
    }
    return {};
}

// systemd-networkd keeps per-link state in /run/systemd/netif/links/<ifindex>.
QStringList linkDns(const QString &iface)
{
    const QString ifindex = readSysFile(QStringLiteral("/sys/class/net/%1/ifindex").arg(iface));
    if (ifindex.isEmpty())
        return {};

    QFile f(QStringLiteral("/run/systemd/netif/links/") + ifindex);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    while (!f.atEnd()) {
        const QString line = QString::fromLatin1(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("DNS=")))
            return line.mid(4).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    return {};
}

} // namespace

QList<NetworkPort> readNetworkPorts()
{
    QList<NetworkPort> ports;

    for (const PortDef &def : kPorts) {
        NetworkPort port;
        port.role  = QLatin1String(def.role);
        port.iface = findIface(def.platformDevice);

        if (!port.iface.isEmpty()) {
            const QString sys = QStringLiteral("/sys/class/net/%1/").arg(port.iface);
            port.present    = true;
            // carrier cannot be read while the interface is down; treat that as no link.
            port.linkUp     = readSysFile(sys + QStringLiteral("carrier")) == QLatin1String("1");
            port.macAddress = readSysFile(sys + QStringLiteral("address")).toUpper();
            port.rxBytes    = readSysFile(sys + QStringLiteral("statistics/rx_bytes")).toLongLong();
            port.txBytes    = readSysFile(sys + QStringLiteral("statistics/tx_bytes")).toLongLong();

            if (port.linkUp) {
                const int speed = readSysFile(sys + QStringLiteral("speed")).toInt();
                port.speedMbps  = speed > 0 ? speed : 0;
                port.duplex     = readSysFile(sys + QStringLiteral("duplex"));
                if (port.duplex == QLatin1String("unknown"))
                    port.duplex.clear();
            }

            const QNetworkInterface ni = QNetworkInterface::interfaceFromName(port.iface);
            for (const QNetworkAddressEntry &entry : ni.addressEntries()) {
                if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                    continue;
                port.ipAddress    = entry.ip().toString();
                port.prefixLength = entry.prefixLength();
                port.netmask      = entry.netmask().toString();
                break;
            }

            port.gateway = defaultGateway(port.iface);
            port.dns     = linkDns(port.iface);
        }

        ports.append(port);
    }

    return ports;
}

} // namespace Util
