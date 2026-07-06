#include "RegisterExecutor.h"

namespace DataCollection {
namespace Comm {

RegisterExecutor::RegisterExecutor(std::unique_ptr<IDeviceClient> client,
                                   Model::ByteOrder defaultByteOrder)
    : m_client(std::move(client))
    , m_defaultByteOrder(defaultByteOrder)
{
}

bool RegisterExecutor::connect(QString &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    if (!m_client->connect()) {
        error = m_client->errorString();
        return false;
    }
    return true;
}

void RegisterExecutor::disconnect()
{
    if (m_client) {
        m_client->disconnect();
    }
}

bool RegisterExecutor::isConnected() const
{
    return m_client && m_client->isConnected();
}

bool RegisterExecutor::readField(const Model::RegisterConfig &config,
                                 QVector<quint16> &registerValues,
                                 QVector<bool> &coilValues,
                                 QString &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    switch (config.type) {
    case Model::RegisterType::Coil:
    case Model::RegisterType::DiscreteInput:
        return m_client->readBits(config.address, config.length, coilValues, error);

    case Model::RegisterType::HoldingRegister:
    case Model::RegisterType::InputRegister:
        if (!m_client->readWords(config.address, config.length, registerValues, error)) {
            return false;
        }
        registerValues = applyByteOrder(registerValues, effectiveByteOrder(config));
        return true;

    default:
        error = QStringLiteral("Unsupported register type: %1").arg(Model::registerTypeToString(config.type));
        return false;
    }
}

bool RegisterExecutor::writeField(const Model::RegisterConfig &config,
                                  const QVector<quint16> &registerValues,
                                  const QVector<bool> &coilValues,
                                  QString &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    if (config.readOnly) {
        error = QStringLiteral("Register is read-only: %1").arg(config.tagName);
        return false;
    }

    //------------------------------------------------------------------------//
    // Validate the length of the provided values against the register length
    //
    // Modbus protocol does not support writing to InputRegisters or DiscreteInputs, 
    // so we only handle Coils and HoldingRegisters here.
    //------------------------------------------------------------------------//
    switch (config.type) {
    case Model::RegisterType::Coil:{
        if (coilValues.size() != config.length) {
            error = QStringLiteral("Bit value count does not match register length for %1").arg(config.tagName);
            return false;
        }
        return m_client->writeBits(config.address, coilValues, error);
    }

    case Model::RegisterType::HoldingRegister:{
        if (registerValues.size() != config.length) {
            error = QStringLiteral("Word value count does not match register length for %1").arg(config.tagName);
            return false;
        }
        const QVector<quint16> ordered = applyByteOrder(registerValues, effectiveByteOrder(config));
        return m_client->writeWords(config.address, ordered, error);
    }

    default:
        error = QStringLiteral("Unsupported write target: %1").arg(Model::registerTypeToString(config.type));
        return false;
    }
}

bool RegisterExecutor::readBatch(int startAddress,
                                 int totalLength,
                                 Model::RegisterType type,
                                 QVector<quint16> &registerValues,
                                 QVector<bool>    &coilValues,
                                 QString          &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    switch (type) {
    case Model::RegisterType::Coil:
    case Model::RegisterType::DiscreteInput:
        return m_client->readBits(startAddress, totalLength, coilValues, error);

    case Model::RegisterType::HoldingRegister:
    case Model::RegisterType::InputRegister:
        return m_client->readWords(startAddress, totalLength, registerValues, error);

    default:
        error = QStringLiteral("Unsupported register type: %1").arg(Model::registerTypeToString(type));
        return false;
    }
}

QVector<quint16> RegisterExecutor::applyFieldByteOrder(const QVector<quint16> &values,
                                                        const Model::RegisterConfig &config) const
{
    return applyByteOrder(values, effectiveByteOrder(config));
}

QVector<quint16> RegisterExecutor::applyByteOrder(const QVector<quint16> &values,
                                                  Model::ByteOrder byteOrder) const
{
    if (values.size() <= 1 || byteOrder == Model::ByteOrder::BigEndian) {
        return values;
    }

    QVector<quint16> ordered;
    ordered.reserve(values.size());
    for (int i = values.size() - 1; i >= 0; --i) {
        ordered.append(values.at(i));
    }
    return ordered;
}

Model::ByteOrder RegisterExecutor::effectiveByteOrder(const Model::RegisterConfig &config) const
{
    if (config.byteOrder != Model::ByteOrder::Default) {
        return config.byteOrder;
    }
    return m_defaultByteOrder;
}


} // namespace Comm
} // namespace DataCollection
