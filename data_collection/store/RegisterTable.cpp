#include "RegisterTable.h"

namespace DataCollection {
namespace Store {

static double clampValue(double value, double minValue, double maxValue)
{
    if(value < minValue) {
        return minValue;
    }
    if(value > maxValue) {
        return maxValue;
    }
    return value;
}

void RegisterTable::clear()
{
    QMutexLocker locker(&m_mutex);
    m_unifiedRegisters.clear();
    m_fieldToUnifiedId.clear();
    m_usedIds.clear();
    m_nextUnifiedId = 1;
}

void RegisterTable::updateUnifiedRegister(int deviceId,
                                          const Model::RegisterField &field,
                                          const QVector<quint16> &registerValues,
                                          const QVector<bool> &coilValues,
                                          bool success,
                                          const QString &errorMessage,
                                          int pollingIntervalMs)
{
    QMutexLocker locker(&m_mutex);
    const int unifiedId = resolveUnifiedId(deviceId, field);

    Model::UnifiedRegister entry = m_unifiedRegisters.value(unifiedId);

    entry.id          = unifiedId;
    entry.tagName     = field.tagName.isEmpty()     ? QStringLiteral("NO TAGNAME_%1").arg(unifiedId) : field.tagName;
    entry.displayName = field.displayName.isEmpty() ? QStringLiteral("NOT DEFINED")                   : field.displayName;

    entry.unit              = field.unit;
    entry.scale             = field.scale;
    entry.isSigned          = field.isSigned;
    entry.bitLabels         = field.bitLabels;
    entry.minValue          = field.minValue;
    entry.maxValue          = field.maxValue;
    entry.deviceId          = deviceId;
    entry.deviceAddress     = field.address;
    entry.sourceType        = field.type;
    entry.pollingIntervalMs = pollingIntervalMs;
    entry.isValid           = success;
    entry.readOnly          = field.readOnly;
    entry.errorMessage      = errorMessage;

    if (success) {
        entry.lastUpdated  = QDateTime::currentDateTimeUtc();
        entry.rawRegisters = registerValues;
        entry.rawCoils     = coilValues;
        entry.scaledValue  = computeScaledValue(entry);
        entry.outOfRange   = entry.scaledValue < entry.minValue || entry.scaledValue > entry.maxValue;
        entry.scaledValue  = clampValue(entry.scaledValue, entry.minValue, entry.maxValue);
    }
    // 에러 시 lastUpdated/rawRegisters/rawCoils/scaledValue/outOfRange는 직전 값 유지
    // → lastUpdated가 갱신되지 않아 elapsed가 축적되어 quality가 Normal → Bad로 떨어짐

    m_unifiedRegisters[unifiedId] = std::move(entry);
}


Model::UnifiedRegister RegisterTable::unifiedRegister(int unifiedId) const
{
    QMutexLocker locker(&m_mutex);
    return m_unifiedRegisters.value(unifiedId);
}

QList<Model::UnifiedRegister> RegisterTable::unifiedRegisters() const
{
    QMutexLocker locker(&m_mutex);
    return m_unifiedRegisters.values();
}


int RegisterTable::resolveUnifiedId(int deviceId, const Model::RegisterField &field)
{
    const QString key = fieldKey(deviceId, field);

    if( m_fieldToUnifiedId.contains(key)) {
        return m_fieldToUnifiedId.value(key);
    }

    int assignedId;

    // If User Specificed the Unified Register ID
    if (field.unifiedRegisterId >= 0) {
        assignedId = field.unifiedRegisterId;

        if (m_nextUnifiedId <= assignedId) {
            m_nextUnifiedId = assignedId + 1;
        }
    }

    // If User Specificed the Unified Register ID As Auto Assign.
    else {
        while (m_usedIds.contains(m_nextUnifiedId)) {
            ++m_nextUnifiedId;
        }
        assignedId = m_nextUnifiedId++;
    }

    m_fieldToUnifiedId.insert(key, assignedId);
    m_usedIds.insert(assignedId);
    return assignedId;
}

QString RegisterTable::fieldKey(int deviceId, const Model::RegisterField &field) const
{
    return QStringLiteral("%1:%2:%3")
        .arg(deviceId)
        .arg(field.address)
        .arg(Model::registerTypeToString(field.type));
}

double RegisterTable::computeScaledValue(const Model::UnifiedRegister &entry) const
{
    //-----------------------------------------------------------------//
    // If the register is a holding/input/word register, compute 
    // the scaled value based on the raw register values
    //-----------------------------------------------------------------//
    if (!entry.rawRegisters.isEmpty()) {
        quint64 combined = 0;
        if (entry.rawRegisters.size() > 1) {
            for (quint16 word : entry.rawRegisters) {
                combined = (combined << 16) | word;
            }
            if (entry.isSigned) {
                const int bits = entry.rawRegisters.size() * 16;
                const quint64 signBit = quint64(1) << (bits - 1);
                if (combined & signBit)
                    combined |= ~((signBit << 1) - 1);
                return static_cast<double>(static_cast<qint64>(combined)) * entry.scale;
            }
        } else {
            combined = static_cast<quint16>(entry.rawRegisters.first());

            if (entry.isSigned){
                return static_cast<double>(static_cast<qint16>(combined)) * entry.scale;
            }
        }
        return static_cast<double>(combined) * entry.scale;
    }

    //-----------------------------------------------------------------//
    // If the register is a coil or bit register, compute 
    // the scaled value based on the number of true bits
    //-----------------------------------------------------------------//
    if (!entry.rawCoils.isEmpty()) {
        if (entry.rawCoils.size() == 1) {
            return entry.rawCoils.first() ? 1.0 * entry.scale : 0.0;
        }
        int trueCount = 0;
        for (bool bit : entry.rawCoils) {
            if (bit) {
                ++trueCount;
            }
        }
        return static_cast<double>(trueCount) * entry.scale;
    }
    return 0.0;
}

} // namespace Store
} // namespace DataCollection
