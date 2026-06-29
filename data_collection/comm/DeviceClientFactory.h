#pragma once

#include <memory>

#include "IDeviceClient.h"
#include "SerialBus.h"
#include "../model/DeviceModels.h"

namespace DataCollection {
namespace Comm {

std::unique_ptr<IDeviceClient> createDeviceClient(
    const Model::DeviceConnection &connection,
    SerialBus *bus = nullptr);

} // namespace Comm
} // namespace DataCollection
