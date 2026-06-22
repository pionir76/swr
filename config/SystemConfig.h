#pragma once

#include "AppConfig.h"

namespace SystemConfig {

//-----------------------------------------------------//
// We have to Call This At First time from main()
//-----------------------------------------------------//
void init(const AppConfig &cfg);

// Section Loader.
const AppConfig             &config();
const Rs485Config           &rs485();
const QList<NetInterfaceConfig> &networkInterfaces();
const SysSettings           &system();

} // namespace SystemConfig
