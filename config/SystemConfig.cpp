#include "SystemConfig.h"

#include <QtGlobal>

namespace SystemConfig {
namespace {
AppConfig s_config;
bool      s_initialized = false;
}

void init(const AppConfig &cfg)
{
    s_config      = cfg;
    s_initialized = true;
}

const AppConfig &config()
{
    Q_ASSERT_X(s_initialized, "SystemConfig::config", "init() must be called before config()");
    return s_config;
}

const Rs485Config &rs485()
{
    Q_ASSERT_X(s_initialized, "SystemConfig::rs485", "init() must be called before rs485()");
    return s_config.rs485;
}

const QList<NetInterfaceConfig> &networkInterfaces()
{
    Q_ASSERT_X(s_initialized, "SystemConfig::networkInterfaces", "init() must be called before networkInterfaces()");
    return s_config.networkInterfaces;
}

const SysSettings &system()
{
    Q_ASSERT_X(s_initialized, "SystemConfig::system", "init() must be called before system()");
    return s_config.system;
}

} // namespace SystemConfig
