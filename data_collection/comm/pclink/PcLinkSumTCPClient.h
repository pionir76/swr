#pragma once

#include "../IDeviceClient.h"
#include "../../model/DeviceModels.h"

#include <QTcpSocket>
#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

// Samwontech PCLink+SUM protocol over TCP.
// Only sequential read (RSD) and sequential write (WSD) are supported.
// Bit operations are not supported by this client.
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
    QByteArray buildRsdFrame(int address, int count) const;
    QByteArray buildWsdFrame(int address, const QVector<quint16> &values) const;
    static QByteArray wrapFrame(const QByteArray &body);
    static quint8 calcBodySum(const QByteArray &body);
    static bool verifyFrameSum(const QByteArray &frame);

    bool sendRequest(const QByteArray &request, QByteArray &response, QString &error);
    bool parseRsdResponse(const QByteArray &response, int expectedCount,
                          QVector<quint16> &out, QString &error);
    bool parseWsdResponse(const QByteArray &response, QString &error);
    bool checkNgResponse(const QByteArray &response, QString &error);

    Model::DeviceConnection m_connection;
    QTcpSocket m_socket;
    mutable QString m_lastError;
};

} // namespace Comm
} // namespace DataCollection
