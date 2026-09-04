#pragma once

#include <QString>
#include <QList>
#include <QSerialPort>


struct Rs485Config {
    QString device;
    int     baudRate = 9600;
    int     dataBits = 8;
    QString parity   = QStringLiteral("none");
    int     stopBits = 1;
};

struct NetInterfaceConfig {
    QString name;
    QString role;
    bool    enabled   = true;
    QString mode      = QStringLiteral("static");
    QString ipAddress;
    QString netmask;
    QString gateway;
    QString dns;
};

struct SysSettings {
    QString hostname  = QStringLiteral("smartroute");
    QString ntpServer = QStringLiteral("pool.ntp.org");
};

struct ModbusServerConfig {
    bool    enabled = false;
    quint16 port    = 502;
    int     slaveId = 1;
};

struct LoginSecurityConfig {
    int  maxFailedAttempts     = 5;
    int  sessionTimeoutMinutes = 30;
    int  minPasswordLength     = 8;
    bool autoLogout            = true;
};

struct TrendChannelConfig {
    int     regId    = -1;
    QString name;
    QString tag;                 // ASCII tag for .tnd file CHTAG[16], e.g. "CH_01"
    QString unit;
    double  scale    = 1.0;
    bool    isSigned = false;    // frontend sign interpretation (storage unaffected)
    quint16 minValue = 0;        // Y-axis lower bound (raw quint16 units)
    quint16 maxValue = 65535;    // Y-axis upper bound (raw quint16 units)
};

struct TrendConfig {
    int                       sampleIntervalSec = 10;  // allowed: 10 | 30 | 60
    QList<TrendChannelConfig> channels;                // max 16
};

struct AppConfig {
    QList<NetInterfaceConfig> networkInterfaces;
    Rs485Config               rs485;
    SysSettings               system;
    ModbusServerConfig        modbusServer;
    LoginSecurityConfig       loginSecurity;
    TrendConfig               trend;
};

inline QSerialPort::Parity rs485Parity(const Rs485Config &cfg)
{
    const QString p = cfg.parity.toLower();
    if (p == QLatin1String("even")) return QSerialPort::EvenParity;
    if (p == QLatin1String("odd"))  return QSerialPort::OddParity;
    return QSerialPort::NoParity;
}

inline QSerialPort::DataBits rs485DataBits(const Rs485Config &cfg)
{
    switch (cfg.dataBits) {
    case 5: return QSerialPort::Data5;
    case 6: return QSerialPort::Data6;
    case 7: return QSerialPort::Data7;
    default: return QSerialPort::Data8;
    }
}

inline QSerialPort::StopBits rs485StopBits(const Rs485Config &cfg)
{
    return cfg.stopBits == 2 ? QSerialPort::TwoStop : QSerialPort::OneStop;
}

AppConfig loadConfig(const QString &filePath);
bool      saveConfig(const QString &filePath, const AppConfig &config, QString &error);

AppConfig factoryDefaultConfig();
bool      factoryReset(const QString &filePath, QString &error);
