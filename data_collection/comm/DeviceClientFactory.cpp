#include "DeviceClientFactory.h"

#include "modbus/ModbusRTUClient.h"
#include "modbus/ModbusTCPClient.h"

namespace DataCollection {
namespace Comm {

std::unique_ptr<IDeviceClient> createDeviceClient(const Model::DeviceConnection &connection)
{
    switch (connection.protocol) {

    case Model::DeviceConnection::Protocol::ModbusRtu:
        return std::make_unique<ModbusRTUClient>(connection);

    //case Model::DeviceConnection::Protocol::ModbusAscii:
    //    return std::make_unique<ModbusASCIIClient>(connection);

    case Model::DeviceConnection::Protocol::ModbusTcp:
        return std::make_unique<ModbusTCPClient>(connection);

    //case Model::DeviceConnection::Protocol::PcLink:
    //    return std::make_unique<PcLinkClient>(connection);

    //case Model::DeviceConnection::Protocol::PcLinkSum:
    //    return std::make_unique<PcLinkSumTCPClient>(connection);

    default:
        return nullptr;
    }

    return nullptr;
}

} // namespace Comm
} // namespace DataCollection
