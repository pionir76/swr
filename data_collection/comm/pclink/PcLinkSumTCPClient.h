#pragma once

#include "../IDeviceClient.h"
#include "../../model/DeviceModels.h"

#include <QTcpSocket>
#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

// Samwontech PCLink+SUM protocol over TCP.
// Frame structure TBD — implementation pending.
class PcLinkSumTCPClient : public IDeviceClient {
public:
    explicit PcLinkSumTCPClient(const Model::DeviceConnection &connection);
    ~PcLinkSumTCPClient() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    bool readWords(int address, int count,
                   QVector<quint16> &out,
                   QString &error) override;

    bool readBits(int address, int count,
                  QVector<bool> &out,
                  QString &error) override;

    bool writeWords(int address,
                    const QVector<quint16> &values,
                    QString &error) override;

    bool writeBits(int address,
                   const QVector<bool> &values,
                   QString &error) override;

    QString errorString() const override;

private:
    Model::DeviceConnection m_connection;
    QTcpSocket m_socket;
    QString m_lastError;
};

} // namespace Comm
} // namespace DataCollection
