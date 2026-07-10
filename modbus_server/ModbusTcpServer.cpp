#include "ModbusTcpServer.h"

#include <QModbusTcpServer>
#include <QModbusDataUnit>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QSet>

#include "../data_collection/store/RegisterTable.h"
#include "../data_collection/store/DeviceList.h"
#include "../data_collection/model/DeviceModels.h"
#include "../data_collection/model/UnifiedRegister.h"

namespace ModbusServer {

// ---------------------------------------------------------------------------
// Internal subclass of QModbusTcpServer to override readData() for unifiedAddress mapping.
// Implements QModbusTcpConnectionObserver to track connected clients.
// ---------------------------------------------------------------------------
class ModbusServerImpl : public QModbusTcpServer,
                         public QModbusTcpConnectionObserver
{
    Q_OBJECT
public:
    explicit ModbusServerImpl(QObject *parent = nullptr)
        : QModbusTcpServer(parent) {}

    std::function<quint16(int)> readFn;  // addr(=unifiedAddress) → raw quint16

    bool readData(QModbusDataUnit *unit) const override
    {
        if (!unit || !readFn) return false;
        for (int i = 0; i < static_cast<int>(unit->valueCount()); ++i)
            unit->setValue(i, readFn(unit->startAddress() + i));
        return true;
    }

    bool acceptNewConnection(QTcpSocket *newClient) override
    {
        m_clients.insert(newClient);
        emit clientConnected(newClient->peerAddress().toString());
        return true;
    }

    void removeClient(QTcpSocket *socket)
    {
        m_clients.remove(socket);
    }

    int connectionCount() const { return m_clients.size(); }

    QStringList connectedIps() const
    {
        QStringList ips;
        for (const QTcpSocket *s : m_clients)
            ips.append(s->peerAddress().toString());
        return ips;
    }

signals:
    void clientConnected(const QString &ip);

private:
    QSet<QTcpSocket *> m_clients;
};

// ---------------------------------------------------------------------------
// ModbusTcpServer
// ---------------------------------------------------------------------------
ModbusTcpServer::ModbusTcpServer(
    std::shared_ptr<DataCollection::Store::RegisterTable> registerTable,
    std::shared_ptr<DataCollection::Store::DeviceList>    deviceList,
    QObject *parent)
    : QObject(parent)
    , m_registerTable(std::move(registerTable))
    , m_deviceList(std::move(deviceList))
{
}

ModbusTcpServer::~ModbusTcpServer()
{
    stop();
}

bool ModbusTcpServer::start(quint16 port, int slaveId, QString &error)
{
    if (m_server) {
        error = QStringLiteral("Modbus server is already running");
        return false;
    }

    //---------------------------------------------------------------------------//
    // Calculate the maximum unifiedAddress based on DeviceList
    // Determine HoldingRegister block range
    //---------------------------------------------------------------------------//
    int maxId = 100;
    for (const auto &device : m_deviceList->getAll())
        for (const auto &reg : device.registers)
            if (reg.unifiedAddress > maxId)
                maxId = reg.unifiedAddress;

    auto *server = new ModbusServerImpl(this);

    //---------------------------------------------------------------------------//
    // addr(=unifiedAddress) → quint16 raw value
    //   Coil/DiscreteInput : rawCoils[0]    → false=0x0000 / true=0x0001
    //   HoldingRegister/InputRegister : rawRegisters[0]
    //---------------------------------------------------------------------------//
    server->readFn = [this](int addr) -> quint16 {
        using RT = DataCollection::Model::RegisterType;
        const auto state = m_registerTable->state(addr);
        if (state.config.unifiedAddress < 0)
            return 0;
        if (state.config.type == RT::Coil || state.config.type == RT::DiscreteInput)
            return (!state.rawCoils.isEmpty() && state.rawCoils.first()) ? 0x0001 : 0x0000;
        return state.rawRegisters.isEmpty() ? 0 : state.rawRegisters.first();
    };

    //---------------------------------------------------------------------------//
    // HoldingRegister Data block (If asking out of bounds qt answer as 0x02)
    //---------------------------------------------------------------------------//
    QModbusDataUnitMap regMap;
    regMap.insert(QModbusDataUnit::HoldingRegisters,
                  { QModbusDataUnit::HoldingRegisters, 0, static_cast<quint16>(maxId + 1) });
    server->setMap(regMap);

    server->setServerAddress(slaveId);
    server->setConnectionParameter(QModbusDevice::NetworkPortParameter,    port);
    server->setConnectionParameter(QModbusDevice::NetworkAddressParameter, QStringLiteral("0.0.0.0"));

    connect(server, &QModbusServer::dataWritten,
            this,   &ModbusTcpServer::onDataWritten);

    connect(server, &QModbusDevice::errorOccurred,
            this, [this, server](QModbusDevice::Error) {
                emit serverError(server->errorString());
            });

    //---------------------------------------------------------------------------//
    // Track connected clients via QModbusTcpConnectionObserver.
    // acceptNewConnection() fires on connect, modbusClientDisconnected on disconnect.
    //---------------------------------------------------------------------------//
    connect(server, &ModbusServerImpl::clientConnected,
            this, [this](const QString &ip) {
                emit clientConnected(ip);
                emit connectionCountChanged(connectionCount());
            });

    connect(server, &QModbusTcpServer::modbusClientDisconnected,
            this, [this, server](QTcpSocket *socket) {
                const QString ip = socket->peerAddress().toString();
                server->removeClient(socket);
                emit clientDisconnected(ip);
                emit connectionCountChanged(connectionCount());
            });

    server->installConnectionObserver(server);

    if (!server->connectDevice()) {
        error = server->errorString();
        delete server;
        return false;
    }

    m_server  = server;
    m_port    = port;
    m_slaveId = slaveId;
    return true;
}

void ModbusTcpServer::stop()
{
    if (!m_server) return;
    m_server->disconnectDevice();
    delete m_server;
    m_server = nullptr;
}

bool ModbusTcpServer::isRunning() const
{
    return m_server && m_server->state() == QModbusDevice::ConnectedState;
}

quint16 ModbusTcpServer::port()    const { return m_port; }
int     ModbusTcpServer::slaveId() const { return m_slaveId; }

int ModbusTcpServer::connectionCount() const
{
    if (!m_server) return 0;
    return static_cast<ModbusServerImpl *>(m_server)->connectionCount();
}

QStringList ModbusTcpServer::connectedClients() const
{
    if (!m_server) return {};
    return static_cast<ModbusServerImpl *>(m_server)->connectedIps();
}

void ModbusTcpServer::onDataWritten(QModbusDataUnit::RegisterType table, int address, int size)
{
    Q_UNUSED(table)

    //-----------------------------------------------------------//
    // readOnly already blocked by setData() override where readOnly is 
    // already blocked, so only propagation is performed
    //-----------------------------------------------------------//
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, static_cast<quint16>(size));
    if (!m_server->data(&unit)) return;

    for (int i = 0; i < size; ++i) {
        const int unifiedId = address + i;
        bool found = false;
        const auto config = m_deviceList->findByUnifiedAddress(unifiedId, found);
        if (!found || config.readOnly) continue;

        DataCollection::Model::WriteRequest req;
        req.config    = config;
        req.rawValues = { unit.value(i) };

        m_deviceList->enqueueWrite(config.deviceId, req);
        emit writeRequested(unifiedId, static_cast<double>(unit.value(i)));
    }
}

} // namespace ModbusServer

#include "ModbusTcpServer.moc"
