#include "DeviceDatabase.h"

#include <QStringList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMap>

namespace DataCollection {
namespace Database{

DeviceDatabase::DeviceDatabase() : m_connectionName(QStringLiteral("smartroute_db"))
{
}

DeviceDatabase::~DeviceDatabase()
{
    close();
}

bool DeviceDatabase::open(const QString& dbPath, QString& error)
{
    close();

    QFileInfo fileInfo(dbPath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        error = QStringLiteral("Failed to create database directory: %1")
        .arg(fileInfo.absolutePath());
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);

    if(!db.open()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        error = q.lastError().text();
        return false;
    }

    //-----------------------------------------------------------//
    // Initialize the database schema if it doesn't exist
    // No problem if the tables already exist,
    // as CREATE TABLE IF NOT EXISTS will not overwrite existing tables.
    //-----------------------------------------------------------//
    return initSchema(error);
}

void DeviceDatabase::close()
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        return;
    }

    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        db.close();
    }

    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DeviceDatabase::isOpen() const
{
    if (!QSqlDatabase::contains(m_connectionName)) {
        return false;
    }

    return QSqlDatabase::database(m_connectionName).isOpen();
}

bool DeviceDatabase::resetSchema(QString& error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    //--------------------------------------------------------------------------------------------//
    // Disable foreign key constraints temporarily during reset to avoid issues with drop order
    // Block scope to ensure PRAGMA is executed before and after the transaction
    // Do not remove Block scope, as it ensures that the PRAGMA is executed before and
    // after the transaction
    //--------------------------------------------------------------------------------------------//
    {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA foreign_keys = OFF"))) {
            error = q.lastError().text();
            return false;
        }
    }

    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);

    //----------------------------------------------------//
    // remove child table first (registers -> devices)
    //----------------------------------------------------//
    const QStringList dropStatements = {
        QStringLiteral("DROP TABLE IF EXISTS registers"),
        QStringLiteral("DROP TABLE IF EXISTS devices"),
        QStringLiteral("DROP TABLE IF EXISTS users"),
        QStringLiteral("DROP TABLE IF EXISTS login_history"),

        QStringLiteral("DROP TABLE IF EXISTS trends"),
        QStringLiteral("DROP TABLE IF EXISTS hmi_tags"),
        QStringLiteral("DROP TABLE IF EXISTS hmi_screens"),
        QStringLiteral("DROP TABLE IF EXISTS hmi_project"),
        QStringLiteral("DROP TABLE IF EXISTS sessions")
    };

    for (const QString& sql : dropStatements) {
        if (!q.exec(sql)) {
            error = q.lastError().text();
            db.rollback();

            QSqlQuery restorePragma(db);
            restorePragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

            return false;
        }
    }

    //----------------------------------------------------//
    // Initicialize AUTOINCREMENT sequence
    //----------------------------------------------------//
    q.exec(QStringLiteral(
        "DELETE FROM sqlite_sequence "
        "WHERE name IN ('users', 'devices', 'registers', 'trends', 'sessions')"
        ));

    if (!db.commit()) {
        error = db.lastError().text();

        QSqlQuery restorePragma(db);
        restorePragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));

        return false;
    }

    {
        QSqlQuery q2(db);
        if (!q2.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
            error = q2.lastError().text();
            return false;
        }
    }

    //----------------------------------------------------//
    // After remove All Table and Create Again by Init Schema.
    //----------------------------------------------------//
    return initSchema(error);
}


bool DeviceDatabase::initSchema(QString& error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    const QStringList statements = {
        //-----------------------------------------------------------//
        // Create user tables if they do not exist 
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username TEXT NOT NULL UNIQUE,"
            "  display_name TEXT NOT NULL,"
            "  description TEXT,"
            "  password_hash TEXT NOT NULL,"
            "  role TEXT NOT NULL CHECK(role IN ('user', 'manager', 'admin')),"
            "  status TEXT NOT NULL DEFAULT 'active' CHECK(status IN ('active', 'disabled', 'locked')),"
            "  failed_login_count INTEGER NOT NULL DEFAULT 0,"
            "  last_login_at TEXT,"
            "  last_login_ip TEXT,"
            "  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")"
        ),

        //-----------------------------------------------------------//
        // Create device tables if they do not exist
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS devices ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  device_code TEXT UNIQUE,"
            "  name TEXT NOT NULL,"
            "  display_name TEXT,"
            "  conn_type TEXT NOT NULL CHECK(conn_type IN ('serial', 'tcp')),"
            "  ip_address TEXT,"
            "  tcp_port INTEGER DEFAULT 502,"
            "  slave_id INTEGER DEFAULT 1,"
            "  timeout_ms INTEGER DEFAULT 5000,"
            "  interval_ms INTEGER DEFAULT 1000,"
            "  retry_count INTEGER DEFAULT 3,"
            "  byte_order TEXT DEFAULT 'big',"
            "  protocol TEXT NOT NULL CHECK(protocol IN ("
            "  'modbus_rtu',"
            "  'modbus_tcp',"
            "  'modbus_ascii',"
            "  'pclink',"
            "  'pclink_sum'))"
            ")"
        ),

        //-----------------------------------------------------------//
        // Create register tables if they do not exist
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS registers ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  device_id INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,"
            "  name TEXT NOT NULL,"
            "  address INTEGER NOT NULL,"
            "  type TEXT NOT NULL CHECK(type IN "
            "    ('coil', 'discrete_input', 'holding_register', 'input_register',"
            "     'word_register', 'bit_register')),"
            "  read_only INTEGER DEFAULT 1,"
            "  length INTEGER DEFAULT 1,"
            "  unified_register_id INTEGER DEFAULT -1,"
            "  display_name TEXT,"
            "  unit TEXT,"
            "  scale REAL DEFAULT 1.0,"
            "  is_signed INTEGER DEFAULT 0,"
            "  min_value REAL,"
            "  max_value REAL,"
            "  byte_order TEXT DEFAULT 'default',"
            "  bit_labels TEXT DEFAULT ''"
            ")"
            ),

        //-----------------------------------------------------------//
        // Create unique index on unified_register_id for registers table
        // If unified_register_id is negative, it is considered unassigned and not included in the index.
        // When insert or update with same unified_register_id, 
        // it will be rejected by the unique index constraint.
        //
        // if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
        //   err["error"] = QStringLiteral("unifiedAddress already in use");
        // }
        //
        // WHERE unified_register_id >= 0" statement ensures that only non-negative 
        // unified_register_id values are considered for uniqueness.
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_registers_unified_id "
            "ON registers(unified_register_id) WHERE unified_register_id >= 0"
        ),

        //-----------------------------------------------------------//
        // Create login history tables if they do not exist
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS login_history ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "  username  TEXT NOT NULL,"
            "  action    TEXT NOT NULL CHECK(action IN ('login', 'logout')),"
            "  result    TEXT NOT NULL CHECK(result IN ('success', 'invalid_password', 'account_locked', 'account_disabled')),"
            "  ip        TEXT"
            ")"
        ),

        //-----------------------------------------------------------//
        // Create index on login_history.username for faster lookups
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_login_history_username "
            "ON login_history(username)"
        ),

        //-----------------------------------------------------------//
        // HMI Tags
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hmi_tags ("
            "  id          TEXT    PRIMARY KEY,"
            "  name        TEXT    NOT NULL,"
            "  register_id INTEGER NOT NULL REFERENCES registers(id) ON DELETE CASCADE,"
            "  unit        TEXT,"
            "  min_value   REAL,"
            "  max_value   REAL,"
            "  decimals    INTEGER,"
            "  access      TEXT    CHECK(access IS NULL OR access IN ('r', 'rw')),"
            "  created_at  TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "  updated_at  TEXT    NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")"
        ),

        //-----------------------------------------------------------//
        // HMI Project — single row (id is pinned to 1)
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hmi_project ("
            "  id           INTEGER PRIMARY KEY CHECK(id = 1),"
            "  name         TEXT    NOT NULL DEFAULT '',"
            "  start_screen TEXT,"
            "  revision     INTEGER NOT NULL DEFAULT 0"
            ")"
        ),

        //-----------------------------------------------------------//
        // HMI Screens
        // content is opaque JSON — the backend never interprets it
        //-----------------------------------------------------------//
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS hmi_screens ("
            "  id         TEXT    PRIMARY KEY,"
            "  title      TEXT    NOT NULL,"
            "  content    TEXT    NOT NULL,"
            "  revision   INTEGER NOT NULL DEFAULT 1,"
            "  sort_order INTEGER NOT NULL DEFAULT 0,"
            "  updated_at TEXT    NOT NULL,"
            "  updated_by TEXT    NOT NULL DEFAULT ''"
            ")"
        ),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_hmi_screens_order "
            "ON hmi_screens(sort_order)"
        ),

    };

    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);

    for (const QString &sql : statements) {
        if (!q.exec(sql)) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        error = db.lastError().text();
        db.rollback();
        return false;
    }

    //-----------------------------------------------------------//
    // Schema migrations — ADD COLUMN to existing tables
    // SQLite does not support IF NOT EXISTS in ALTER TABLE;
    // ignore "duplicate column" error (error code 1) silently.
    //-----------------------------------------------------------//
    {
        QSqlQuery mq(db);
        mq.exec(QStringLiteral(
            "ALTER TABLE devices ADD COLUMN poll_mode TEXT NOT NULL DEFAULT 'auto' "
            "CHECK(poll_mode IN ('auto', 'manual'))"));
        // ignore error — column may already exist

        mq.exec(QStringLiteral(
            "ALTER TABLE devices ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1"));
        // ignore error — column may already exist
    }

    //-----------------------------------------------------------//
    // ensure default admin account exists
    // set password to "1234" if not exists and id is 0
    //-----------------------------------------------------------//
    QSqlQuery chk(db);
    chk.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE username = 'admin'"));

    if (!chk.exec() || !chk.next()) {
        error = chk.lastError().text();
        return false;
    }

    if (chk.value(0).toInt() == 0) {
        const QString hash = QString::fromLatin1(
                                    QCryptographicHash::hash(QByteArrayLiteral("1234"),
                                    QCryptographicHash::Sha256).toHex());
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO users "
            "(id, username, display_name, description, password_hash, role, status) "
            "VALUES (0, 'admin', 'Administrator', 'Default administrator account', ?, 'admin', 'active')"
        ));

        ins.addBindValue(hash);
        if (!ins.exec()) {
            error = ins.lastError().text();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------
QList<Model::DeviceInfo> DeviceDatabase::loadDevices(QString &error) const
{
    QList<Model::DeviceInfo> devices;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return devices;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "SELECT id, device_code, name, display_name, conn_type, ip_address, tcp_port, slave_id, timeout_ms, "
            "interval_ms, retry_count, byte_order, protocol, poll_mode, enabled "
            "FROM devices "
            "ORDER BY name"))) {
        error = q.lastError().text();
        return devices;
    }

    while (q.next()) {
        Model::DeviceInfo device;
        Model::DeviceConnection &conn = device.connection;
        Model::PollingConfig &polling = device.polling;

        device.id               = q.value(0).toInt();
        device.deviceCode       = q.value(1).toString();
        device.name             = q.value(2).toString();
        device.displayName      = q.value(3).toString();

        conn.type               = Model::connectionTypeFromString(q.value(4).toString().toLower());
        conn.ipAddress          = q.value(5).toString();
        conn.tcpPort            = q.value(6).toInt();
        conn.slaveId            = q.value(7).toInt();
        conn.timeoutMs          = q.value(8).toInt();

        polling.intervalMs      = q.value(9).toInt();
        polling.retryCount      = q.value(10).toInt();
        conn.defaultByteOrder   = Model::byteOrderFromString(q.value(11).toString().toLower());
        conn.protocol           = Model::protocolFromString(q.value(12).toString().toLower());
        polling.pollMode        = Model::pollModeFromString(q.value(13).toString());
        device.enabled          = q.value(14).toInt() != 0;

        QString regError;
        device.registers = loadRegisters(device.id, regError);

        if(!regError.isEmpty()){
            error = QStringLiteral("Failed to load registers for device %1: %2")
            .arg(device.id).arg(regError);

            return {};
        }
        devices.append(device);
    }

    return devices;
}

bool DeviceDatabase::insertDevice(const Model::DeviceInfo &device, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO devices ("
        "device_code, name, display_name, conn_type, ip_address, tcp_port, "
        "slave_id, timeout_ms, interval_ms, retry_count, byte_order, protocol, poll_mode, enabled"
        ") VALUES ("
        ":device_code, :name, :display_name, :conn_type, :ip_address, :tcp_port, "
        ":slave_id, :timeout_ms, :interval_ms, :retry_count, :byte_order, :protocol, :poll_mode, :enabled"
        ")"
        ));

    q.bindValue(":device_code",  device.deviceCode);
    q.bindValue(":name",         device.name);
    q.bindValue(":display_name", device.displayName);
    q.bindValue(":conn_type",    Model::connectionTypeToString(device.connection.type));
    q.bindValue(":ip_address",   device.connection.ipAddress);
    q.bindValue(":tcp_port",     device.connection.tcpPort);
    q.bindValue(":slave_id",     device.connection.slaveId);
    q.bindValue(":timeout_ms",   device.connection.timeoutMs);
    q.bindValue(":interval_ms",  device.polling.intervalMs);
    q.bindValue(":retry_count",  device.polling.retryCount);
    q.bindValue(":byte_order",   Model::byteOrderToString(device.connection.defaultByteOrder));
    q.bindValue(":protocol",     Model::protocolToString(device.connection.protocol));
    q.bindValue(":poll_mode",    Model::pollModeToString(device.polling.pollMode));
    q.bindValue(":enabled",      device.enabled ? 1 : 0);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    return true;
}

bool DeviceDatabase::updateDevice(const Model::DeviceInfo &device, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "UPDATE devices SET "
        "device_code=:device_code, "
        "name=:name, "
        "display_name=:display_name, "
        "conn_type=:conn_type, "
        "ip_address=:ip_address, "
        "tcp_port=:tcp_port, "
        "slave_id=:slave_id, "
        "timeout_ms=:timeout_ms, "
        "interval_ms=:interval_ms, "
        "retry_count=:retry_count, "
        "byte_order=:byte_order, "
        "protocol=:protocol, "
        "poll_mode=:poll_mode, "
        "enabled=:enabled "
        "WHERE id=:id"
        ));

    q.bindValue(":id",           device.id);
    q.bindValue(":device_code",  device.deviceCode);
    q.bindValue(":name",         device.name);
    q.bindValue(":display_name", device.displayName);
    q.bindValue(":conn_type",    Model::connectionTypeToString(device.connection.type));
    q.bindValue(":ip_address",   device.connection.ipAddress);
    q.bindValue(":tcp_port",     device.connection.tcpPort);
    q.bindValue(":slave_id",     device.connection.slaveId);
    q.bindValue(":timeout_ms",   device.connection.timeoutMs);
    q.bindValue(":interval_ms",  device.polling.intervalMs);
    q.bindValue(":retry_count",  device.polling.retryCount);
    q.bindValue(":byte_order",   Model::byteOrderToString(device.connection.defaultByteOrder));
    q.bindValue(":protocol",     Model::protocolToString(device.connection.protocol));
    q.bindValue(":poll_mode",    Model::pollModeToString(device.polling.pollMode));
    q.bindValue(":enabled",      device.enabled ? 1 : 0);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    return true;
}

bool DeviceDatabase::setDevicesEnabled(const QList<QPair<int, bool>> &changes, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    for (const auto &c : changes) {
        q.prepare(QStringLiteral("UPDATE devices SET enabled=? WHERE id=?"));
        q.addBindValue(c.second ? 1 : 0);
        q.addBindValue(c.first);
        if (!q.exec()) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
        if (q.numRowsAffected() == 0) {
            error = QStringLiteral("Device not found: %1").arg(c.first);
            db.rollback();
            return false;
        }
    }

    if (!db.commit()) {
        error = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool DeviceDatabase::deleteDevice(int deviceId, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral("DELETE FROM devices WHERE id=:id"));
    q.bindValue(":id", deviceId);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Register
// ---------------------------------------------------------------------------
QList<Model::RegisterConfig> DeviceDatabase::loadRegisters(int deviceId, QString &error) const
{
    QList<Model::RegisterConfig> fields;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return fields;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "SELECT id, name, address, type, read_only, length, unified_register_id, "
        "display_name, unit, scale, is_signed, min_value, max_value, byte_order, bit_labels "
        "FROM registers "
        "WHERE device_id=:device_id "
        "ORDER BY unified_register_id, address"));

    q.bindValue(":device_id", deviceId);

    if (!q.exec()) {
        error = q.lastError().text();
        return fields;
    }

    while (q.next()) {
        Model::RegisterConfig config;

        config.id                = q.value(0).toInt();
        config.deviceId          = deviceId;
        config.tagName           = q.value(1).toString();
        config.localAddress  = q.value(2).toInt();   // DB column: address
        config.type          = Model::registerTypeFromString(q.value(3).toString());
        config.readOnly      = q.value(4).toBool();
        config.length        = q.value(5).toInt();
        config.unifiedAddress = q.value(6).toInt();  // DB column: unified_register_id
        config.displayName       = q.value(7).toString();
        config.unit              = q.value(8).toString();
        config.scale             = q.value(9).toDouble();
        config.isSigned          = q.value(10).toInt() != 0;

        if (!q.value(11).isNull()){
            config.minValue = q.value(11).toDouble();
        }

        if (!q.value(12).isNull()){
            config.maxValue = q.value(12).toDouble();
        }

        config.byteOrder = Model::byteOrderFromString(q.value(13).toString().toLower());
        config.bitLabels = q.value(14).toString();

        fields.append(config);
    }

    return fields;
}

bool DeviceDatabase::insertRegister(int deviceId, Model::RegisterConfig &config, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO registers (device_id, name, address, type, read_only, length, "
        "unified_register_id, display_name, unit, scale, is_signed, min_value, max_value, byte_order, bit_labels) "
        "VALUES (:device_id, :name, :address, :type, :read_only, :length, "
        ":unified_register_id, :display_name, :unit, :scale, :is_signed, :min_value, :max_value, :byte_order, :bit_labels)"));

    q.bindValue(":device_id",           deviceId);
    q.bindValue(":name",                config.tagName);
    q.bindValue(":address",             config.localAddress);        // TODO: rename DB column address -> local_address
    q.bindValue(":type",                Model::registerTypeToString(config.type));
    q.bindValue(":read_only",           config.readOnly ? 1 : 0);
    q.bindValue(":length",              config.length);
    q.bindValue(":unified_register_id", config.unifiedAddress);      // TODO: rename DB column unified_register_id -> unified_address
    q.bindValue(":display_name",        config.displayName);
    q.bindValue(":unit",                config.unit);
    q.bindValue(":scale",               config.scale);
    q.bindValue(":is_signed",           config.isSigned ? 1 : 0);
    q.bindValue(":min_value",           config.minValue);
    q.bindValue(":max_value",           config.maxValue);
    q.bindValue(":byte_order",          Model::byteOrderToString(config.byteOrder));
    q.bindValue(":bit_labels",          config.bitLabels);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    config.id       = q.lastInsertId().toInt();
    config.deviceId = deviceId;

    //---------------------------------------------------------//
    // Auto-assign unifiedAddress if not specified (< 0).
    // Auto range: 1 ~ kAutoUnifiedIdMax (kManualUnifiedIdMin and above are reserved for manual assignment).
    // The row was inserted with uid=-1 first; the actual ID is assigned via a
    // separate UPDATE so we can reference the newly inserted row by its PK.
    //---------------------------------------------------------//
    if (config.unifiedAddress < 0) {
        QSqlQuery q2(db);

        //---------------------------------------------------------//
        // Find the next available ID within the auto-assign range.
        // Returns 1 if no rows exist yet (COALESCE(NULL, 0) + 1).
        //---------------------------------------------------------//
        q2.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(unified_register_id), 0) + 1 "
            "FROM registers WHERE unified_register_id >= 0 AND unified_register_id < :max"));
        q2.bindValue(":max", Model::kManualUnifiedIdMin);

        if (!q2.exec() || !q2.next()) {
            error = q2.lastError().text();
            return false;
        }
        const int newUnifiedId = q2.value(0).toInt();

        //---------------------------------------------------------//
        // Auto-assign range exhausted if MAX+1 reaches the manual range.
        //---------------------------------------------------------//
        if (newUnifiedId >= Model::kManualUnifiedIdMin) {
            error = QStringLiteral("Auto-assign range exhausted (max %1)").arg(Model::kAutoUnifiedIdMax);
            return false;
        }

        QSqlQuery q3(db);

        //---------------------------------------------------------//
        // Write the assigned ID back to the row inserted above.
        //---------------------------------------------------------//
        q3.prepare(QStringLiteral(
            "UPDATE registers SET unified_register_id=:uid WHERE id=:id"));

        q3.bindValue(":uid", newUnifiedId);
        q3.bindValue(":id",  config.id);

        if (!q3.exec()) {
            error = q3.lastError().text();
            return false;
        }

        //---------------------------------------------------------//
        // Reflect the assigned ID in the caller's config object.
        //---------------------------------------------------------//
        config.unifiedAddress = newUnifiedId;
    }

    return true;
}

bool DeviceDatabase::updateRegister(Model::RegisterConfig &config, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (config.unifiedAddress < 0) {
        QSqlQuery q2(db);
        q2.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(unified_register_id), 0) + 1 "
            "FROM registers WHERE unified_register_id >= 0 AND unified_register_id < :max"));
        q2.bindValue(":max", Model::kManualUnifiedIdMin);
        if (!q2.exec() || !q2.next()) {
            error = q2.lastError().text();
            return false;
        }
        const int newUnifiedId = q2.value(0).toInt();
        if (newUnifiedId >= Model::kManualUnifiedIdMin) {
            error = QStringLiteral("Auto-assign range exhausted (max %1)").arg(Model::kAutoUnifiedIdMax);
            return false;
        }
        config.unifiedAddress = newUnifiedId;
    }

    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "UPDATE registers SET name=:name, address=:address, type=:type, read_only=:read_only, length=:length, "
        "unified_register_id=:unified_register_id, display_name=:display_name, unit=:unit, "
        "scale=:scale, is_signed=:is_signed, min_value=:min_value, max_value=:max_value, "
        "byte_order=:byte_order, bit_labels=:bit_labels "
        "WHERE id=:id"));

    q.bindValue(":id",                  config.id);
    q.bindValue(":name",                config.tagName);
    q.bindValue(":address",             config.localAddress);        // TODO: rename DB column address -> local_address
    q.bindValue(":type",                Model::registerTypeToString(config.type));
    q.bindValue(":read_only",           config.readOnly ? 1 : 0);
    q.bindValue(":length",              config.length);
    q.bindValue(":unified_register_id", config.unifiedAddress);      // TODO: rename DB column unified_register_id -> unified_address
    q.bindValue(":display_name",        config.displayName);
    q.bindValue(":unit",                config.unit);
    q.bindValue(":scale",               config.scale);
    q.bindValue(":is_signed",           config.isSigned ? 1 : 0);
    q.bindValue(":min_value",           config.minValue);
    q.bindValue(":max_value",           config.maxValue);
    q.bindValue(":byte_order",          Model::byteOrderToString(config.byteOrder));
    q.bindValue(":bit_labels",          config.bitLabels);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

bool DeviceDatabase::deleteRegister(int registerId, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "DELETE FROM registers WHERE id=:id"));
    q.bindValue(":id", registerId);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// User
// ---------------------------------------------------------------------------
QList<Model::UserInfo> DeviceDatabase::loadUsers(QString &error) const
{
    QList<Model::UserInfo> users;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return users;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (!q.exec(QStringLiteral(
            "SELECT id, username, display_name, description, password_hash, "
            "role, status, failed_login_count, "
            "last_login_at, last_login_ip, created_at, updated_at "
            "FROM users ORDER BY id"))) {
        error = q.lastError().text();
        return users;
    }

    while (q.next()) {
        Model::UserInfo user;
        user.id               = q.value(0).toInt();
        user.username         = q.value(1).toString();
        user.displayName      = q.value(2).toString();
        user.description      = q.value(3).toString();
        user.passwordHash     = q.value(4).toString();
        user.role             = Model::userRoleFromString(q.value(5).toString());
        user.status           = Model::userStatusFromString(q.value(6).toString());
        user.failedLoginCount = q.value(7).toInt();
        user.lastLoginAt      = q.value(8).toString();
        user.lastLoginIp      = q.value(9).toString();
        user.createdAt        = q.value(10).toString();
        user.updatedAt        = q.value(11).toString();
        users.append(user);
    }

    return users;
}

Model::UserInfo DeviceDatabase::loadUser(const QString &username, bool &found, QString &error) const
{
    found = false;
    Model::UserInfo user;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return user;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, description, password_hash, "
        "role, status, failed_login_count, "
        "last_login_at, last_login_ip, created_at, updated_at "
        "FROM users WHERE username=:username"));
    q.bindValue(":username", username);

    if (!q.exec()) {
        error = q.lastError().text();
        return user;
    }

    if (!q.next())
        return user;

    found             = true;
    user.id               = q.value(0).toInt();
    user.username         = q.value(1).toString();
    user.displayName      = q.value(2).toString();
    user.description      = q.value(3).toString();
    user.passwordHash     = q.value(4).toString();
    user.role             = Model::userRoleFromString(q.value(5).toString());
    user.status           = Model::userStatusFromString(q.value(6).toString());
    user.failedLoginCount = q.value(7).toInt();
    user.lastLoginAt      = q.value(8).toString();
    user.lastLoginIp      = q.value(9).toString();
    user.createdAt        = q.value(10).toString();
    user.updatedAt        = q.value(11).toString();

    return user;
}

bool DeviceDatabase::insertUser(const Model::UserInfo &user, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO users "
        "(username, display_name, description, password_hash, role, status) "
        "VALUES "
        "(:username, :display_name, :description, :password_hash, :role, :status)"
        ));

    q.bindValue(":username",     user.username);
    q.bindValue(":display_name", user.displayName);
    q.bindValue(":description",  user.description);
    q.bindValue(":password_hash",user.passwordHash);
    q.bindValue(":role",         Model::userRoleToString(user.role));
    q.bindValue(":status",       Model::userStatusToString(user.status));

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    return true;
}

bool DeviceDatabase::updateUser(const Model::UserInfo &user, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "UPDATE users SET "
        "display_name=:display_name, "
        "description=:description, "
        "password_hash=:password_hash, "
        "role=:role, "
        "status=:status, "
        "failed_login_count=:failed_login_count, "
        "updated_at=CURRENT_TIMESTAMP "
        "WHERE username=:username"
        ));

    q.bindValue(":username",           user.username);
    q.bindValue(":display_name",       user.displayName);
    q.bindValue(":description",        user.description);
    q.bindValue(":password_hash",      user.passwordHash);
    q.bindValue(":role",               Model::userRoleToString(user.role));
    q.bindValue(":status",             Model::userStatusToString(user.status));
    q.bindValue(":failed_login_count", user.failedLoginCount);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    return true;
}

bool DeviceDatabase::deleteUser(const QString &username, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral("DELETE FROM users WHERE username=:username"));
    q.bindValue(":username", username);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

Model::LoginResult DeviceDatabase::validateUser(const QString &username,
                                                const QString &password,
                                                const QString &ip,
                                                int maxFailedAttempts,
                                                QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return Model::LoginResult::InvalidCredentials;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 1. Search User Info form DB
    q.prepare(QStringLiteral(
        "SELECT id, password_hash, status, failed_login_count "
        "FROM users WHERE username=:username"));
    q.bindValue(":username", username);

    if (!q.exec()) {
        error = q.lastError().text();
        return Model::LoginResult::InvalidCredentials;
    }

    // Not exist user.
    if (!q.next())
        return Model::LoginResult::InvalidCredentials;

    const int userId         = q.value(0).toInt();
    const QString storedHash = q.value(1).toString();
    Model::UserStatus status = Model::userStatusFromString(q.value(2).toString());
    int failedCount          = q.value(3).toInt();
    const bool isDefaultAdmin = (userId == 0);

    // 2. Is Disabled User?
    if (status == Model::UserStatus::Disabled) {
        error = QStringLiteral("Account is disabled.");
        return Model::LoginResult::AccountDisabled;
    }

    // 3. Is Locked User?
    if (status == Model::UserStatus::Locked) {
        error = QStringLiteral("Account is locked. Contact administrator.");
        return Model::LoginResult::AccountLocked;
    }

    // 4. Verify password
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());

    // Not match for password.
    if (hash != storedHash) {
        if (!isDefaultAdmin) {
            const int newCount = failedCount + 1;

            if (newCount >= maxFailedAttempts) {
                QSqlQuery lockQ(db);
                lockQ.prepare(QStringLiteral(
                    "UPDATE users SET status='locked', failed_login_count=:count "
                    "WHERE username=:username"));
                lockQ.bindValue(":count",    newCount);
                lockQ.bindValue(":username", username);
                lockQ.exec();
                error = QStringLiteral("Account locked due to too many failed attempts. Contact administrator.");
                return Model::LoginResult::AccountJustLocked;
            }

            QSqlQuery failQ(db);
            failQ.prepare(QStringLiteral(
                "UPDATE users SET failed_login_count=:count WHERE username=:username"));
            failQ.bindValue(":count",    newCount);
            failQ.bindValue(":username", username);
            failQ.exec();
        }

        return Model::LoginResult::InvalidCredentials;
    }

    // 5. Success , Reset failed login count, last login data & ip
    QSqlQuery successQ(db);
    successQ.prepare(QStringLiteral(
        "UPDATE users SET "
        "failed_login_count=0, "
        "last_login_at=CURRENT_TIMESTAMP, last_login_ip=:ip "
        "WHERE username=:username"));

    successQ.bindValue(":ip",       ip.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : ip);
    successQ.bindValue(":username", username);
    successQ.exec();

    return Model::LoginResult::Success;
}

// ---------------------------------------------------------------------------
// Login History
// ---------------------------------------------------------------------------
bool DeviceDatabase::insertLoginHistory(const Model::LoginHistoryEntry &entry, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO login_history (username, action, result, ip) "
        "VALUES (:username, :action, :result, :ip)"));

    q.bindValue(":username", entry.username);
    q.bindValue(":action",   entry.action);
    q.bindValue(":result",   entry.result);
    q.bindValue(":ip",       entry.ip.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : entry.ip);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

QList<Model::LoginHistoryEntry> DeviceDatabase::fetchLoginHistory(int limit,
                                                                   const QString &username,
                                                                   QString &error) const
{
    QList<Model::LoginHistoryEntry> result;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return result;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (username.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT id, timestamp, username, action, result, ip "
            "FROM login_history ORDER BY id DESC LIMIT :limit"));
    } else {
        q.prepare(QStringLiteral(
            "SELECT id, timestamp, username, action, result, ip "
            "FROM login_history WHERE username=:username ORDER BY id DESC LIMIT :limit"));
        q.bindValue(":username", username);
    }
    q.bindValue(":limit", limit);

    if (!q.exec()) {
        error = q.lastError().text();
        return result;
    }

    while (q.next()) {
        Model::LoginHistoryEntry entry;
        entry.id        = q.value(0).toLongLong();
        entry.timestamp = q.value(1).toString();
        entry.username  = q.value(2).toString();
        entry.action    = q.value(3).toString();
        entry.result    = q.value(4).toString();
        entry.ip        = q.value(5).toString();
        result.append(entry);
    }

    return result;
}

bool DeviceDatabase::deleteLoginHistory(const QString &username, QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    if (username.isEmpty()) {
        if (!q.exec(QStringLiteral("DELETE FROM login_history"))) {
            error = q.lastError().text();
            return false;
        }
    } else {
        q.prepare(QStringLiteral("DELETE FROM login_history WHERE username=:username"));
        q.bindValue(":username", username);
        if (!q.exec()) {
            error = q.lastError().text();
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Restore Data
// User can choose to restore devices, registers, and users from JSON arrays.
// ---------------------------------------------------------------------------
bool DeviceDatabase::restoreData(bool restoreDevices,
                                 const QJsonArray &devices,
                                 const QJsonArray &registers,
                                 bool restoreUsers,
                                 const QJsonArray &users,
                                 bool restoreHmi,
                                 const QJsonObject &hmi,
                                 QStringList &hmiWarnings,
                                 QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);

    //-----------------------------------------------------------//
    // Deleting devices cascades to registers and then to hmi_tags.
    // When HMI is not being restored from the backup, snapshot the
    // current tags keyed by unifiedAddress so they can be re-linked
    // to the re-created registers (which receive new ids).
    //-----------------------------------------------------------//
    struct TagSnapshot {
        QString  id;
        QString  name;
        int      unifiedAddress = -1;
        QVariant unit, minValue, maxValue, decimals, access, createdAt, updatedAt;
    };
    QList<TagSnapshot> keptTags;
    const bool relinkTags = restoreDevices && !restoreHmi;

    if (relinkTags) {
        if (!q.exec(QStringLiteral(
                "SELECT t.id, t.name, r.unified_register_id, t.unit, t.min_value, "
                "t.max_value, t.decimals, t.access, t.created_at, t.updated_at "
                "FROM hmi_tags t JOIN registers r ON r.id = t.register_id"))) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
        while (q.next()) {
            TagSnapshot s;
            s.id             = q.value(0).toString();
            s.name           = q.value(1).toString();
            s.unifiedAddress = q.value(2).toInt();
            s.unit           = q.value(3);
            s.minValue       = q.value(4);
            s.maxValue       = q.value(5);
            s.decimals       = q.value(6);
            s.access         = q.value(7);
            s.createdAt      = q.value(8);
            s.updatedAt      = q.value(9);
            keptTags.append(s);
        }
    }

    if (restoreDevices) {
        //-----------------------------------------------------------//
        // Remove all devices and their associated registers
        //-----------------------------------------------------------//
        if (!q.exec(QStringLiteral("DELETE FROM devices"))) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }

        //-----------------------------------------------------------//
        // assuming that the "id" field in the JSON represents the old ID, 
        // we will map it to the new ID generated by the database 
        // upon insertion.
        //-----------------------------------------------------------//
        QMap<int, int> idMap;

        for (const QJsonValue &v : devices) {
            const QJsonObject obj  = v.toObject();
            const int oldId        = obj[QLatin1String("id")].toInt();
            const QJsonObject conn = obj[QLatin1String("connection")].toObject();
            const QJsonObject poll = obj[QLatin1String("polling")].toObject();

            q.prepare(QStringLiteral(
                "INSERT INTO devices "
                "(device_code, name, display_name, conn_type, ip_address, tcp_port, "
                "slave_id, timeout_ms, interval_ms, retry_count, byte_order, protocol, poll_mode, enabled) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

            q.addBindValue(obj[QLatin1String("deviceCode")].toString());
            q.addBindValue(obj[QLatin1String("name")].toString());
            q.addBindValue(obj[QLatin1String("displayName")].toString());
            q.addBindValue(conn[QLatin1String("type")].toString());
            q.addBindValue(conn[QLatin1String("ipAddress")].toString());
            q.addBindValue(conn[QLatin1String("tcpPort")].toInt(502));
            q.addBindValue(conn[QLatin1String("slaveId")].toInt(1));
            q.addBindValue(conn[QLatin1String("timeoutMs")].toInt(5000));
            q.addBindValue(poll[QLatin1String("intervalMs")].toInt(1000));
            q.addBindValue(poll[QLatin1String("retryCount")].toInt(3));
            q.addBindValue(conn[QLatin1String("defaultByteOrder")].toString(QStringLiteral("big")));
            q.addBindValue(conn[QLatin1String("protocol")].toString());
            q.addBindValue(poll[QLatin1String("pollMode")].toString(QStringLiteral("auto")));
            q.addBindValue(obj[QLatin1String("enabled")].toBool(true) ? 1 : 0);

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
            idMap[oldId] = q.lastInsertId().toInt();
        }

        //-----------------------------------------------------------//
        // Now insert registers, remapping device IDs using the idMap
        //-----------------------------------------------------------//
        for (const QJsonValue &v : registers) {
            const QJsonObject obj = v.toObject();
            const int newDeviceId = idMap.value(obj[QLatin1String("deviceId")].toInt(), -1);
            if (newDeviceId < 0) continue;

            q.prepare(QStringLiteral(
                "INSERT INTO registers "
                "(device_id, name, address, type, read_only, length, unified_register_id, "
                "display_name, unit, scale, is_signed, min_value, max_value, byte_order, bit_labels) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            q.addBindValue(newDeviceId);
            q.addBindValue(obj[QLatin1String("tagName")].toString());
            q.addBindValue(obj[QLatin1String("localAddress")].toInt());       // TODO: rename DB column address -> local_address
            q.addBindValue(obj[QLatin1String("type")].toString());
            q.addBindValue(obj[QLatin1String("readOnly")].toBool(true) ? 1 : 0);
            q.addBindValue(obj[QLatin1String("length")].toInt(1));
            q.addBindValue(obj[QLatin1String("unifiedAddress")].toInt(-1));   // TODO: rename DB column unified_register_id -> unified_address
            q.addBindValue(obj[QLatin1String("displayName")].toString());
            q.addBindValue(obj[QLatin1String("unit")].toString());
            q.addBindValue(obj[QLatin1String("scale")].toDouble(1.0));
            q.addBindValue(obj[QLatin1String("isSigned")].toBool(false) ? 1 : 0);
            q.addBindValue(obj[QLatin1String("minValue")].toDouble());
            q.addBindValue(obj[QLatin1String("maxValue")].toDouble());
            q.addBindValue(obj[QLatin1String("byteOrder")].toString(QStringLiteral("default")));
            q.addBindValue(obj[QLatin1String("bitLabels")].toString());

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
        }
    }

    //-----------------------------------------------------------//
    // Re-link the snapshotted HMI tags to the re-created registers
    //-----------------------------------------------------------//
    if (relinkTags && !keptTags.isEmpty()) {
        QMap<int, int> unifiedToRegisterId;
        if (!q.exec(QStringLiteral(
                "SELECT unified_register_id, id FROM registers WHERE unified_register_id >= 0"))) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
        while (q.next())
            unifiedToRegisterId[q.value(0).toInt()] = q.value(1).toInt();

        QStringList dropped;
        for (const TagSnapshot &s : keptTags) {
            const int registerId = (s.unifiedAddress >= 0)
                                   ? unifiedToRegisterId.value(s.unifiedAddress, -1) : -1;
            if (registerId < 0) {
                dropped.append(s.id);
                continue;
            }

            q.prepare(QStringLiteral(
                "INSERT INTO hmi_tags "
                "(id, name, register_id, unit, min_value, max_value, decimals, access, "
                "created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            q.addBindValue(s.id);
            q.addBindValue(s.name);
            q.addBindValue(registerId);
            q.addBindValue(s.unit);
            q.addBindValue(s.minValue);
            q.addBindValue(s.maxValue);
            q.addBindValue(s.decimals);
            q.addBindValue(s.access);
            q.addBindValue(s.createdAt);
            q.addBindValue(s.updatedAt);

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
        }

        if (!dropped.isEmpty()) {
            hmiWarnings << QStringLiteral(
                "%1 existing HMI tag(s) removed — their register is not in the backup: %2")
                .arg(dropped.size()).arg(dropped.join(QStringLiteral(", ")));
        }
    }

    //-----------------------------------------------------------//
    // Restore Users
    //-----------------------------------------------------------//
    if (restoreUsers) {
        for (const QJsonValue &v : users) {
            const QJsonObject obj  = v.toObject();
            const QString username = obj[QLatin1String("username")].toString();
            
            if (username.isEmpty() || username == QLatin1String("admin"))
                continue;

            QSqlQuery chk(db);
            chk.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE username=?"));
            chk.addBindValue(username);
            if (!chk.exec() || !chk.next()) {
                error = chk.lastError().text();
                db.rollback();
                return false;
            }
            if (chk.value(0).toInt() > 0) continue;  // 이미 존재하면 스킵

            q.prepare(QStringLiteral(
                "INSERT INTO users "
                "(username, display_name, description, password_hash, role, status) "
                "VALUES (?, ?, ?, ?, ?, ?)"));
            q.addBindValue(username);
            q.addBindValue(obj[QLatin1String("displayName")].toString());
            q.addBindValue(obj[QLatin1String("description")].toString());
            q.addBindValue(obj[QLatin1String("passwordHash")].toString());
            q.addBindValue(obj[QLatin1String("role")].toString(QStringLiteral("user")));
            q.addBindValue(obj[QLatin1String("status")].toString(QStringLiteral("active")));

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
        }
    }

    //-----------------------------------------------------------//
    // Restore HMI (project + screens + tags)
    //
    // HMI tags reference registers by numeric id, but register ids are
    // reassigned on restore. The backup therefore carries each tag's
    // unifiedAddress, which is globally unique and stable, and we resolve
    // it back to the current register id here.
    //-----------------------------------------------------------//
    if (restoreHmi) {
        QMap<int, int> unifiedToRegisterId;
        {
            QSqlQuery rq(db);
            if (!rq.exec(QStringLiteral(
                    "SELECT unified_register_id, id FROM registers "
                    "WHERE unified_register_id >= 0"))) {
                error = rq.lastError().text();
                db.rollback();
                return false;
            }
            while (rq.next())
                unifiedToRegisterId[rq.value(0).toInt()] = rq.value(1).toInt();
        }

        const QStringList wipe = {
            QStringLiteral("DELETE FROM hmi_tags"),
            QStringLiteral("DELETE FROM hmi_screens"),
            QStringLiteral("DELETE FROM hmi_project")
        };
        for (const QString &sql : wipe) {
            if (!q.exec(sql)) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
        }

        const QJsonObject proj = hmi[QLatin1String("project")].toObject();
        const QJsonArray  scrs = hmi[QLatin1String("screens")].toArray();
        const QJsonArray  tags = hmi[QLatin1String("tags")].toArray();

        //-----------------------------------------------------------//
        // Screens — sort_order follows the backup's array order
        //-----------------------------------------------------------//
        QStringList restoredScreenIds;
        int sortOrder = 0;
        for (const QJsonValue &v : scrs) {
            const QJsonObject obj = v.toObject();
            const QString id      = obj[QLatin1String("id")].toString();
            if (id.isEmpty()) continue;

            q.prepare(QStringLiteral(
                "INSERT INTO hmi_screens "
                "(id, title, content, revision, sort_order, updated_at, updated_by) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)"));
            q.addBindValue(id);
            q.addBindValue(obj[QLatin1String("title")].toString());
            q.addBindValue(QJsonDocument(obj[QLatin1String("content")].toObject())
                               .toJson(QJsonDocument::Compact));
            q.addBindValue(obj[QLatin1String("revision")].toInt(1));
            q.addBindValue(sortOrder++);
            q.addBindValue(obj[QLatin1String("updatedAt")].toString(
                               QDateTime::currentDateTime().toString(Qt::ISODate)));
            q.addBindValue(obj[QLatin1String("updatedBy")].toString());

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
            restoredScreenIds.append(id);
        }

        //-----------------------------------------------------------//
        // Project — startScreen must point at a restored screen
        //-----------------------------------------------------------//
        QString startScreen = proj[QLatin1String("startScreen")].toString();
        if (!startScreen.isEmpty() && !restoredScreenIds.contains(startScreen)) {
            hmiWarnings << QStringLiteral(
                "Backup start screen '%1' is missing; falling back to '%2'")
                .arg(startScreen,
                     restoredScreenIds.isEmpty() ? QStringLiteral("(none)")
                                                 : restoredScreenIds.first());
            startScreen = restoredScreenIds.value(0);
        }
        if (startScreen.isEmpty() && !restoredScreenIds.isEmpty())
            startScreen = restoredScreenIds.first();

        q.prepare(QStringLiteral(
            "INSERT INTO hmi_project (id, name, start_screen, revision) "
            "VALUES (1, ?, ?, ?)"));
        q.addBindValue(proj[QLatin1String("name")].toString());
        q.addBindValue(startScreen.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                             : QVariant(startScreen));
        q.addBindValue(proj[QLatin1String("revision")].toInt(0));
        if (!q.exec()) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }

        //-----------------------------------------------------------//
        // Tags — resolve unifiedAddress back to the current register id
        //-----------------------------------------------------------//
        int droppedTags = 0;
        for (const QJsonValue &v : tags) {
            const QJsonObject obj = v.toObject();
            const QString id      = obj[QLatin1String("id")].toString();
            if (id.isEmpty()) continue;

            const QJsonValue unified = obj[QLatin1String("unifiedAddress")];
            if (!unified.isDouble()) {
                ++droppedTags;
                continue;
            }

            const int registerId = unifiedToRegisterId.value(unified.toInt(-1), -1);
            if (registerId < 0) {
                ++droppedTags;
                continue;
            }

            auto optReal = [&obj](const char *key) -> QVariant {
                const QJsonValue v2 = obj[QLatin1String(key)];
                return v2.isDouble() ? QVariant(v2.toDouble())
                                     : QVariant(QMetaType(QMetaType::Double));
            };

            q.prepare(QStringLiteral(
                "INSERT INTO hmi_tags "
                "(id, name, register_id, unit, min_value, max_value, decimals, access) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
            q.addBindValue(id);
            q.addBindValue(obj[QLatin1String("name")].toString());
            q.addBindValue(registerId);

            const QString unit = obj[QLatin1String("unit")].toString();
            q.addBindValue(unit.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                          : QVariant(unit));
            q.addBindValue(optReal("minValue"));
            q.addBindValue(optReal("maxValue"));

            const QJsonValue dec = obj[QLatin1String("decimals")];
            q.addBindValue(dec.isDouble() ? QVariant(dec.toInt())
                                          : QVariant(QMetaType(QMetaType::Int)));

            const QString access = obj[QLatin1String("access")].toString();
            q.addBindValue(access.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(access));

            if (!q.exec()) {
                error = q.lastError().text();
                db.rollback();
                return false;
            }
        }

        if (droppedTags > 0) {
            hmiWarnings << QStringLiteral(
                "%1 HMI tag(s) skipped — their register is not present on this device")
                .arg(droppedTags);
        }
    }

    if (!db.commit()) {
        error = db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

bool DeviceDatabase::factoryReset(QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);

    //----------------------------------------------------//
    // Delete All Devices
    //----------------------------------------------------//
    if (!q.exec(QStringLiteral("DELETE FROM devices"))) {
        error = q.lastError().text();
        db.rollback();
        return false;
    }

    //----------------------------------------------------//
    // Delete All Users (except admin)
    //----------------------------------------------------//
    if (!q.exec(QStringLiteral("DELETE FROM users WHERE id != 0"))) {
        error = q.lastError().text();
        db.rollback();
        return false;
    }

    //----------------------------------------------------//
    // Delete All HMI data
    // hmi_tags is already cascaded by the device delete above,
    // but screens and the project are independent of devices.
    //----------------------------------------------------//
    const QStringList hmiDeletes = {
        QStringLiteral("DELETE FROM hmi_tags"),
        QStringLiteral("DELETE FROM hmi_screens"),
        QStringLiteral("DELETE FROM hmi_project")
    };
    for (const QString &sql : hmiDeletes) {
        if (!q.exec(sql)) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
    }

    //----------------------------------------------------//
    // Reset Admin Password to Default (1234)
    //----------------------------------------------------//
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayLiteral("1234"),
                                 QCryptographicHash::Sha256).toHex());

    q.prepare(QStringLiteral("UPDATE users SET password_hash=? WHERE id=0"));
    q.addBindValue(hash);
    if (!q.exec()) {
        error = q.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        error = db.lastError().text();
        db.rollback();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// HMI Tags
// ---------------------------------------------------------------------------
static Model::HmiTag hmiTagFromQuery(const QSqlQuery &q)
{
    Model::HmiTag tag;
    tag.id         = q.value(0).toString();
    tag.name       = q.value(1).toString();
    tag.registerId = q.value(2).toInt();

    if (!q.value(3).isNull()) tag.unit        = q.value(3).toString();
    if (!q.value(4).isNull()) { tag.hasMin    = true; tag.minValue  = q.value(4).toDouble(); }
    if (!q.value(5).isNull()) { tag.hasMax    = true; tag.maxValue  = q.value(5).toDouble(); }
    if (!q.value(6).isNull()) { tag.hasDecimals = true; tag.decimals = q.value(6).toInt();   }
    if (!q.value(7).isNull()) tag.access      = q.value(7).toString();

    tag.createdAt  = q.value(8).toString();
    tag.updatedAt  = q.value(9).toString();
    return tag;
}

QList<Model::HmiTag> DeviceDatabase::loadHmiTags(QString &error) const
{
    QList<Model::HmiTag> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id,name,register_id,unit,min_value,max_value,decimals,access,created_at,updated_at "
            "FROM hmi_tags ORDER BY id"))) {
        error = q.lastError().text();
        return list;
    }
    while (q.next())
        list.append(hmiTagFromQuery(q));
    return list;
}

Model::HmiTag DeviceDatabase::loadHmiTag(const QString &id, bool &found, QString &error) const
{
    found = false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id,name,register_id,unit,min_value,max_value,decimals,access,created_at,updated_at "
        "FROM hmi_tags WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return {}; }
    if (!q.next())  return {};
    found = true;
    return hmiTagFromQuery(q);
}

bool DeviceDatabase::insertHmiTag(const Model::HmiTag &tag, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO hmi_tags (id,name,register_id,unit,min_value,max_value,decimals,access) "
        "VALUES (?,?,?,?,?,?,?,?)"));
    q.addBindValue(tag.id);
    q.addBindValue(tag.name);
    q.addBindValue(tag.registerId);
    q.addBindValue(tag.unit.isEmpty()    ? QVariant(QMetaType(QMetaType::QString)) : tag.unit);
    q.addBindValue(tag.hasMin            ? QVariant(tag.minValue)  : QVariant(QMetaType(QMetaType::Double)));
    q.addBindValue(tag.hasMax            ? QVariant(tag.maxValue)  : QVariant(QMetaType(QMetaType::Double)));
    q.addBindValue(tag.hasDecimals       ? QVariant(tag.decimals)  : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(tag.access.isEmpty()  ? QVariant(QMetaType(QMetaType::QString)) : tag.access);
    if (!q.exec()) { error = q.lastError().text(); return false; }
    return true;
}

bool DeviceDatabase::updateHmiTag(const Model::HmiTag &tag, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE hmi_tags SET name=?,register_id=?,unit=?,min_value=?,max_value=?,decimals=?,access=?,"
        "updated_at=CURRENT_TIMESTAMP WHERE id=?"));
    q.addBindValue(tag.name);
    q.addBindValue(tag.registerId);
    q.addBindValue(tag.unit.isEmpty()    ? QVariant(QMetaType(QMetaType::QString)) : tag.unit);
    q.addBindValue(tag.hasMin            ? QVariant(tag.minValue)  : QVariant(QMetaType(QMetaType::Double)));
    q.addBindValue(tag.hasMax            ? QVariant(tag.maxValue)  : QVariant(QMetaType(QMetaType::Double)));
    q.addBindValue(tag.hasDecimals       ? QVariant(tag.decimals)  : QVariant(QMetaType(QMetaType::Int)));
    q.addBindValue(tag.access.isEmpty()  ? QVariant(QMetaType(QMetaType::QString)) : tag.access);
    q.addBindValue(tag.id);
    if (!q.exec()) { error = q.lastError().text(); return false; }
    if (q.numRowsAffected() == 0) { error = QStringLiteral("HMI tag not found: %1").arg(tag.id); return false; }
    return true;
}

bool DeviceDatabase::deleteHmiTag(const QString &id, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM hmi_tags WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return false; }
    if (q.numRowsAffected() == 0) { error = QStringLiteral("HMI tag not found: %1").arg(id); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// HMI Project / Screens
// ---------------------------------------------------------------------------
namespace {

QString hmiNow()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

bool ensureHmiProjectRow(QSqlDatabase &db, QString &error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO hmi_project (id, name, start_screen, revision) "
            "VALUES (1, '', NULL, 0)"))) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

bool readHmiProjectRow(QSqlDatabase &db, Model::HmiProject &project, QString &error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT name, start_screen, revision FROM hmi_project WHERE id=1"))) {
        error = q.lastError().text();
        return false;
    }
    if (!q.next())
        return true;  // caller treats this as the empty default

    project.name        = q.value(0).toString();
    project.startScreen = q.value(1).toString();
    project.revision    = q.value(2).toInt();
    return true;
}

QStringList readHmiScreenIds(QSqlDatabase &db, QString &error)
{
    QStringList ids;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id FROM hmi_screens ORDER BY sort_order, id"))) {
        error = q.lastError().text();
        return ids;
    }
    while (q.next())
        ids.append(q.value(0).toString());
    return ids;
}

bool bumpHmiProjectRevision(QSqlDatabase &db, int &newRevision, QString &error)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "UPDATE hmi_project SET revision = revision + 1 WHERE id=1"))) {
        error = q.lastError().text();
        return false;
    }
    if (!q.exec(QStringLiteral("SELECT revision FROM hmi_project WHERE id=1"))) {
        error = q.lastError().text();
        return false;
    }
    if (q.next())
        newRevision = q.value(0).toInt();
    return true;
}

} // namespace

bool DeviceDatabase::loadHmiProject(Model::HmiProject &project,
                                    QList<Model::HmiScreen> &screens,
                                    QString &error) const
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!readHmiProjectRow(db, project, error))
        return false;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, title, revision, sort_order, updated_at, updated_by "
            "FROM hmi_screens ORDER BY sort_order, id"))) {
        error = q.lastError().text();
        return false;
    }

    while (q.next()) {
        Model::HmiScreen s;
        s.id        = q.value(0).toString();
        s.title     = q.value(1).toString();
        s.revision  = q.value(2).toInt();
        s.sortOrder = q.value(3).toInt();
        s.updatedAt = q.value(4).toString();
        s.updatedBy = q.value(5).toString();
        screens.append(s);
    }
    return true;
}

Model::HmiOpResult DeviceDatabase::updateHmiProject(const Model::HmiProjectUpdate &update,
                                                    Model::HmiProject &result,
                                                    QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return Model::HmiOpResult::Error;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return Model::HmiOpResult::Error;
    }

    auto fail = [&db](Model::HmiOpResult r) {
        db.rollback();
        return r;
    };

    if (!ensureHmiProjectRow(db, error))
        return fail(Model::HmiOpResult::Error);

    Model::HmiProject current;
    if (!readHmiProjectRow(db, current, error))
        return fail(Model::HmiOpResult::Error);

    if (current.revision != update.revision) {
        result = current;
        return fail(Model::HmiOpResult::RevisionMismatch);
    }

    const QStringList ids = readHmiScreenIds(db, error);
    if (!error.isEmpty())
        return fail(Model::HmiOpResult::Error);

    //-----------------------------------------------------------//
    // startScreen may be null only while no screen exists
    //-----------------------------------------------------------//
    if (update.hasStartScreen) {
        if (update.startScreen.isEmpty()) {
            if (!ids.isEmpty())
                return fail(Model::HmiOpResult::InvalidStartScreen);
        } else if (!ids.contains(update.startScreen)) {
            return fail(Model::HmiOpResult::InvalidStartScreen);
        }
    }

    //-----------------------------------------------------------//
    // order must be a permutation of the existing screen ids
    //-----------------------------------------------------------//
    if (update.hasOrder) {
        QStringList sortedGiven = update.order;
        QStringList sortedHave  = ids;
        sortedGiven.sort();
        sortedHave.sort();
        if (sortedGiven != sortedHave)
            return fail(Model::HmiOpResult::InvalidOrder);
    }

    QSqlQuery q(db);

    if (update.hasName) {
        q.prepare(QStringLiteral("UPDATE hmi_project SET name=? WHERE id=1"));
        q.addBindValue(update.name);
        if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    }

    if (update.hasStartScreen) {
        q.prepare(QStringLiteral("UPDATE hmi_project SET start_screen=? WHERE id=1"));
        q.addBindValue(update.startScreen.isEmpty()
                           ? QVariant(QMetaType(QMetaType::QString))
                           : QVariant(update.startScreen));
        if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    }

    if (update.hasOrder) {
        for (int i = 0; i < update.order.size(); ++i) {
            q.prepare(QStringLiteral("UPDATE hmi_screens SET sort_order=? WHERE id=?"));
            q.addBindValue(i);
            q.addBindValue(update.order.at(i));
            if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
        }
    }

    int newRevision = current.revision;
    if (!bumpHmiProjectRevision(db, newRevision, error))
        return fail(Model::HmiOpResult::Error);

    if (!db.commit()) {
        error = db.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }

    result.name        = update.hasName        ? update.name        : current.name;
    result.startScreen = update.hasStartScreen ? update.startScreen : current.startScreen;
    result.revision    = newRevision;
    return Model::HmiOpResult::Ok;
}

Model::HmiScreen DeviceDatabase::loadHmiScreen(const QString &id, bool &found, QString &error) const
{
    found = false;
    Model::HmiScreen screen;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return screen;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, title, content, revision, sort_order, updated_at, updated_by "
        "FROM hmi_screens WHERE id=?"));
    q.addBindValue(id);

    if (!q.exec()) { error = q.lastError().text(); return screen; }
    if (!q.next()) return screen;

    screen.id        = q.value(0).toString();
    screen.title     = q.value(1).toString();
    screen.content   = q.value(2).toString();
    screen.revision  = q.value(3).toInt();
    screen.sortOrder = q.value(4).toInt();
    screen.updatedAt = q.value(5).toString();
    screen.updatedBy = q.value(6).toString();
    found = true;
    return screen;
}

Model::HmiOpResult DeviceDatabase::insertHmiScreen(const Model::HmiScreen &screen,
                                                   int maxScreens,
                                                   Model::HmiProject &project,
                                                   QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return Model::HmiOpResult::Error;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return Model::HmiOpResult::Error;
    }

    auto fail = [&db](Model::HmiOpResult r) {
        db.rollback();
        return r;
    };

    if (!ensureHmiProjectRow(db, error))
        return fail(Model::HmiOpResult::Error);

    Model::HmiProject current;
    if (!readHmiProjectRow(db, current, error))
        return fail(Model::HmiOpResult::Error);

    QSqlQuery q(db);

    if (!q.exec(QStringLiteral("SELECT COUNT(*), COALESCE(MAX(sort_order), -1) FROM hmi_screens"))) {
        error = q.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }
    q.next();
    const int count       = q.value(0).toInt();
    const int maxSortOrder = q.value(1).toInt();

    if (count >= maxScreens)
        return fail(Model::HmiOpResult::TooManyScreens);

    q.prepare(QStringLiteral("SELECT 1 FROM hmi_screens WHERE id=?"));
    q.addBindValue(screen.id);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    if (q.next())
        return fail(Model::HmiOpResult::DuplicateId);

    const QString now = hmiNow();

    q.prepare(QStringLiteral(
        "INSERT INTO hmi_screens (id, title, content, revision, sort_order, updated_at, updated_by) "
        "VALUES (?, ?, ?, 1, ?, ?, ?)"));
    q.addBindValue(screen.id);
    q.addBindValue(screen.title);
    q.addBindValue(screen.content);
    q.addBindValue(maxSortOrder + 1);
    q.addBindValue(now);
    q.addBindValue(screen.updatedBy);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }

    //-----------------------------------------------------------//
    // First screen becomes the start screen
    //-----------------------------------------------------------//
    QString startScreen = current.startScreen;
    if (count == 0) {
        startScreen = screen.id;
        q.prepare(QStringLiteral("UPDATE hmi_project SET start_screen=? WHERE id=1"));
        q.addBindValue(startScreen);
        if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    }

    int newRevision = current.revision;
    if (!bumpHmiProjectRevision(db, newRevision, error))
        return fail(Model::HmiOpResult::Error);

    if (!db.commit()) {
        error = db.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }

    project.name        = current.name;
    project.startScreen = startScreen;
    project.revision    = newRevision;
    return Model::HmiOpResult::Ok;
}

Model::HmiOpResult DeviceDatabase::updateHmiScreen(const QString &id,
                                                   const QString *title,
                                                   const QString &content,
                                                   const QString &updatedBy,
                                                   int revision,
                                                   Model::HmiScreen &current,
                                                   QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return Model::HmiOpResult::Error;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return Model::HmiOpResult::Error;
    }

    auto fail = [&db](Model::HmiOpResult r) {
        db.rollback();
        return r;
    };

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT title, revision, updated_at, updated_by FROM hmi_screens WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    if (!q.next())
        return fail(Model::HmiOpResult::NotFound);

    current.id        = id;
    current.title     = q.value(0).toString();
    current.revision  = q.value(1).toInt();
    current.updatedAt = q.value(2).toString();
    current.updatedBy = q.value(3).toString();

    if (current.revision != revision)
        return fail(Model::HmiOpResult::RevisionMismatch);

    const QString now = hmiNow();

    q.prepare(QStringLiteral(
        "UPDATE hmi_screens SET title=?, content=?, revision=revision+1, "
        "updated_at=?, updated_by=? WHERE id=?"));
    q.addBindValue(title ? *title : current.title);
    q.addBindValue(content);
    q.addBindValue(now);
    q.addBindValue(updatedBy);
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }

    if (!db.commit()) {
        error = db.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }

    if (title) current.title = *title;
    current.revision  = revision + 1;
    current.updatedAt = now;
    current.updatedBy = updatedBy;
    return Model::HmiOpResult::Ok;
}

Model::HmiOpResult DeviceDatabase::deleteHmiScreen(const QString &id,
                                                   Model::HmiProject &project,
                                                   QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return Model::HmiOpResult::Error;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) {
        error = db.lastError().text();
        return Model::HmiOpResult::Error;
    }

    auto fail = [&db](Model::HmiOpResult r) {
        db.rollback();
        return r;
    };

    if (!ensureHmiProjectRow(db, error))
        return fail(Model::HmiOpResult::Error);

    Model::HmiProject current;
    if (!readHmiProjectRow(db, current, error))
        return fail(Model::HmiOpResult::Error);

    QSqlQuery q(db);

    q.prepare(QStringLiteral("SELECT 1 FROM hmi_screens WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }
    if (!q.next())
        return fail(Model::HmiOpResult::NotFound);

    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM hmi_screens"))) {
        error = q.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }
    q.next();
    const int count = q.value(0).toInt();

    //-----------------------------------------------------------//
    // The start screen is protected only while another screen remains,
    // otherwise deleting the last screen would be impossible.
    //-----------------------------------------------------------//
    QString startScreen = current.startScreen;
    if (startScreen == id) {
        if (count > 1)
            return fail(Model::HmiOpResult::StartScreenProtected);

        startScreen.clear();
        if (!q.exec(QStringLiteral("UPDATE hmi_project SET start_screen=NULL WHERE id=1"))) {
            error = q.lastError().text();
            return fail(Model::HmiOpResult::Error);
        }
    }

    q.prepare(QStringLiteral("DELETE FROM hmi_screens WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) { error = q.lastError().text(); return fail(Model::HmiOpResult::Error); }

    int newRevision = current.revision;
    if (!bumpHmiProjectRevision(db, newRevision, error))
        return fail(Model::HmiOpResult::Error);

    if (!db.commit()) {
        error = db.lastError().text();
        return fail(Model::HmiOpResult::Error);
    }

    project.name        = current.name;
    project.startScreen = startScreen;
    project.revision    = newRevision;
    return Model::HmiOpResult::Ok;
}

} // namespace Database
} // namespace DataCollection
