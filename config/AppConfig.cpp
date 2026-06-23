#include "AppConfig.h"

#include <QDate>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "../utils/Logger.h"

// ---------------------------------------------------------------------------
// factory default
// ---------------------------------------------------------------------------

AppConfig factoryDefaultConfig()
{
    AppConfig cfg;

    NetInterfaceConfig eth0;
    eth0.name      = QStringLiteral("eth0");
    eth0.role      = QStringLiteral("field");
    eth0.enabled   = true;
    eth0.mode      = QStringLiteral("static");
    eth0.ipAddress = QStringLiteral("192.168.0.150");
    eth0.netmask   = QStringLiteral("255.255.255.0");
    eth0.gateway   = QStringLiteral("192.168.0.1");
    eth0.dns       = QStringLiteral("8.8.8.8");
    cfg.networkInterfaces.append(eth0);

    NetInterfaceConfig eth1;
    eth1.name      = QStringLiteral("eth1");
    eth1.role      = QStringLiteral("service");
    eth1.enabled   = true;
    eth1.mode      = QStringLiteral("static");
    eth1.ipAddress = QStringLiteral("192.168.0.151");
    eth1.netmask   = QStringLiteral("255.255.255.0");
    eth1.gateway   = QStringLiteral("");
    eth1.dns       = QStringLiteral("");
    cfg.networkInterfaces.append(eth1);

    cfg.rs485.device   = QStringLiteral("/dev/ttymxc1");
    cfg.rs485.baudRate = 9600;
    cfg.rs485.dataBits = 8;
    cfg.rs485.parity   = QStringLiteral("none");
    cfg.rs485.stopBits = 1;

    cfg.system.hostname  = QStringLiteral("smartroute");
    cfg.system.ntpServer = QStringLiteral("pool.ntp.org");

    cfg.modbusServer.enabled = false;
    cfg.modbusServer.port    = 502;
    cfg.modbusServer.slaveId = 1;

    cfg.loginSecurity.maxFailedAttempts     = 5;
    cfg.loginSecurity.sessionTimeoutMinutes = 30;
    cfg.loginSecurity.minPasswordLength     = 8;
    cfg.loginSecurity.autoLogout            = true;

    return cfg;
}

bool factoryReset(const QString &filePath, QString &error)
{
    return saveConfig(filePath, factoryDefaultConfig(), error);
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

AppConfig loadConfig(const QString &filePath)
{
    AppConfig config;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Config file not found, using defaults:" << filePath;
        return config;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        qWarning() << "Invalid JSON config file:" << filePath;
        return config;
    }

    const QJsonObject root = doc.object();

    // network
    const QJsonArray ifaces =
        root.value(QLatin1String("network")).toObject()
            .value(QLatin1String("interfaces")).toArray();
    for (const QJsonValue &v : ifaces) {
        const QJsonObject obj = v.toObject();
        NetInterfaceConfig iface;
        iface.name      = obj.value(QLatin1String("name")).toString();
        iface.role      = obj.value(QLatin1String("role")).toString();
        iface.enabled   = obj.value(QLatin1String("enabled")).toBool(true);
        iface.mode      = obj.value(QLatin1String("mode")).toString(QStringLiteral("static"));
        iface.ipAddress = obj.value(QLatin1String("ipAddress")).toString();
        iface.netmask   = obj.value(QLatin1String("netmask")).toString();
        iface.gateway   = obj.value(QLatin1String("gateway")).toString();
        iface.dns       = obj.value(QLatin1String("dns")).toString();
        config.networkInterfaces.append(iface);
    }

    // serial
    const QJsonObject serial = root.value(QLatin1String("serial")).toObject();
    config.rs485.device   = serial.value(QLatin1String("device")).toString(QStringLiteral("/dev/ttymxc1"));
    config.rs485.baudRate = serial.value(QLatin1String("baudRate")).toInt(9600);
    config.rs485.dataBits = serial.value(QLatin1String("dataBits")).toInt(8);
    config.rs485.parity   = serial.value(QLatin1String("parity")).toString(QStringLiteral("none"));
    config.rs485.stopBits = serial.value(QLatin1String("stopBits")).toInt(1);

    // system
    const QJsonObject sys = root.value(QLatin1String("system")).toObject();
    config.system.hostname  = sys.value(QLatin1String("hostname")).toString(QStringLiteral("smartroute"));
    config.system.ntpServer = sys.value(QLatin1String("ntpServer")).toString(QStringLiteral("pool.ntp.org"));

    // modbus_server
    const QJsonObject mbs = root.value(QLatin1String("modbus_server")).toObject();
    config.modbusServer.enabled = mbs.value(QLatin1String("enabled")).toBool(false);
    config.modbusServer.port    = static_cast<quint16>(mbs.value(QLatin1String("port")).toInt(502));
    config.modbusServer.slaveId = mbs.value(QLatin1String("slaveId")).toInt(1);

    // login_security
    const QJsonObject ls = root.value(QLatin1String("login_security")).toObject();
    config.loginSecurity.maxFailedAttempts     = ls.value(QLatin1String("maxFailedAttempts")).toInt(5);
    config.loginSecurity.sessionTimeoutMinutes = ls.value(QLatin1String("sessionTimeoutMinutes")).toInt(30);
    config.loginSecurity.minPasswordLength     = ls.value(QLatin1String("minPasswordLength")).toInt(8);
    config.loginSecurity.autoLogout            = ls.value(QLatin1String("autoLogout")).toBool(true);

    return config;
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

bool saveConfig(const QString &filePath, const AppConfig &config, QString &error)
{
    Util::Logger::info(QStringLiteral("Updated system config file"));

    QJsonArray ifaces;
    for (const NetInterfaceConfig &iface : config.networkInterfaces) {
        QJsonObject obj;
        obj[QLatin1String("name")]      = iface.name;
        obj[QLatin1String("role")]      = iface.role;
        obj[QLatin1String("enabled")]   = iface.enabled;
        obj[QLatin1String("mode")]      = iface.mode;
        obj[QLatin1String("ipAddress")] = iface.ipAddress;
        obj[QLatin1String("netmask")]   = iface.netmask;
        obj[QLatin1String("gateway")]   = iface.gateway;
        obj[QLatin1String("dns")]       = iface.dns;
        ifaces.append(obj);
    }

    QJsonObject net;
    net[QLatin1String("interfaces")] = ifaces;

    QJsonObject serial;
    serial[QLatin1String("device")]   = config.rs485.device;
    serial[QLatin1String("baudRate")] = config.rs485.baudRate;
    serial[QLatin1String("dataBits")] = config.rs485.dataBits;
    serial[QLatin1String("parity")]   = config.rs485.parity;
    serial[QLatin1String("stopBits")] = config.rs485.stopBits;

    QJsonObject sys;
    sys[QLatin1String("hostname")]  = config.system.hostname;
    sys[QLatin1String("ntpServer")] = config.system.ntpServer;

    QJsonObject mbs;
    mbs[QLatin1String("enabled")] = config.modbusServer.enabled;
    mbs[QLatin1String("port")]    = config.modbusServer.port;
    mbs[QLatin1String("slaveId")] = config.modbusServer.slaveId;

    QJsonObject ls;
    ls[QLatin1String("maxFailedAttempts")]     = config.loginSecurity.maxFailedAttempts;
    ls[QLatin1String("sessionTimeoutMinutes")] = config.loginSecurity.sessionTimeoutMinutes;
    ls[QLatin1String("minPasswordLength")]     = config.loginSecurity.minPasswordLength;
    ls[QLatin1String("autoLogout")]            = config.loginSecurity.autoLogout;

    QJsonObject root;
    root[QLatin1String("network")]        = net;
    root[QLatin1String("serial")]         = serial;
    root[QLatin1String("system")]         = sys;
    root[QLatin1String("modbus_server")]  = mbs;
    root[QLatin1String("login_security")] = ls;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("Cannot open config file for writing: %1").arg(filePath);
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
