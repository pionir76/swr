#pragma once

#include <QHash>
#include <QVector>
#include <QString>
#include <QMutex>

#include "../model/UnifiedRegister.h"
#include "../model/DeviceModels.h"

namespace DataCollection {
namespace Store {

class RegisterTable {

public:
    void clear();
    void updateUnifiedRegister(int deviceId,
                               const Model::RegisterField &field,
                               const QVector<quint16> &registerValues,
                               const QVector<bool> &coilValues,
                               bool success,
                               const QString &errorMessage,
                               int pollingIntervalMs);

    Model::UnifiedRegister unifiedRegister(int unifiedId) const;
    QList<Model::UnifiedRegister> unifiedRegisters() const;

private:
    int resolveUnifiedId(int deviceId, const Model::RegisterField &field);
    QString fieldKey(int deviceId, const Model::RegisterField &field) const;
    double computeScaledValue(const Model::UnifiedRegister &entry) const;

    mutable QMutex m_mutex;
    QHash<int, Model::UnifiedRegister> m_unifiedRegisters;
    QHash<QString, int> m_fieldToUnifiedId;
    QSet<int> m_usedIds;
    int m_nextUnifiedId = 1;

};

} // namespace Store
} // namespace DataCollection
