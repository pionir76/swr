#pragma once

#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

class IDeviceClient {
public:
    virtual ~IDeviceClient() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual bool readWords(int address, int count, QVector<quint16> &out, QString &error) = 0;
    virtual bool readBits(int address, int count, QVector<bool> &out, QString &error) = 0;
    virtual bool writeWords(int address, const QVector<quint16> &values, QString &error) = 0;
    virtual bool writeBits(int address, const QVector<bool> &values, QString &error) = 0;

    virtual QString errorString() const = 0;
};

} // namespace Comm
} // namespace DataCollection
