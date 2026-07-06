#include "BackupManager.h"
#include "BackupModels.h"

#include "../config/AppConfig.h"
#include "../config/SystemConfig.h"
#include "../data_collection/database/DeviceDatabase.h"
#include "../data_collection/model/DeviceModels.h"

#include <QBuffer>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QtCore/private/qzipwriter_p.h>

namespace Maintenance {

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------

QByteArray BackupManager::create(const DataCollection::Database::DeviceDatabase *db, QString &error)
{
    const QByteArray manifest  = buildManifest();
    const QByteArray config    = buildConfig();

    //------------------------------------------------------------//
    // isNull() Check DB Error, 0 Size is [] Not Error; 
    //------------------------------------------------------------//
    QString devErr;
    const QByteArray devices   = buildDevices(db, devErr);
    if (devices.isNull()) { 
        error = devErr; 
        return {}; 
    }

    QString regErr;
    const QByteArray registers = buildRegisters(db, regErr);
    if (registers.isNull()) { 
        error = regErr; 
        return {}; 
    }

    QString usrErr;
    const QByteArray users     = buildUsers(db, usrErr);
    if (users.isNull()) { 
        error = usrErr; 
        return {}; 
    }

    const QByteArray hmi       = buildHmi();

    QMap<QString, QByteArray> files;
    files[QStringLiteral("manifest.json")]  = manifest;
    files[QStringLiteral("config.json")]    = config;
    files[QStringLiteral("devices.json")]   = devices;
    files[QStringLiteral("registers.json")] = registers;
    files[QStringLiteral("users.json")]     = users;
    files[QStringLiteral("hmi.json")]       = hmi;

    const QByteArray checksum = buildChecksum(files);
    files[QStringLiteral("checksum.sha256")] = checksum;

    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    {
        QZipWriter zip(&buf);
        zip.setCompressionPolicy(QZipWriter::AutoCompress);
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            //------------------------------------------------------------//
            // Add File Name and Data to Zip
            //------------------------------------------------------------//
            zip.addFile(it.key(), it.value());
        }
        zip.close();
    }

    return buf.data();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

QByteArray BackupManager::buildManifest()
{
    const AppConfig &cfg = SystemConfig::config();

    BackupManifest m;
    m.createdAt               = QDateTime::currentDateTime().toString(Qt::ISODate);
    m.sourceDevice.hostname   = cfg.system.hostname;
    m.sourceDevice.version    = QStringLiteral(SR_VERSION);
    m.sourceDevice.revision   = QStringLiteral(SR_REVISION);
    m.sourceDevice.zcode      = QStringLiteral("Z") + QStringLiteral(SR_ZCODE);

    QJsonObject src;
    src[QLatin1String("hostname")]      = m.sourceDevice.hostname;
    src[QLatin1String("version")]       = m.sourceDevice.version;
    src[QLatin1String("revision")]      = m.sourceDevice.revision;
    src[QLatin1String("zcode")]         = m.sourceDevice.zcode;
    src[QLatin1String("schemaVersion")] = m.sourceDevice.schemaVersion;

    QJsonObject contents;
    contents[QLatin1String("config")]    = m.contents.config;
    contents[QLatin1String("devices")]   = m.contents.devices;
    contents[QLatin1String("registers")] = m.contents.registers;
    contents[QLatin1String("users")]     = m.contents.users;
    contents[QLatin1String("hmi")]       = m.contents.hmi;

    QJsonObject root;
    root[QLatin1String("product")]      = m.product;
    root[QLatin1String("createdAt")]    = m.createdAt;
    root[QLatin1String("sourceDevice")] = src;
    root[QLatin1String("contents")]     = contents;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildConfig()
{
    const AppConfig &cfg = SystemConfig::config();

    QJsonArray ifaces;
    for (const NetInterfaceConfig &iface : cfg.networkInterfaces) {
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

    QJsonObject serial;
    serial[QLatin1String("device")]   = cfg.rs485.device;
    serial[QLatin1String("baudRate")] = cfg.rs485.baudRate;
    serial[QLatin1String("dataBits")] = cfg.rs485.dataBits;
    serial[QLatin1String("parity")]   = cfg.rs485.parity;
    serial[QLatin1String("stopBits")] = cfg.rs485.stopBits;

    QJsonObject sys;
    sys[QLatin1String("hostname")]  = cfg.system.hostname;
    sys[QLatin1String("ntpServer")] = cfg.system.ntpServer;

    QJsonObject mbs;
    mbs[QLatin1String("enabled")] = cfg.modbusServer.enabled;
    mbs[QLatin1String("port")]    = cfg.modbusServer.port;
    mbs[QLatin1String("slaveId")] = cfg.modbusServer.slaveId;

    QJsonObject ls;
    ls[QLatin1String("maxFailedAttempts")]     = cfg.loginSecurity.maxFailedAttempts;
    ls[QLatin1String("sessionTimeoutMinutes")] = cfg.loginSecurity.sessionTimeoutMinutes;
    ls[QLatin1String("minPasswordLength")]     = cfg.loginSecurity.minPasswordLength;
    ls[QLatin1String("autoLogout")]            = cfg.loginSecurity.autoLogout;

    QJsonObject root;
    root[QLatin1String("network")]        = QJsonObject{{QLatin1String("interfaces"), ifaces}};
    root[QLatin1String("serial")]         = serial;
    root[QLatin1String("system")]         = sys;
    root[QLatin1String("modbus_server")]  = mbs;
    root[QLatin1String("login_security")] = ls;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildDevices(const DataCollection::Database::DeviceDatabase *db, QString &error)
{
    using namespace DataCollection::Model;

    const QList<DeviceInfo> devices = db->loadDevices(error);
    if (!error.isEmpty())
        return {};

    QJsonArray arr;
    for (const DeviceInfo &d : devices) {
        QJsonObject conn;
        conn[QLatin1String("type")]             = connectionTypeToString(d.connection.type);
        conn[QLatin1String("protocol")]         = protocolToString(d.connection.protocol);
        conn[QLatin1String("defaultByteOrder")] = byteOrderToString(d.connection.defaultByteOrder);
        conn[QLatin1String("ipAddress")]        = d.connection.ipAddress;
        conn[QLatin1String("tcpPort")]          = d.connection.tcpPort;
        conn[QLatin1String("slaveId")]          = d.connection.slaveId;
        conn[QLatin1String("timeoutMs")]        = d.connection.timeoutMs;

        QJsonObject polling;
        polling[QLatin1String("intervalMs")]  = d.polling.intervalMs;
        polling[QLatin1String("retryCount")]  = d.polling.retryCount;

        QJsonObject obj;
        obj[QLatin1String("id")]          = d.id;
        obj[QLatin1String("deviceCode")]  = d.deviceCode;
        obj[QLatin1String("name")]        = d.name;
        obj[QLatin1String("displayName")] = d.displayName;
        obj[QLatin1String("connection")]  = conn;
        obj[QLatin1String("polling")]     = polling;

        arr.append(obj);
    }

    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildRegisters(const DataCollection::Database::DeviceDatabase *db, QString &error)
{
    using namespace DataCollection::Model;

    const QList<DeviceInfo> devices = db->loadDevices(error);
    if (!error.isEmpty())
        return {};

    QJsonArray arr;
    for (const DeviceInfo &d : devices) {
        for (const RegisterConfig &f : d.registers) {
            QJsonObject obj;
            obj[QLatin1String("deviceId")]          = d.id;
            obj[QLatin1String("tagName")]            = f.tagName;
            obj[QLatin1String("displayName")]        = f.displayName;
            obj[QLatin1String("address")]            = f.address;
            obj[QLatin1String("type")]               = registerTypeToString(f.type);
            obj[QLatin1String("readOnly")]           = f.readOnly;
            obj[QLatin1String("length")]             = f.length;
            obj[QLatin1String("unifiedRegisterId")]  = f.unifiedRegisterId;
            obj[QLatin1String("unit")]               = f.unit;
            obj[QLatin1String("scale")]              = f.scale;
            obj[QLatin1String("isSigned")]           = f.isSigned;
            obj[QLatin1String("minValue")]           = f.minValue;
            obj[QLatin1String("maxValue")]           = f.maxValue;
            obj[QLatin1String("byteOrder")]          = byteOrderToString(f.byteOrder);
            obj[QLatin1String("bitLabels")]          = f.bitLabels;

            arr.append(obj);
        }
    }

    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildUsers(const DataCollection::Database::DeviceDatabase *db, QString &error)
{
    using namespace DataCollection::Model;

    const QList<UserInfo> users = db->loadUsers(error);
    if (!error.isEmpty())
        return {};

    QJsonArray arr;
    for (const UserInfo &u : users) {

        if (u.id == 0)
            continue;

        QJsonObject obj;
        obj[QLatin1String("username")]     = u.username;
        obj[QLatin1String("displayName")]  = u.displayName;
        obj[QLatin1String("description")]  = u.description;
        obj[QLatin1String("passwordHash")] = u.passwordHash;
        obj[QLatin1String("role")]         = userRoleToString(u.role);
        obj[QLatin1String("status")]       = userStatusToString(u.status);

        arr.append(obj);
    }

    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildHmi()
{
    return QJsonDocument(QJsonObject{}).toJson(QJsonDocument::Indented);
}

QByteArray BackupManager::buildChecksum(const QMap<QString, QByteArray> &files)
{
    QByteArray result;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QByteArray hash = QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex();
        result += hash + "  " + it.key().toUtf8() + '\n';
    }
    return result;
}

} // namespace Maintenance
