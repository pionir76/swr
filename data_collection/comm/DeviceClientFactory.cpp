#include "DeviceClientFactory.h"

#include "modbus/ModbusRTUClient.h"
#include "modbus/ModbusASCIIClient.h"
#include "modbus/ModbusTCPClient.h"
#include "pclink/PcLinkClient.h"
#include "pclink/PcLinkSumTCPClient.h"

namespace DataCollection {
namespace Comm {

std::unique_ptr<IDeviceClient> createDeviceClient(
    const Model::DeviceConnection &connection,
    SerialBus *bus)
{
    switch (connection.protocol) {

    case Model::DeviceConnection::Protocol::ModbusRtu:
        return std::make_unique<ModbusRTUClient>(connection, *bus);

    case Model::DeviceConnection::Protocol::ModbusAscii:
        return std::make_unique<ModbusASCIIClient>(connection, *bus);

    case Model::DeviceConnection::Protocol::ModbusTcp:
        return std::make_unique<ModbusTCPClient>(connection);

    case Model::DeviceConnection::Protocol::PcLink:
        return std::make_unique<PcLinkClient>(connection, *bus);

    case Model::DeviceConnection::Protocol::PcLinkSum:
        return std::make_unique<PcLinkSumTCPClient>(connection);

    default:
        return nullptr;
    }
}

} // namespace Comm
} // namespace DataCollection
