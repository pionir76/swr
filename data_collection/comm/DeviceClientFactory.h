#pragma once

#include <memory>

#include "IDeviceClient.h"
#include "../model/DeviceModels.h"

namespace DataCollection {
namespace Comm {

std::unique_ptr<IDeviceClient> createDeviceClient(const Model::DeviceConnection &connection);

} // namespace Comm
} // namespace DataCollection
