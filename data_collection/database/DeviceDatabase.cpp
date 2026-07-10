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
        QStringLiteral("DROP TABLE IF EXISTS hmi_layouts"),
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
        "WHERE name IN ('users', 'devices', 'registers', 'trends', 'hmi_layouts', 'sessions')"
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

        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_registers_unified_id "
            "ON registers(unified_register_id) WHERE unified_register_id >= 0"
        ),

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

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_login_history_username "
            "ON login_history(username)"
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
            "interval_ms, retry_count, byte_order, protocol "
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
        "slave_id, timeout_ms, interval_ms, retry_count, byte_order, protocol"
        ") VALUES ("
        ":device_code, :name, :display_name, :conn_type, :ip_address, :tcp_port, "
        ":slave_id, :timeout_ms, :interval_ms, :retry_count, :byte_order, :protocol"
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
        "protocol=:protocol "
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

    if (!q.exec()) {
        error = q.lastError().text();
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
                "slave_id, timeout_ms, interval_ms, retry_count, byte_order, protocol) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

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

} // namespace Database
} // namespace DataCollection
