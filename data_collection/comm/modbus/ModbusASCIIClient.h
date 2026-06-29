#pragma once

#include "../IDeviceClient.h"
#include "../SerialBus.h"
#include "../../model/DeviceModels.h"

#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

class ModbusASCIIClient : public IDeviceClient {
public:
    explicit ModbusASCIIClient(const Model::DeviceConnection &connection, SerialBus &bus);
    ~ModbusASCIIClient() override = default;

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
    QByteArray buildAsciiFrame(const QByteArray &pdu) const;
    QByteArray buildReadRequest(quint8 functionCode, int startAddress, int quantity) const;
    QByteArray buildWriteSingleCoilRequest(int address, bool value) const;
    QByteArray buildWriteSingleRegisterRequest(int address, quint16 value) const;
    QByteArray buildWriteMultipleCoilsRequest(int startAddress, const QVector<bool> &values) const;
    QByteArray buildWriteMultipleRegistersRequest(int startAddress, const QVector<quint16> &values) const;
    static QByteArray packCoils(const QVector<bool> &values);
    static quint8 lrc(const QByteArray &data);

    bool sendRequest(const QByteArray &request, QByteArray &responsePdu, QString &error);
    bool parseAsciiFrame(const QByteArray &frame, QByteArray &responsePdu, QString &error) const;
    bool verifyResponse(const QByteArray &responsePdu, quint8 expectedFunction,
                        int expectedDataBytes, QString &error) const;

    Model::DeviceConnection m_connection;
    SerialBus &m_bus;
    QString    m_lastError;
};

} // namespace Comm
} // namespace DataCollection
