#pragma once

#include "../IDeviceClient.h"
#include "../../model/DeviceModels.h"

#include <QTcpSocket>

namespace DataCollection {
namespace Comm {

class ModbusTCPClient : public IDeviceClient {

public:
    explicit ModbusTCPClient(const Model::DeviceConnection &connection);
    ~ModbusTCPClient() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    bool readWords(int address,
                   int count,
                   QVector<quint16> &out,
                   QString &error) override;

    bool readBits(int address,
                  int count,
                  QVector<bool> &out,
                  QString &error) override;

    // writeWords: single value uses FC06, multiple uses FC10
    bool writeWords(int address,
                    const QVector<quint16> &values,
                    QString &error) override;

    // writeBits: single value uses FC05, multiple uses FC0F
    bool writeBits(int address,
                   const QVector<bool> &values,
                   QString &error) override;

    QString errorString() const override;

private:
    QByteArray buildTcpRequest(quint8 functionCode,
                               int startAddress,
                               int quantityOrValue,
                               const QVector<bool> *coils,
                               const QVector<quint16> *registers);

    static QByteArray packCoils(const QVector<bool> &values);
    bool sendRequest(const QByteArray &request, QByteArray &response, QString &error);
    bool verifyResponse(const QByteArray &response,
                        quint8 expectedFunction,
                        int expectedDataBytes,
                        QString &error) const;


    Model::DeviceConnection m_connection;
    QTcpSocket m_socket;
    quint16 m_transactionId = 1;
    QString m_lastError;
};

} // namespace Comm
} // namespace DataCollection
