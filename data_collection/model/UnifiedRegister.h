#pragma once

#include "DeviceModels.h"
#include <QString>
#include <QVector>
#include <QDateTime>
#include <limits>

namespace DataCollection {
namespace Model {

enum class DataQuality {
    Good,    // 경과 시간 < 1.5× pollingInterval
    Normal,  // 1.5× ≤ 경과 시간 < 3× pollingInterval
    Bad      // 경과 시간 ≥ 3× pollingInterval  또는 미수신
};

inline QString dataQualityToString(DataQuality q)
{
    switch (q) {
    case DataQuality::Good:   return QStringLiteral("good");
    case DataQuality::Normal: return QStringLiteral("normal");
    default:                  return QStringLiteral("bad");
    }
}

struct UnifiedRegister {
    int id = -1;
    QString tagName;
    QString displayName;
    
    QString unit;
    double scale = 1.0;
    bool isSigned = false;
    QString bitLabels;
    double minValue = -std::numeric_limits<double>::infinity();
    double maxValue = std::numeric_limits<double>::infinity();
    int deviceId = -1;
    int deviceAddress = 0;
    RegisterType sourceType = RegisterType::Unknown;
    QVector<quint16> rawRegisters;
    QVector<bool> rawCoils;
    double scaledValue = 0.0;
    bool outOfRange = false;
    QDateTime lastUpdated;
    bool readOnly = false;
    bool isValid = true;
    QString errorMessage;
    int pollingIntervalMs = 1000;  // quality 계산 기준, 장비의 polling.intervalMs
};

} // namespace Model
} // namespace DataCollection
