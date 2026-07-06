#pragma once

#include "IDeviceClient.h"
#include "../model/DeviceModels.h"

#include <memory>

namespace DataCollection {
namespace Comm {

//--------------------------------------------------------------------------------//
// Executes read/write requests for a single device using an IDeviceClient.
//
// Translates RegisterConfig read/write requests into IDeviceClient calls.
// Protocol-agnostic: works with any IDeviceClient implementation.
//--------------------------------------------------------------------------------//
class RegisterExecutor {
public:
    // Maximum registers/coils per batch read request. Not a per-field limit.
    static constexpr int MAX_READ_QUANTITY = 64;

    RegisterExecutor(std::unique_ptr<IDeviceClient> client,
                     Model::ByteOrder defaultByteOrder);

    bool connect(QString &error);
    void disconnect();
    bool isConnected() const;

    bool readField(const Model::RegisterConfig &config,
                   QVector<quint16> &registerValues,
                   QVector<bool> &coilValues,
                   QString &error);

    bool readBatch(int startAddress,
                   int totalLength,
                   Model::RegisterType type,
                   QVector<quint16> &registerValues,
                   QVector<bool>    &coilValues,
                   QString          &error);

    bool writeField(const Model::RegisterConfig &config,
                    const QVector<quint16> &registerValues,
                    const QVector<bool> &coilValues,
                    QString &error);

    QVector<quint16> applyFieldByteOrder(const QVector<quint16> &values,
                                         const Model::RegisterConfig &config) const;

private:
    QVector<quint16> applyByteOrder(const QVector<quint16> &values,
                                    Model::ByteOrder byteOrder) const;
    Model::ByteOrder effectiveByteOrder(const Model::RegisterConfig &config) const;

    std::unique_ptr<IDeviceClient> m_client;
    Model::ByteOrder m_defaultByteOrder;
};

} // namespace Comm
} // namespace DataCollection
