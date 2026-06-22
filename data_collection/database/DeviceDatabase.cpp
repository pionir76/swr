#include "DeviceDatabase.h"

#include <QStringList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QCryptographicHash>

namespace DataCollection {
namespace Database{

DeviceDatabase::DeviceDatabase()
    : m_connectionName(QStringLiteral("smartroute_db"))
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

    return true;
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

    /*
    * SQLite foreign key 제약이 켜져 있으면 DROP 순서에 영향을 받을 수 있으므로
    * reset 중에는 잠시 끈다.
    */
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

    const QStringList dropStatements = {
        /*
         * FK 관계가 있는 테이블은 자식 테이블부터 삭제한다.
         * registers -> devices 순서
         */
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

    /*
     * AUTOINCREMENT 시퀀스 초기화.
     * sqlite_sequence 테이블은 AUTOINCREMENT 테이블이 생성된 후 생긴다.
     * 없을 수도 있으므로 실패해도 치명적으로 보지 않는다.
     */
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

    /*
     * 테이블 삭제 후 현재 버전의 스키마로 다시 생성
     */
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
            "  result    TEXT NOT NULL CHECK(result IN ('success', 'fail')),"
            "  ip        TEXT"
            ")"
        ),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_login_history_username "
            "ON login_history(username)"
        )
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
        device.id   = q.value(0).toInt();

        device.deviceCode  = q.value(1).toString();
        device.name        = q.value(2).toString();
        device.displayName = q.value(3).toString();

        Model::DeviceConnection &conn = device.connection;

        conn.type = Model::connectionTypeFromString(q.value(4).toString().toLower());

        conn.ipAddress  = q.value(5).toString();
        conn.tcpPort    = q.value(6).toInt();
        conn.slaveId    = q.value(7).toInt();
        conn.timeoutMs  = q.value(8).toInt();

        Model::PollingConfig &polling = device.polling;
        polling.intervalMs  = q.value(9).toInt();
        polling.retryCount  = q.value(10).toInt();

        conn.defaultByteOrder = Model::byteOrderFromString(q.value(11).toString().toLower());
        conn.protocol         = Model::protocolFromString(q.value(12).toString().toLower());

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
QList<Model::RegisterField> DeviceDatabase::loadRegisters(int deviceId, QString &error) const
{
    QList<Model::RegisterField> fields;

    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return fields;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "SELECT name, address, type, read_only, length, unified_register_id, "
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
        Model::RegisterField field;
        field.tagName           = q.value(0).toString();
        field.address           = q.value(1).toInt();
        field.type              = Model::registerTypeFromString(q.value(2).toString());
        field.readOnly          = q.value(3).toBool();
        field.length            = q.value(4).toInt();
        field.unifiedRegisterId = q.value(5).toInt();
        field.displayName       = q.value(6).toString();
        field.unit              = q.value(7).toString();
        field.scale             = q.value(8).toDouble();
        field.isSigned          = q.value(9).toInt() != 0;

        if (!q.value(10).isNull()) {
            field.minValue = q.value(10).toDouble();
        }
        if (!q.value(11).isNull()) {
            field.maxValue = q.value(11).toDouble();
        }

        field.byteOrder   = Model::byteOrderFromString(q.value(12).toString().toLower());
        field.bitLabels   = q.value(13).toString();

        fields.append(field);
    }

    return fields;
}

bool DeviceDatabase::insertRegister(int deviceId, const Model::RegisterField &field, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "INSERT INTO registers (device_id, name, address, type, read_only, length, "
        "unified_register_id, display_name, unit, scale, is_signed, min_value, max_value, byte_order, bit_labels) "
        "VALUES (:device_id, :name, :address, :type, :read_only, :length, "
        ":unified_register_id, :display_name, :unit, :scale, :is_signed, :min_value, :max_value, :byte_order, :bit_labels)"));

    q.bindValue(":device_id",           deviceId);
    q.bindValue(":name",                field.tagName);
    q.bindValue(":address",             field.address);
    q.bindValue(":type",                Model::registerTypeToString(field.type));
    q.bindValue(":read_only",           field.readOnly ? 1 : 0);
    q.bindValue(":length",              field.length);
    q.bindValue(":unified_register_id", field.unifiedRegisterId);
    q.bindValue(":display_name",        field.displayName);
    q.bindValue(":unit",                field.unit);
    q.bindValue(":scale",               field.scale);
    q.bindValue(":is_signed",           field.isSigned ? 1 : 0);
    q.bindValue(":min_value",           field.minValue);
    q.bindValue(":max_value",           field.maxValue);

    q.bindValue(":byte_order",  Model::byteOrderToString(field.byteOrder));
    q.bindValue(":bit_labels",  field.bitLabels);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }

    if (field.unifiedRegisterId < 0) {
        const int rowId = q.lastInsertId().toInt();

        QSqlQuery q2(db);
        q2.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(unified_register_id), 0) + 1 "
            "FROM registers WHERE unified_register_id >= 0"));
        if (!q2.exec() || !q2.next()) {
            error = q2.lastError().text();
            return false;
        }
        const int newUnifiedId = q2.value(0).toInt();

        QSqlQuery q3(db);
        q3.prepare(QStringLiteral(
            "UPDATE registers SET unified_register_id=:uid WHERE id=:id"));
        q3.bindValue(":uid", newUnifiedId);
        q3.bindValue(":id",  rowId);
        if (!q3.exec()) {
            error = q3.lastError().text();
            return false;
        }
    }

    return true;
}

bool DeviceDatabase::updateRegister(int deviceId, const Model::RegisterField &field, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "UPDATE registers SET name=:name, type=:type, read_only=:read_only, length=:length, "
        "unified_register_id=:unified_register_id, display_name=:display_name, unit=:unit, "
        "scale=:scale, is_signed=:is_signed, min_value=:min_value, max_value=:max_value, "
        "byte_order=:byte_order, bit_labels=:bit_labels "
        "WHERE device_id=:device_id AND address=:address"));

    q.bindValue(":device_id",           deviceId);
    q.bindValue(":address",             field.address);
    q.bindValue(":name",                field.tagName);
    q.bindValue(":type",                Model::registerTypeToString(field.type));
    q.bindValue(":read_only",           field.readOnly ? 1 : 0);
    q.bindValue(":length",              field.length);
    q.bindValue(":unified_register_id", field.unifiedRegisterId);
    q.bindValue(":display_name",        field.displayName);
    q.bindValue(":unit",                field.unit);
    q.bindValue(":scale",               field.scale);
    q.bindValue(":is_signed",           field.isSigned ? 1 : 0);
    q.bindValue(":min_value",           field.minValue);
    q.bindValue(":max_value",           field.maxValue);

    q.bindValue(":byte_order", Model::byteOrderToString(field.byteOrder));
    q.bindValue(":bit_labels",  field.bitLabels);

    if (!q.exec()) {
        error = q.lastError().text();
        return false;
    }
    return true;
}

bool DeviceDatabase::deleteRegister(int deviceId, int address, const QString &type, QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    q.prepare(QStringLiteral(
        "DELETE FROM registers WHERE device_id=:device_id AND address=:address AND type=:type"));
    q.bindValue(":device_id", deviceId);
    q.bindValue(":address",   address);
    q.bindValue(":type",      type);

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

LoginResult DeviceDatabase::validateUser(const QString &username,
                                          const QString &password,
                                          const QString &ip,
                                          QString &error)
{
    static constexpr int kMaxFailedAttempts  = 5;
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return LoginResult::InvalidCredentials;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // 1. 사용자 레코드 조회
    q.prepare(QStringLiteral(
        "SELECT password_hash, status, failed_login_count "
        "FROM users WHERE username=:username"));
    q.bindValue(":username", username);

    if (!q.exec()) {
        error = q.lastError().text();
        return LoginResult::InvalidCredentials;
    }

    if (!q.next())
        return LoginResult::InvalidCredentials;  // 존재하지 않는 계정도 동일 응답

    const QString storedHash = q.value(0).toString();
    Model::UserStatus status = Model::userStatusFromString(q.value(1).toString());
    int failedCount          = q.value(2).toInt();

    // 2. DISABLED 체크
    if (status == Model::UserStatus::Disabled) {
        error = QStringLiteral("Account is disabled.");
        return LoginResult::AccountDisabled;
    }

    // 3. LOCKED 체크 — 자동 해제 없음, 관리자가 ACTIVE로 변경해야 해제됨
    if (status == Model::UserStatus::Locked) {
        error = QStringLiteral("Account is locked. Contact administrator.");
        return LoginResult::AccountLocked;
    }

    // 4. 비밀번호 검증
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());

    if (hash != storedHash) {
        const int newCount = failedCount + 1;

        if (newCount >= kMaxFailedAttempts) {
            QSqlQuery lockQ(db);
            lockQ.prepare(QStringLiteral(
                "UPDATE users SET status='locked', failed_login_count=:count "
                "WHERE username=:username"));
            lockQ.bindValue(":count",    newCount);
            lockQ.bindValue(":username", username);
            lockQ.exec();
            error = QStringLiteral("Account locked due to too many failed attempts. Contact administrator.");
            return LoginResult::AccountLocked;
        }

        QSqlQuery failQ(db);
        failQ.prepare(QStringLiteral(
            "UPDATE users SET failed_login_count=:count WHERE username=:username"));
        failQ.bindValue(":count",    newCount);
        failQ.bindValue(":username", username);
        failQ.exec();

        return LoginResult::InvalidCredentials;
    }

    // 5. 인증 성공 — 카운터 초기화, 마지막 로그인 정보 갱신
    QSqlQuery successQ(db);
    successQ.prepare(QStringLiteral(
        "UPDATE users SET "
        "failed_login_count=0, "
        "last_login_at=CURRENT_TIMESTAMP, last_login_ip=:ip "
        "WHERE username=:username"));
    successQ.bindValue(":ip",       ip.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : ip);
    successQ.bindValue(":username", username);
    successQ.exec();

    return LoginResult::Success;
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

bool DeviceDatabase::insertSampleRegistersForDevice(int deviceId, Model::DeviceConnection::Protocol protocol, QString &error)
{
    using Proto   = Model::DeviceConnection::Protocol;
    using RegType = Model::RegisterType;

    bool isModbus = (protocol == Proto::ModbusRtu   ||
                     protocol == Proto::ModbusTcp   ||
                     protocol == Proto::ModbusAscii);
    RegType regType = isModbus ? RegType::HoldingRegister : RegType::WordRegister;

    for (int i = 0; i < 10; ++i) {
        Model::RegisterField field;
        field.tagName      = QStringLiteral("device#%1_reg_%2").arg(deviceId).arg(i, 2, 10, QChar('0'));
        field.displayName  = QStringLiteral("테스트 레지스터 %1").arg(i, 2, 10, QChar('0'));
        field.address      = i;
        field.type         = regType;
        field.readOnly     = false;
        field.length       = 1;
        field.unit         = QString();
        field.scale        = 1.0;
        field.minValue     = 0.0;
        field.maxValue     = 65535.0;
        field.byteOrder    = Model::ByteOrder::Default;

        QString regError;
        if (!insertRegister(deviceId, field, regError)) {
            error = QStringLiteral("Failed to insert sample register [%1] for device %2: %3")
                        .arg(field.tagName).arg(deviceId).arg(regError);
            return false;
        }
    }

    return true;
}

bool DeviceDatabase::insertRefrigerationSampleDevices(QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    using ConnType = Model::DeviceConnection::ConnectionType;
    using Proto    = Model::DeviceConnection::Protocol;

    struct SampleSpec {
        QString  deviceCode;
        QString  name;
        ConnType connType;
        Proto    protocol;
        QString  ipAddress;
        int      slaveId;
    };

    const QList<SampleSpec> specs = {
        { QStringLiteral("DEV-MODBUS-TCP-01"),   QStringLiteral("Modbus TCP 프로토콜"),   ConnType::Tcp,    Proto::ModbusTcp,   QStringLiteral("192.168.0.101"), 1 },
        { QStringLiteral("DEV-MODBUS-RTU-01"),   QStringLiteral("Modbus RTU 프로토콜"),   ConnType::Serial, Proto::ModbusRtu,   QString(),                       2 },
        { QStringLiteral("DEV-MODBUS-ASCII-01"), QStringLiteral("Modbus ASCII 프로토콜"), ConnType::Serial, Proto::ModbusAscii, QString(),                       3 },
        { QStringLiteral("DEV-PCLINK-01"),       QStringLiteral("PcLink 프로토콜"),       ConnType::Serial, Proto::PcLink,      QString(),                       4 },
        { QStringLiteral("DEV-PCLINK-SUM-01"),   QStringLiteral("PcLink Sum 프로토콜"),   ConnType::Serial, Proto::PcLinkSum,   QString(),                       5 },
    };

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    for (const SampleSpec &spec : specs) {
        Model::DeviceInfo device;
        device.id          = -1;
        device.deviceCode  = spec.deviceCode;
        device.name        = spec.name;
        device.displayName = spec.name;

        device.connection.type             = spec.connType;
        device.connection.protocol         = spec.protocol;
        device.connection.ipAddress        = spec.ipAddress;
        device.connection.tcpPort          = 502;
        device.connection.slaveId          = spec.slaveId;
        device.connection.timeoutMs        = 3000;
        device.connection.defaultByteOrder = Model::ByteOrder::BigEndian;

        device.polling.intervalMs  = 1000;
        device.polling.retryCount  = 3;

        QString insertError;
        if (!insertDevice(device, insertError)) {
            db.rollback();
            error = QStringLiteral("Failed to insert sample device [%1]: %2")
                        .arg(spec.name, insertError);
            return false;
        }

        QSqlQuery idQuery(db);
        if (!idQuery.exec(QStringLiteral("SELECT last_insert_rowid()")) || !idQuery.next()) {
            db.rollback();
            error = QStringLiteral("Failed to retrieve ID for device [%1]").arg(spec.name);
            return false;
        }
        int deviceId = idQuery.value(0).toInt();

        if (!insertSampleRegistersForDevice(deviceId, spec.protocol, insertError)) {
            db.rollback();
            error = insertError;
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

/*
bool DeviceDatabase::insertRefrigerationSampleDevices(QString &error)
{
    if (!isOpen()) {
        error = QStringLiteral("Database is not open.");
        return false;
    }

    QList<Model::DeviceInfo> devices;

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-CHILLER-01"),
        QStringLiteral("냉동기 1호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.101"),
        1,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-CHILLER-02"),
        QStringLiteral("냉동기 2호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.102"),
        2,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-COMP-01"),
        QStringLiteral("압축기 1호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        3,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-COMP-02"),
        QStringLiteral("압축기 2호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        4,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-COND-01"),
        QStringLiteral("응축기 1호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        5,
        1500));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-COND-02"),
        QStringLiteral("응축기 2호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        6,
        1500));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-EVAP-01"),
        QStringLiteral("증발기 1호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        7,
        1500));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-EVAP-02"),
        QStringLiteral("증발기 2호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        8,
        1500));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-PUMP-CHW-01"),
        QStringLiteral("냉수 펌프 1호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.111"),
        9,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-PUMP-CHW-02"),
        QStringLiteral("냉수 펌프 2호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.112"),
        10,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-PUMP-CW-01"),
        QStringLiteral("냉각수 펌프 1호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.113"),
        11,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-PUMP-CW-02"),
        QStringLiteral("냉각수 펌프 2호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.114"),
        12,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-CT-01"),
        QStringLiteral("냉각탑 1호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.121"),
        13,
        2000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-CT-02"),
        QStringLiteral("냉각탑 2호기"),
        Model::DeviceConnection::ConnectionType::Tcp,
        QStringLiteral("192.168.0.122"),
        14,
        2000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-FAN-01"),
        QStringLiteral("냉각팬 1호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        15,
        2000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-FAN-02"),
        QStringLiteral("냉각팬 2호기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        16,
        2000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-TANK-01"),
        QStringLiteral("브라인 탱크"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        17,
        3000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-FLOW-01"),
        QStringLiteral("냉수 유량계"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        18,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-PRESS-01"),
        QStringLiteral("냉매 압력 계측기"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        19,
        1000));

    devices.append(makeSampleDevice(
        QStringLiteral("DEV-TEMP-01"),
        QStringLiteral("온도 계측 모듈"),
        Model::DeviceConnection::ConnectionType::Serial,
        QString(),
        20,
        1000));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);

    if (!db.transaction()) {
        error = db.lastError().text();
        return false;
    }

    for (const Model::DeviceInfo &device : devices) {
        QString insertError;

        if (!insertDevice(device, insertError)) {
            db.rollback();

            error = QStringLiteral("Failed to insert sample device [%1]: %2")
                        .arg(device.name, insertError);
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
*/


} // namespace Database
} // namespace DataCollection
