#include "RegisterTable.h"

namespace DataCollection {
namespace Store {

static double clampValue(double value, double minValue, double maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void RegisterTable::clear()
{
    QMutexLocker locker(&m_mutex);
    m_states.clear();
}

//---------------------------------------------------------------------//
// if m_states already contains an entry for this unifiedId, update it; 
// otherwise, create a new entry
//---------------------------------------------------------------------//
void RegisterTable::updateState(const Model::RegisterConfig &config,
                                const QVector<quint16> &registerValues,
                                const QVector<bool> &coilValues,
                                bool success,
                                const QString &errorMessage,
                                int pollingIntervalMs)
{
    QMutexLocker locker(&m_mutex);
    const int unifiedId = config.unifiedAddress;

    // DB-assigned IDs are always >= 0
    if (unifiedId < 0){
        return;  
    }

    Model::RegisterState entry = m_states.value(unifiedId);

    entry.config            = config;
    entry.pollingIntervalMs = pollingIntervalMs;
    entry.isValid           = success;
    entry.errorMessage      = errorMessage;

    if (success) {
        entry.lastUpdated  = QDateTime::currentDateTimeUtc();
        entry.rawRegisters = registerValues;
        entry.rawCoils     = coilValues;        
        entry.outOfRange   = entry.scaledValue < entry.config.minValue
                          || entry.scaledValue > entry.config.maxValue;

        entry.scaledValue  = computeScaledValue(entry);
        entry.scaledValue  = clampValue(entry.scaledValue,
                                        entry.config.minValue,
                                        entry.config.maxValue);
    }

    //------------------------------------------------------------------------------------------//
    // Note: In case of error, lastUpdated/rawRegisters/rawCoils/scaledValue/outOfRange 
    // retain their previous values
    // → lastUpdated does not get updated, so elapsed time accumulates, 
    // causing quality to degrade from Normal to Bad
    //
    // Update or create a new entry in m_states for this unifiedId
    //------------------------------------------------------------------------------------------//

    m_states[unifiedId] = std::move(entry);
}

Model::RegisterState RegisterTable::state(int unifiedId) const
{
    QMutexLocker locker(&m_mutex);
    return m_states.value(unifiedId);
}

QList<Model::RegisterState> RegisterTable::states() const
{
    QMutexLocker locker(&m_mutex);
    return m_states.values();
}

double RegisterTable::computeScaledValue(const Model::RegisterState &entry) const
{
    switch (entry.config.type) {

    case Model::RegisterType::Coil:
    case Model::RegisterType::DiscreteInput: {
        if (entry.rawCoils.isEmpty())
            return 0.0;

        if (entry.rawCoils.size() == 1)
            return entry.rawCoils.first() ? 1.0 * entry.config.scale : 0.0;

        int trueCount = 0;
        for (bool bit : entry.rawCoils)
            if (bit) ++trueCount;

        return static_cast<double>(trueCount) * entry.config.scale;
    }

    case Model::RegisterType::HoldingRegister:
    case Model::RegisterType::InputRegister:{
        if (entry.rawRegisters.isEmpty())
            return 0.0;

        quint64 combined = 0;

        if (entry.rawRegisters.size() > 1) {
            for (quint16 word : entry.rawRegisters)
                combined = (combined << 16) | word;

            if (entry.config.isSigned) {
                const int bits = entry.rawRegisters.size() * 16;
                const quint64 signBit = quint64(1) << (bits - 1);

                if (combined & signBit)
                    combined |= ~((signBit << 1) - 1);

                return static_cast<double>(static_cast<qint64>(combined)) * entry.config.scale;
            }
        }
        else {
            combined = static_cast<quint16>(entry.rawRegisters.first());

            if (entry.config.isSigned)
                return static_cast<double>(static_cast<qint16>(combined)) * entry.config.scale;
        }
        return static_cast<double>(combined) * entry.config.scale;
    }

    default:
        return 0.0;
    }
}

} // namespace Store
} // namespace DataCollection
