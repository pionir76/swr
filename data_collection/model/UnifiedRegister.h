#pragma once

#include "DeviceModels.h"
#include <QString>
#include <QVector>
#include <QDateTime>

namespace DataCollection {
namespace Model {

enum class DataQuality {
    Good,     // last polling time < 1.5× pollingInterval
    Normal,   // 1.5× ≤ last polling time < 3× pollingInterval
    Bad,      // last polling time ≥ 3× pollingInterval or no response
    Disabled  // owning device is disabled by the user
};

inline QString dataQualityToString(DataQuality q)
{
    switch (q) {
    case DataQuality::Good:     return QStringLiteral("good");
    case DataQuality::Normal:   return QStringLiteral("normal");
    case DataQuality::Disabled: return QStringLiteral("disabled");
    default:                    return QStringLiteral("bad");
    }
}

struct RegisterState {
    //----------------------------------------//
    // Register SnapShot.
    //----------------------------------------//
    RegisterConfig      config;

    //----------------------------------------//
    // Values for Run-time
    //----------------------------------------//
    QVector<quint16>    rawRegisters;
    QVector<bool>       rawCoils;
    double              scaledValue       = 0.0;
    bool                outOfRange        = false;
    QDateTime           lastUpdated;
    bool                isValid           = true;
    QString             errorMessage;
    int                 pollingIntervalMs = 1000;
};

inline DataQuality computeQuality(const RegisterState &r, bool deviceDisabled, const QDateTime &nowUtc)
{
    if (deviceDisabled)
        return DataQuality::Disabled;

    if (r.lastUpdated.isValid() && r.pollingIntervalMs > 0) {
        const qint64 elapsedMs = r.lastUpdated.msecsTo(nowUtc);
        if (elapsedMs < r.pollingIntervalMs * 3 / 2)
            return DataQuality::Good;
        if (elapsedMs < r.pollingIntervalMs * 3)
            return DataQuality::Normal;
    }
    return DataQuality::Bad;
}

} // namespace Model
} // namespace DataCollection
