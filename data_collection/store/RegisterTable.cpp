#include "RegisterTable.h"

namespace DataCollection {
namespace Store {

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
    }

    //------------------------------------------------------------------------------------------//
    // Note: In case of error, lastUpdated/rawRegisters/rawCoils
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

} // namespace Store
} // namespace DataCollection
