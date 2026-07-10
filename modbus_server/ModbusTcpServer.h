#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include <QModbusServer>

class QModbusTcpServer;

namespace DataCollection::Store { class RegisterTable; class DeviceList; }

namespace ModbusServer {

class ModbusTcpServer : public QObject
{
    Q_OBJECT

public:
    explicit ModbusTcpServer(
        std::shared_ptr<DataCollection::Store::RegisterTable> registerTable,
        std::shared_ptr<DataCollection::Store::DeviceList>    deviceList,
        QObject *parent = nullptr);

    ~ModbusTcpServer() override;

    bool start(quint16 port, int slaveId, QString &error);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    int     slaveId() const;

    int         connectionCount() const;
    QStringList connectedClients() const;

signals:
    void writeRequested(int unifiedId, double value);
    void serverError(const QString &message);
    void connectionCountChanged(int count);
    void clientConnected(const QString &ip);
    void clientDisconnected(const QString &ip);

private slots:
    void onDataWritten(QModbusDataUnit::RegisterType table, int address, int size);

private:
    std::shared_ptr<DataCollection::Store::RegisterTable> m_registerTable;
    std::shared_ptr<DataCollection::Store::DeviceList>    m_deviceList;

    QModbusTcpServer *m_server = nullptr;

    quint16 m_port    = 502;
    int     m_slaveId = 1;
};

} // namespace ModbusServer
