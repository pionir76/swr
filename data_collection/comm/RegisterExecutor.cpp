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

bool RegisterExecutor::readField(const Model::RegisterField &field,
                                 QVector<quint16> &registerValues,
                                 QVector<bool> &coilValues,
                                 QString &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    if (field.length > MAX_READ_QUANTITY) {
        error = QStringLiteral("Read quantity exceeded (%1): requested %2 for field %3")
        .arg(MAX_READ_QUANTITY).arg(field.length).arg(field.tagName);
        return false;
    }

    switch (field.type) {
    case Model::RegisterType::Coil:
    case Model::RegisterType::DiscreteInput:
    case Model::RegisterType::BitRegister:
        return m_client->readBits(field.address, field.length, coilValues, error);

    case Model::RegisterType::HoldingRegister:
    case Model::RegisterType::InputRegister:
    case Model::RegisterType::WordRegister:
        if (!m_client->readWords(field.address, field.length, registerValues, error)) {
            return false;
        }
        registerValues = applyByteOrder(registerValues, effectiveByteOrder(field));
        return true;

    default:
        error = QStringLiteral("Unsupported register type: %1").arg(Model::registerTypeToString(field.type));
        return false;
    }
}

bool RegisterExecutor::writeField(const Model::RegisterField &field,
                                  const QVector<quint16> &registerValues,
                                  const QVector<bool> &coilValues,
                                  QString &error)
{
    if (!m_client) {
        error = QStringLiteral("Device client is not initialized");
        return false;
    }

    if (field.readOnly) {
        error = QStringLiteral("Field is read-only: %1").arg(field.tagName);
        return false;
    }

    if (field.length > MAX_READ_QUANTITY) {
        error = QStringLiteral("Write quantity exceeded (%1): requested %2 for field %3")
        .arg(MAX_READ_QUANTITY).arg(field.length).arg(field.tagName);
        return false;
    }

    switch (field.type) {
    case Model::RegisterType::Coil:
    case Model::RegisterType::BitRegister: {
        if (coilValues.size() != field.length) {
            error = QStringLiteral("Bit value count does not match field length for %1").arg(field.tagName);
            return false;
        }
        return m_client->writeBits(field.address, coilValues, error);
    }

    case Model::RegisterType::HoldingRegister:
    case Model::RegisterType::WordRegister: {
        if (registerValues.size() != field.length) {
            error = QStringLiteral("Word value count does not match field length for %1").arg(field.tagName);
            return false;
        }
        const QVector<quint16> ordered = applyByteOrder(registerValues, effectiveByteOrder(field));
        return m_client->writeWords(field.address, ordered, error);
    }

    default:
        error = QStringLiteral("Unsupported write target: %1").arg(Model::registerTypeToString(field.type));
        return false;
    }
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

Model::ByteOrder RegisterExecutor::effectiveByteOrder(const Model::RegisterField &field) const
{
    if (field.byteOrder != Model::ByteOrder::Default) {
        return field.byteOrder;
    }
    return m_defaultByteOrder;
}


} // namespace Comm
} // namespace DataCollection
