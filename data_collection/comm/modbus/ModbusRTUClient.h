#pragma once

#include "../IDeviceClient.h"
#include "../SerialBus.h"
#include "../../model/DeviceModels.h"

namespace DataCollection {
namespace Comm {

class ModbusRTUClient : public IDeviceClient {
public:
    explicit ModbusRTUClient(const Model::DeviceConnection &connection, SerialBus &bus);
    ~ModbusRTUClient() override = default;

    bool connect() override    { return m_bus.isOpen(); }
    void disconnect() override { }
    bool isConnected() const override { return m_bus.isOpen(); }

    bool readWords(int address, int count,
                   QVector<quint16> &out, QString &error) override;

    bool readBits(int address, int count,
                  QVector<bool> &out, QString &error) override;

    bool writeWords(int address,
                    const QVector<quint16> &values, QString &error) override;

    bool writeBits(int address,
                   const QVector<bool> &values, QString &error) override;

    QString errorString() const override;

private:
    QByteArray buildReadRequest(quint8 functionCode, int startAddress, int quantity) const;
    QByteArray buildWriteSingleCoilRequest(int address, bool value) const;
    QByteArray buildWriteSingleRegisterRequest(int address, quint16 value) const;
    QByteArray buildWriteMultipleCoilsRequest(int startAddress, const QVector<bool> &values) const;
    QByteArray buildWriteMultipleRegistersRequest(int startAddress, const QVector<quint16> &values) const;
    static quint16 crc16(const QByteArray &data);
    static QByteArray packCoils(const QVector<bool> &values);

    bool sendRequest(const QByteArray &request, QByteArray &response, QString &error);
    bool verifyResponse(const QByteArray &response, quint8 expectedFunction,
                        int expectedDataBytes, QString &error) const;

    Model::DeviceConnection m_connection;
    SerialBus &m_bus;
    QString    m_lastError;
};

} // namespace Comm
} // namespace DataCollection
