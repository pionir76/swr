#pragma once

#include "DeviceModels.h"
#include <QString>
#include <QVector>
#include <QDateTime>

namespace DataCollection {
namespace Model {

enum class DataQuality {
    Good,    // last polling time < 1.5× pollingInterval
    Normal,  // 1.5× ≤ last polling time < 3× pollingInterval
    Bad      // last polling time ≥ 3× pollingInterval or no response
};

inline QString dataQualityToString(DataQuality q)
{
    switch (q) {
    case DataQuality::Good:   return QStringLiteral("good");
    case DataQuality::Normal: return QStringLiteral("normal");
    default:                  return QStringLiteral("bad");
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

} // namespace Model
} // namespace DataCollection
