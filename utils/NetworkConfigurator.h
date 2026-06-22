#pragma once

#include <QString>

namespace Util {

struct NetworkConfig {
    QString ipAddress;
    QString netmask;
    QString gateway;
    QString interfaceName = "eth0";
};

bool applyNetworkConfig(const NetworkConfig &config, QString &error);

} // namespace Util
