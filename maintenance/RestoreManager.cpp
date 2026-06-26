#include "RestoreManager.h"
#include "BackupManager.h"

#include "../config/AppConfig.h"
#include "../config/SystemConfig.h"
#include "../data_collection/database/DeviceDatabase.h"
#include "../data_collection/polling/PollingManager.h"
#include "../utils/Logger.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QUuid>
#include <QtCore/private/qzipreader_p.h>

namespace Maintenance {

// ---------------------------------------------------------------------------
// public — validate
// ---------------------------------------------------------------------------

RestorePreview RestoreManager::validate(const QByteArray &zipData, QString &error)
{
    RestorePreview preview;

    //-----------------------------------------------------------//
    // 0. Clear previous restore session
    //-----------------------------------------------------------//
    QDir(QStringLiteral(SR_RESTORE_TEMP_DIR)).removeRecursively();
    QDir().mkpath(QStringLiteral(SR_RESTORE_TEMP_DIR));

    //-----------------------------------------------------------//
    // 1. Check ZIP format
    //-----------------------------------------------------------//
    QBuffer buf;
    buf.setData(zipData);
    buf.open(QIODevice::ReadOnly);
    QZipReader zip(&buf);

    if (zip.status() != QZipReader::NoError) {
        error = QStringLiteral("Invalid ZIP format");
        Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));
        return preview;
    }

    //-----------------------------------------------------------//
    // 2. Extract file list — prevent path traversal
    //-----------------------------------------------------------//
    QMap<QString, QByteArray> files;
    const auto fileInfoList = zip.fileInfoList();

    for (const QZipReader::FileInfo &fi : fileInfoList) {
        if (!fi.isFile) continue;

        if (fi.filePath.contains(QLatin1String("..")) ||
            fi.filePath.contains(QLatin1Char('/'))) {

            error = QStringLiteral("Invalid file path in ZIP: %1").arg(fi.filePath);
            Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));
            return preview;
        }
        files[fi.filePath] = zip.fileData(fi.filePath);
    }

    //-----------------------------------------------------------//
    // 3. Check required files
    //-----------------------------------------------------------//
    if (!files.contains(QStringLiteral("manifest.json")) ||
        !files.contains(QStringLiteral("checksum.sha256"))) {

        error = QStringLiteral("Missing required files (manifest.json, checksum.sha256)");
        Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));
        return preview;
    }

    //-----------------------------------------------------------//
    // 4. CheckSum Check
    //-----------------------------------------------------------//
    const QByteArray checksumRaw = files[QStringLiteral("checksum.sha256")];

    for (const QByteArray &line : checksumRaw.split('\n')) {
        const QByteArray trimmed = line.trimmed();

        if (trimmed.isEmpty())
            continue;

        const int sep = trimmed.indexOf("  ");
        if (sep < 0) 
            continue;

        const QString expectedHash = QString::fromLatin1(trimmed.left(sep));
        const QString filename     = QString::fromLatin1(trimmed.mid(sep + 2)).trimmed();

        if (!files.contains(filename)) {
            error = QStringLiteral("Checksum references missing file: %1").arg(filename);
            Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));
            return preview;
        }

        const QString actualHash = QString::fromLatin1(
            QCryptographicHash::hash(files[filename], 
            QCryptographicHash::Sha256).toHex());
        
        if (actualHash != expectedHash) {
            error = QStringLiteral("Checksum mismatch: %1").arg(filename);
            Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));
            
            return preview;
        }
    }

    //-----------------------------------------------------------//
    // 5. Parse manifest & check schemaVersion compatibility
    //-----------------------------------------------------------//
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(files[QStringLiteral("manifest.json")]);

    if (!manifestDoc.isObject()) {
        error = QStringLiteral("Invalid manifest.json");
        Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));

        return preview;
    }

    const QJsonObject manifest = manifestDoc.object();
    const QJsonObject src      = manifest[QLatin1String("sourceDevice")].toObject();
    const int schemaVersion    = src[QLatin1String("schemaVersion")].toInt();

    // DB schema version mismatch
    if (schemaVersion != SR_SCHEMA_VERSION) {
        error = QStringLiteral("Incompatible schemaVersion: backup=%1, device=%2").arg(schemaVersion).arg(SR_SCHEMA_VERSION);
        Util::Logger::warning(QStringLiteral("Restore validate failed: %1").arg(error));

        return preview;
    }

    //-----------------------------------------------------------//
    // 6. Fill RestorePreview
    //-----------------------------------------------------------//
    preview.backupInfo.product       = manifest[QLatin1String("product")].toString();
    preview.backupInfo.createdAt     = manifest[QLatin1String("createdAt")].toString();
    preview.backupInfo.hostname      = src[QLatin1String("hostname")].toString();
    preview.backupInfo.version       = src[QLatin1String("version")].toString();
    preview.backupInfo.revision      = src[QLatin1String("revision")].toString();
    preview.backupInfo.zcode         = src[QLatin1String("zcode")].toString();
    preview.backupInfo.schemaVersion = schemaVersion;

    //-----------------------------------------------------------//
    // 7. Check hostname mismatch
    //-----------------------------------------------------------//
    const QString myHostname = SystemConfig::config().system.hostname;

    if (preview.backupInfo.hostname != myHostname) {
        preview.warnings << QStringLiteral("Backup was created on '%1', current device is '%2'")
                                .arg(preview.backupInfo.hostname, myHostname);
    }

    //-----------------------------------------------------------//
    // 8. Check available items
    //-----------------------------------------------------------//
    if (files.contains(QStringLiteral("config.json"))) {
        preview.config.available = true;
        preview.config.count     = 1;
        preview.config.warning   = QStringLiteral("Network settings will NOT be restored unless explicitly enabled");
    }

    if (files.contains(QStringLiteral("devices.json"))) {
        const QJsonArray arr = QJsonDocument::fromJson(files[QStringLiteral("devices.json")]).array();
        preview.devices.available = true;
        preview.devices.count     = arr.size();
    }

    if (files.contains(QStringLiteral("registers.json"))) {
        const QJsonArray arr = QJsonDocument::fromJson(files[QStringLiteral("registers.json")]).array();
        preview.registers.available = true;
        preview.registers.count     = arr.size();
    }

    if (files.contains(QStringLiteral("users.json"))) {
        const QJsonArray arr = QJsonDocument::fromJson(files[QStringLiteral("users.json")]).array();
        preview.users.available = true;
        preview.users.count     = arr.size();
    }

    if (files.contains(QStringLiteral("hmi.json"))) {
        preview.hmi.available = true;
        preview.hmi.count     = 1;
        preview.hmi.warning   = QStringLiteral("HMI restore is not yet implemented");
    }

    //-----------------------------------------------------------//
    // 9. Save temp files for later apply
    //-----------------------------------------------------------//
    const QString restoreId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString dir       = QStringLiteral(SR_RESTORE_TEMP_DIR "/") + restoreId;

    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("Failed to create temp dir: %1").arg(dir);
        Util::Logger::error(QStringLiteral("Restore validate failed: %1").arg(error));

        return preview;
    }

    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QFile f(dir + QLatin1Char('/') + it.key());

        if (!f.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("Failed to write temp file: %1").arg(it.key());
            QDir(dir).removeRecursively();
        
            Util::Logger::error(QStringLiteral("Restore validate failed: %1").arg(error));
            return preview;
        }
        f.write(it.value());
    }

    preview.restoreId = restoreId;
    preview.valid     = true;

    Util::Logger::info(QStringLiteral(
        "Restore validated: id=%1 devices=%2 registers=%3 users=%4")
        .arg(restoreId).arg(preview.devices.count)
        .arg(preview.registers.count).arg(preview.users.count));

    return preview;
}

// ---------------------------------------------------------------------------
// public — apply
// ---------------------------------------------------------------------------
bool RestoreManager::apply(const QString &restoreId,
                           const RestoreOptions &options,
                           DataCollection::Database::DeviceDatabase *db,
                           DataCollection::Polling::PollingManager *pollingManager,
                           bool &restartRequired,
                           QString &error)
{
    restartRequired = false;

    //-----------------------------------------------------------//
    // 1. Check temp dir exists
    //-----------------------------------------------------------//
    const QString dir = QStringLiteral(SR_RESTORE_TEMP_DIR "/") + restoreId;
    if (!QDir(dir).exists()) {
        error = QStringLiteral("Restore session not found or expired: %1").arg(restoreId);
        return false;
    }

    //-----------------------------------------------------------//
    // 2. Check if polling is running
    //-----------------------------------------------------------//
    if (pollingManager->isRunning()) {
        error = QStringLiteral("Stop polling before applying restore");
        return false;
    }
    
    Util::Logger::info(QStringLiteral("Restore apply started: id=%1").arg(restoreId));

    //-----------------------------------------------------------//
    // 4. Read temp files
    //-----------------------------------------------------------//
    auto readJson = [&dir](const QString &name, bool &ok) -> QByteArray {
        QFile f(dir + QLatin1Char('/') + name);
        ok = f.open(QIODevice::ReadOnly);
        return ok ? f.readAll() : QByteArray{};
    };

    bool ok;
    const QJsonArray devicesArr =
        (options.devices)
            ? QJsonDocument::fromJson(readJson(QStringLiteral("devices.json"), ok)).array()
            : QJsonArray{};

    const QJsonArray registersArr =
        (options.registers)
            ? QJsonDocument::fromJson(readJson(QStringLiteral("registers.json"), ok)).array()
            : QJsonArray{};

    const QJsonArray usersArr =
        (options.users)
            ? QJsonDocument::fromJson(readJson(QStringLiteral("users.json"), ok)).array()
            : QJsonArray{};

    //-----------------------------------------------------------//
    // 5. DB (devices + registers + users)
    //-----------------------------------------------------------//
    if (options.devices || options.users) {
        if (!db->restoreData(options.devices, 
                             devicesArr, 
                             registersArr,
                             options.users, 
                             usersArr, 
                             error)) {
            Util::Logger::error(QStringLiteral("Restore DB failed: %1").arg(error));
            QDir(dir).removeRecursively();
            return false;
        }
    }

    //-----------------------------------------------------------//
    // 6. Config restore (system, serial, modbus_server, login_security, network)
    //-----------------------------------------------------------//
    if (options.config) {
        const QByteArray configData = readJson(QStringLiteral("config.json"), ok);
        
        if (ok && !configData.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(configData);
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                AppConfig cfg = SystemConfig::config();  // 현재 설정 복사

                // system
                const QJsonObject sys = root[QLatin1String("system")].toObject();
                if (!sys.isEmpty()) {
                    cfg.system.hostname  = sys[QLatin1String("hostname")].toString(cfg.system.hostname);
                    cfg.system.ntpServer = sys[QLatin1String("ntpServer")].toString(cfg.system.ntpServer);
                }
                // serial
                const QJsonObject serial = root[QLatin1String("serial")].toObject();
                if (!serial.isEmpty()) {
                    cfg.rs485.device   = serial[QLatin1String("device")].toString(cfg.rs485.device);
                    cfg.rs485.baudRate = serial[QLatin1String("baudRate")].toInt(cfg.rs485.baudRate);
                    cfg.rs485.dataBits = serial[QLatin1String("dataBits")].toInt(cfg.rs485.dataBits);
                    cfg.rs485.parity   = serial[QLatin1String("parity")].toString(cfg.rs485.parity);
                    cfg.rs485.stopBits = serial[QLatin1String("stopBits")].toInt(cfg.rs485.stopBits);
                }
                // modbus_server
                const QJsonObject mbs = root[QLatin1String("modbus_server")].toObject();
                if (!mbs.isEmpty()) {
                    cfg.modbusServer.enabled = mbs[QLatin1String("enabled")].toBool(cfg.modbusServer.enabled);
                    cfg.modbusServer.port    = static_cast<quint16>(mbs[QLatin1String("port")].toInt(cfg.modbusServer.port));
                    cfg.modbusServer.slaveId = mbs[QLatin1String("slaveId")].toInt(cfg.modbusServer.slaveId);
                }
                // login_security
                const QJsonObject ls = root[QLatin1String("login_security")].toObject();
                if (!ls.isEmpty()) {
                    cfg.loginSecurity.maxFailedAttempts     = ls[QLatin1String("maxFailedAttempts")].toInt(cfg.loginSecurity.maxFailedAttempts);
                    cfg.loginSecurity.sessionTimeoutMinutes = ls[QLatin1String("sessionTimeoutMinutes")].toInt(cfg.loginSecurity.sessionTimeoutMinutes);
                    cfg.loginSecurity.minPasswordLength     = ls[QLatin1String("minPasswordLength")].toInt(cfg.loginSecurity.minPasswordLength);
                    cfg.loginSecurity.autoLogout            = ls[QLatin1String("autoLogout")].toBool(cfg.loginSecurity.autoLogout);
                }
                // network (기본 OFF)
                if (options.network) {
                    const QJsonArray ifaces =
                        root[QLatin1String("network")].toObject()[QLatin1String("interfaces")].toArray();
                    if (!ifaces.isEmpty()) {
                        cfg.networkInterfaces.clear();
                        for (const QJsonValue &v : ifaces) {
                            const QJsonObject obj = v.toObject();
                            NetInterfaceConfig iface;
                            iface.name      = obj[QLatin1String("name")].toString();
                            iface.role      = obj[QLatin1String("role")].toString();
                            iface.enabled   = obj[QLatin1String("enabled")].toBool(true);
                            iface.mode      = obj[QLatin1String("mode")].toString(QStringLiteral("static"));
                            iface.ipAddress = obj[QLatin1String("ipAddress")].toString();
                            iface.netmask   = obj[QLatin1String("netmask")].toString();
                            iface.gateway   = obj[QLatin1String("gateway")].toString();
                            iface.dns       = obj[QLatin1String("dns")].toString();
                            cfg.networkInterfaces.append(iface);
                        }
                    }
                }

                QString saveErr;
                if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), cfg, saveErr)) {
                    Util::Logger::error(
                        QStringLiteral("Restore config save failed: %1").arg(saveErr));
                }
            }
        }
    }

    //-----------------------------------------------------------//
    // remove temp files after successful restore
    //-----------------------------------------------------------//
    QDir(dir).removeRecursively();

    Util::Logger::info(QStringLiteral("Restore apply completed: id=%1").arg(restoreId));
    restartRequired = true;
    return true;
}


} // namespace Maintenance
