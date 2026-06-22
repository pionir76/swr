#pragma once

#include "../IDeviceClient.h"
#include "../../model/DeviceModels.h"

#include <QSerialPort>
#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

// Samwontech PCLink ASCII protocol client.
// Supports word-register read/write only; bit operations are not available.
// Fill in buildReadFrame() / buildWriteFrame() / parseReadResponse()
// according to the official Samwontech PCLink specification.
class PcLinkClient : public IDeviceClient {
public:
    explicit PcLinkClient(const Model::DeviceConnection &connection);
    ~PcLinkClient() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    bool readWords(int address, int count,
                   QVector<quint16> &out,
                   QString &error) override;

    bool writeWords(int address,
                    const QVector<quint16> &values,
                    QString &error) override;

    // Not supported by PCLink — always return false
    bool readBits(int address,
                  int count,
                  QVector<bool> &out,
                  QString &error) override;

    bool writeBits(int address,
                   const QVector<bool> &values,
                   QString &error) override;

    QString errorString() const override;

private:
    QByteArray buildReadFrame(int address, int count) const;
    QByteArray buildWriteFrame(int address, const QVector<quint16> &values) const;
    bool parseReadResponse(const QByteArray &response, int expectedCount,
                           QVector<quint16> &out, QString &error) const;
    bool parseWriteResponse(const QByteArray &response, QString &error) const;
    bool sendRequest(const QByteArray &request, QByteArray &response, QString &error);
    static QByteArray checksum(const QByteArray &data);

    Model::DeviceConnection m_connection;
    QSerialPort m_serialPort;
    QString m_lastError;
};

} // namespace Comm
} // namespace DataCollection
