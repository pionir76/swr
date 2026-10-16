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
    void updateState(const Model::RegisterConfig &config,
                     const QVector<quint16> &registerValues,
                     const QVector<bool> &coilValues,
                     bool success,
                     const QString &errorMessage,
                     int pollingIntervalMs);

    Model::RegisterState state(int unifiedId) const;
    QList<Model::RegisterState> states() const;

private:
    double computeScaledValue(const Model::RegisterState &entry) const;

    mutable QMutex m_mutex;
    QHash<int, Model::RegisterState> m_states;

};

} // namespace Store
} // namespace DataCollection
