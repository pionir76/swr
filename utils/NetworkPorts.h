#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Util {

//---------------------------------------------------------------------------//
// The board's two Ethernet ports, read from the OS.
//
//   field port   = upper RJ45 = FEC1 (2188000.ethernet), 192.168.0.150/24
//   service port = lower RJ45 = FEC2 (20b4000.ethernet), 192.168.1.150/24
//
// The OS image (meta-swr swr-network-config) fixes the addresses by port
// position. Linux names follow probe order and are the reverse of the
// physical order (field = eth1, service = eth0), so ports are found by
// their platform device, never by name. The app only reports them.
//---------------------------------------------------------------------------//
struct NetworkPort {
    QString     role;               // "field" | "service"
    QString     iface;              // Linux name, empty when the port is not found
    bool        present      = false;
    bool        linkUp       = false;  // cable connected (carrier)
    int         speedMbps    = 0;      // 0 when unknown or link down
    QString     duplex;                // "full" | "half" | ""
    QString     macAddress;
    QString     ipAddress;             // first IPv4 address, empty if none
    int         prefixLength = -1;
    QString     netmask;
    QString     gateway;               // default route via this port, empty if none
    QStringList dns;                   // per-link DNS from systemd-networkd
    qint64      rxBytes      = 0;
    qint64      txBytes      = 0;
};

// Always returns two entries: field first, then service.
QList<NetworkPort> readNetworkPorts();

} // namespace Util
