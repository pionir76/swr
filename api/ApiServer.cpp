#include "ApiServer.h"

#include "../config/AppConfig.h"
#include "../data_collection/database/DeviceDatabase.h"
#include "../data_collection/store/RegisterTable.h"
#include "../data_collection/store/DeviceList.h"
#include "../data_collection/polling/PollingManager.h"
#include "../utils/Logger.h"

#include <QCoreApplication>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QUuid>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTimer>
#include <sys/statvfs.h>
#include <algorithm>

using namespace DataCollection;

namespace Api {

ApiServer::ApiServer(Database::DeviceDatabase *db,
                     std::shared_ptr<Store::RegisterTable> registerTable,
                     std::shared_ptr<Store::DeviceList> deviceList,
                     Polling::PollingManager *pollingManager,
                     QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_registerTable(std::move(registerTable))
    , m_deviceList(std::move(deviceList))
    , m_pollingManager(pollingManager)
{
}

ApiServer::~ApiServer()
{
    stop();
}

bool ApiServer::start(quint16 port, QString& error)
{
    setupRoutes();

    auto *tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, port)) {
        error = QStringLiteral("ApiServer failed to listen on port %1: %2")
        .arg(port).arg(tcpServer->errorString());
        delete tcpServer;
        return false;
    }

    if (!m_server.bind(tcpServer)) {
        error = QStringLiteral("ApiServer failed to bind TCP server on port %1").arg(port);
        delete tcpServer;
        return false;
    }

    Util::Logger::info(QStringLiteral("ApiServer listening on port %1").arg(port));
    return true;
}

void ApiServer::stop()
{
    // QHttpServer는 소멸 시 자동 정리
}

// ---------------------------------------------------------------------------
// Route Setup
// ---------------------------------------------------------------------------
void ApiServer::setupRoutes()
{
    // Authotification
    m_server.route("/api/login",  QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleLogin(req); });
    m_server.route("/api/logout", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleLogout(req); });
    m_server.route("/api/session", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleSession(req); });

    // Polling Control
    m_server.route("/api/polling/status", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetPollingStatus(req); });
    m_server.route("/api/polling/start", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleStartPolling(req); });
    m_server.route("/api/polling/stop", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleStopPolling(req); });


    // Device List
    m_server.route("/api/devices", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetDevices(req); });
    m_server.route("/api/devices/status", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetDeviceStatus(req); });
    m_server.route("/api/devices", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostDevice(req); });
    m_server.route("/api/devices/<arg>", QHttpServerRequest::Method::Put,
                   [this](const QString &id, const QHttpServerRequest &req) { return handlePutDevice(req, id); });
    m_server.route("/api/devices/<arg>", QHttpServerRequest::Method::Delete,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleDeleteDevice(req, id); });

    // registers.
    m_server.route("/api/devices/<arg>/registers", QHttpServerRequest::Method::Get,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleGetRegisters(req, id); });
    m_server.route("/api/devices/<arg>/registers", QHttpServerRequest::Method::Post,
                   [this](const QString &id, const QHttpServerRequest &req) { return handlePostRegister(req, id); });
    m_server.route("/api/devices/<arg>/registers", QHttpServerRequest::Method::Put,
                   [this](const QString &id, const QHttpServerRequest &req) { return handlePutRegister(req, id); });
    m_server.route("/api/devices/<arg>/registers", QHttpServerRequest::Method::Delete,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleDeleteRegister(req, id); });
    m_server.route("/api/devices/<arg>/registers/write", QHttpServerRequest::Method::Post,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleWriteRegister(req, id); });


    // realtime update.
    m_server.route("/api/registers/realtime", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetRealtime(req); });

    // logs
    m_server.route("/api/logs", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetLogs(req); });


    // User
    m_server.route("/api/users", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetUsers(req); });
    m_server.route("/api/users", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostUser(req); });
    m_server.route("/api/users/<arg>", QHttpServerRequest::Method::Delete,
                   [this](const QString &username, const QHttpServerRequest &req) {
                       return handleDeleteUser(req, username); });
    m_server.route("/api/users/<arg>", QHttpServerRequest::Method::Put,
                   [this](const QString &username, const QHttpServerRequest &req) {
                       return handlePutUser(req, username); });
    m_server.route("/api/users/<arg>/password", QHttpServerRequest::Method::Put,
                   [this](const QString &username, const QHttpServerRequest &req) {
                       return handlePutUserPassword(req, username); });
    m_server.route("/api/users/<arg>/status", QHttpServerRequest::Method::Put,
                   [this](const QString &username, const QHttpServerRequest &req) {
                       return handlePutUserStatus(req, username); });

    // System config
    m_server.route("/api/config", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetConfig(req); });
    m_server.route("/api/config/reset", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostConfigReset(req); });
    m_server.route("/api/config/network", QHttpServerRequest::Method::Put,
                   [this](const QHttpServerRequest &req) { return handlePutConfigNetwork(req); });
    m_server.route("/api/config/serial", QHttpServerRequest::Method::Put,
                   [this](const QHttpServerRequest &req) { return handlePutConfigSerial(req); });
    m_server.route("/api/config/system", QHttpServerRequest::Method::Put,
                   [this](const QHttpServerRequest &req) { return handlePutConfigSystem(req); });
    m_server.route("/api/config/modbus-server", QHttpServerRequest::Method::Put,
                   [this](const QHttpServerRequest &req) { return handlePutConfigModbusServer(req); });
    m_server.route("/api/system/restart", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostRestart(req); });
    m_server.route("/api/system/info", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetSystemInfo(req); });
    m_server.route("/api/users/login-history", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetLoginHistory(req); });
    m_server.route("/api/users/login-history", QHttpServerRequest::Method::Delete,
                   [this](const QHttpServerRequest &req) { return handleDeleteLoginHistory(req); });
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
bool ApiServer::isAuthenticated(const QHttpServerRequest &request) const
{
    const QByteArray auth = request.value("Authorization");
    if (auth.isEmpty()){
        return false;
    }

    const QString token = QString::fromUtf8(auth).remove(QStringLiteral("Bearer ")).trimmed();

    QMutexLocker locker(&m_sessionMutex);
    return m_sessions.contains(token);
}

QString ApiServer::createSession(const QString &username)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QMutexLocker locker(&m_sessionMutex);
    m_sessions.insert(token);
    m_sessionUsers.insert(token, username);
    return token;
}

QString ApiServer::removeSession(const QString &token)
{
    QMutexLocker locker(&m_sessionMutex);
    m_sessions.remove(token);
    return m_sessionUsers.take(token);
}

bool ApiServer::rejectIfPolling(QHttpServerResponse &out) const
{
    if (!m_pollingManager->isRunning()) return false;

    QJsonObject body;
    body["error"] = QStringLiteral("Polling is running. Stop polling before modifying configuration.");
    out = QHttpServerResponse(body, QHttpServerResponse::StatusCode::Conflict);
    return true;
}

// ---------------------------------------------------------------------------
// Authenticate Handling
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleLogin(const QHttpServerRequest &request)
{
    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    qDebug() << doc;

    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString username = body.value("username").toString();
    const QString password = body.value("password").toString();
    const QString ip       = request.remoteAddress().toString();

    QString dbError;
    const DataCollection::Database::LoginResult result =
        m_db->validateUser(username, password, ip, dbError);

    Model::LoginHistoryEntry hist;
    hist.username = username;
    hist.action   = QStringLiteral("login");
    hist.result   = (result == DataCollection::Database::LoginResult::Success)
                        ? QStringLiteral("success") : QStringLiteral("fail");
    hist.ip       = ip;

    QString histError;
    m_db->insertLoginHistory(hist, histError);

    switch (result) {
    case DataCollection::Database::LoginResult::Success: {
        const QString token = createSession(username);
        Util::Logger::info(QStringLiteral("User logged in: %1 from %2").arg(username, ip));
        QJsonObject resp;
        resp[QLatin1String("token")] = token;
        return QHttpServerResponse(resp);
    }
    case DataCollection::Database::LoginResult::AccountDisabled: {
        Util::Logger::warning(QStringLiteral("Login denied (disabled): %1 from %2").arg(username, ip));
        QJsonObject err;
        err[QLatin1String("error")]  = QStringLiteral("Account is disabled.");
        err[QLatin1String("reason")] = QStringLiteral("disabled");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }
    case DataCollection::Database::LoginResult::AccountLocked: {
        Util::Logger::warning(QStringLiteral("Login denied (locked): %1 from %2").arg(username, ip));
        QJsonObject err;
        err[QLatin1String("error")]  = dbError;
        err[QLatin1String("reason")] = QStringLiteral("locked");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }
    default:
        Util::Logger::warning(QStringLiteral("Login failed (bad credentials): %1 from %2").arg(username, ip));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }
}

QHttpServerResponse ApiServer::handleLogout(const QHttpServerRequest &request)
{
    const QString token    = QString::fromUtf8(request.value("Authorization"))
                                 .remove(QStringLiteral("Bearer ")).trimmed();
    const QString username = removeSession(token);
    const QString ip       = request.remoteAddress().toString();

    if (!username.isEmpty()) {
        Model::LoginHistoryEntry hist;
        hist.username = username;
        hist.action   = QStringLiteral("logout");
        hist.result   = QStringLiteral("success");
        hist.ip       = ip;
        QString histError;
        m_db->insertLoginHistory(hist, histError);
        Util::Logger::info(QStringLiteral("User logged out: %1 from %2").arg(username, ip));
    }

    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleSession(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    QJsonObject resp;
    resp["valid"] = true;
    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// Polling Control Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetPollingStatus(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    QJsonObject resp;
    resp["running"] = m_pollingManager->isRunning();
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleStartPolling(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    QString error;
    if (!m_pollingManager->start(error)) {
        QJsonObject body;
        body["error"] = error;
        return QHttpServerResponse(body, QHttpServerResponse::StatusCode::Conflict);
    }

    Util::Logger::info(QStringLiteral("Polling started via API."));
    QJsonObject resp;
    resp["running"] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleStopPolling(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    m_pollingManager->stop();
    Util::Logger::info(QStringLiteral("Polling stopped via API."));

    QJsonObject resp;
    resp["running"] = false;
    return QHttpServerResponse(resp);
}


// ---------------------------------------------------------------------------
// Device Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetDevices(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QList<Model::DeviceInfo> devices = m_deviceList->getAll();

    QJsonArray arr;
    for (const Model::DeviceInfo &d : devices) {
        QJsonObject obj;
        obj["id"]          = d.id;
        obj["deviceCode"]  = d.deviceCode;
        obj["name"]        = d.name;
        obj["displayName"] = d.displayName;
        obj["connType"]    = Model::connectionTypeToString(d.connection.type);
        obj["protocol"]    = Model::protocolToString(d.connection.protocol);
        obj["ipAddress"]   = d.connection.ipAddress;
        obj["tcpPort"]     = d.connection.tcpPort;
        obj["slaveId"]     = d.connection.slaveId;
        obj["timeoutMs"]   = d.connection.timeoutMs;
        obj["intervalMs"]  = d.polling.intervalMs;
        obj["retryCount"]  = d.polling.retryCount;
        obj["byteOrder"]   = Model::byteOrderToString(d.connection.defaultByteOrder);
        arr.append(obj);
    }

    QJsonObject resp;
    resp["devices"] = arr;

    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleGetDeviceStatus(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QList<Model::DeviceInfo> devices = m_deviceList->getAll();

    QJsonArray arr;
    for (const Model::DeviceInfo &d : devices) {
        QString stateStr;
        switch (d.status.state) {
        case Model::DeviceInfo::Status::State::Ok:    stateStr = QStringLiteral("ok");      break;
        case Model::DeviceInfo::Status::State::Error: stateStr = QStringLiteral("error");   break;
        default:                                      stateStr = QStringLiteral("unknown"); break;
        }

        QJsonObject obj;
        obj["deviceId"]           = d.id;
        obj["deviceCode"]         = d.deviceCode;
        obj["displayName"]        = d.displayName;
        obj["state"]              = stateStr;
        obj["lastPollTimestamp"]  = d.status.lastPollTimestamp;
        obj["lastPollDurationMs"] = d.status.lastPollDurationMs;
        obj["consecutiveErrors"]  = d.status.consecutiveErrors;
        obj["lastError"]          = d.status.lastError;
        arr.append(obj);
    }

    QJsonObject resp;
    resp["devices"] = arr;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostDevice(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString name = body.value("name").toString().trimmed();
    if (name.isEmpty()) {
        QJsonObject err;
        err["error"] = QStringLiteral("name is required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    Model::DeviceInfo device;
    device.deviceCode   = body.value("deviceCode").toString().trimmed();
    device.name         = name;
    device.displayName  = body.value("displayName").toString(name);

    Model::DeviceConnection &conn = device.connection;
    const QString connTypeStr = body.value("connType").toString(QStringLiteral("serial")).toLower();
    conn.type      = Model::connectionTypeFromString(connTypeStr);
    conn.ipAddress = body.value("ipAddress").toString(conn.ipAddress);
    conn.tcpPort   = body.value("tcpPort").toInt(conn.tcpPort);
    conn.slaveId   = body.value("slaveId").toInt(conn.slaveId);
    conn.timeoutMs = body.value("timeoutMs").toInt(conn.timeoutMs);
    const QString byteOrderStr = body.value("byteOrder").toString(QStringLiteral("big")).toLower();
    
    conn.defaultByteOrder = Model::byteOrderFromString(byteOrderStr);
    if (conn.defaultByteOrder == Model::ByteOrder::Default) {
        QJsonObject err;
        err["error"] = QStringLiteral("byteOrder must be 'big' or 'little' for a device: %1").arg(byteOrderStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // If protocol is empty, set default protocol by connection tye.
    const QString protocolStr = body.value("protocol").toString().toLower();
    if (!protocolStr.isEmpty()) {
        conn.protocol = Model::protocolFromString(protocolStr);
        if (conn.protocol == Model::DeviceConnection::Protocol::Unknown) {
            QJsonObject err;
            err["error"] = QStringLiteral("Unknown protocol: %1").arg(protocolStr);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    } else {
        conn.protocol = (conn.type == Model::DeviceConnection::ConnectionType::Tcp)
        ? Model::DeviceConnection::Protocol::ModbusTcp
        : Model::DeviceConnection::Protocol::ModbusRtu;
    }

    device.polling.intervalMs = body.value("intervalMs").toInt(device.polling.intervalMs);
    device.polling.retryCount = body.value("retryCount").toInt(device.polling.retryCount);

    QString error;
    if (!syncAddDevice(device, error)) {
        Util::Logger::error(QStringLiteral("addDevice failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Device added: %1").arg(device.name));
    QJsonObject resp;
    resp["name"] = device.name;
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiServer::handlePutDevice(const QHttpServerRequest &request, const QString &id)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const int deviceId = id.toInt();
    Model::DeviceInfo device;
    try {
        device = m_deviceList->get(deviceId);
    } catch (const std::out_of_range &) {
        QJsonObject err;
        err["error"] = QStringLiteral("Device not found");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const QJsonObject body = doc.object();
    if (body.contains("deviceCode"))  device.deviceCode  = body.value("deviceCode").toString().trimmed();
    if (body.contains("name"))        device.name        = body.value("name").toString().trimmed();
    if (body.contains("displayName")) device.displayName = body.value("displayName").toString().trimmed();
    if (body.contains("ipAddress"))   device.connection.ipAddress = body.value("ipAddress").toString();
    if (body.contains("tcpPort"))     device.connection.tcpPort   = body.value("tcpPort").toInt();
    if (body.contains("slaveId"))     device.connection.slaveId   = body.value("slaveId").toInt();
    if (body.contains("timeoutMs"))   device.connection.timeoutMs = body.value("timeoutMs").toInt();
    if (body.contains("intervalMs"))  device.polling.intervalMs   = body.value("intervalMs").toInt();
    if (body.contains("retryCount"))  device.polling.retryCount   = body.value("retryCount").toInt();
    if (body.contains("byteOrder")) {
        const QString byteOrderStr = body.value("byteOrder").toString().toLower();
        device.connection.defaultByteOrder = Model::byteOrderFromString(byteOrderStr);
        if (device.connection.defaultByteOrder == Model::ByteOrder::Default) {
            QJsonObject err;
            err["error"] = QStringLiteral("byteOrder must be 'big' or 'little' for a device: %1").arg(byteOrderStr);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    }
    if (body.contains("connType"))
        device.connection.type = Model::connectionTypeFromString(
            body.value("connType").toString().toLower());
    if (body.contains("protocol")) {
        const QString protocolStr = body.value("protocol").toString().toLower();
        device.connection.protocol = Model::protocolFromString(protocolStr);
        if (device.connection.protocol == Model::DeviceConnection::Protocol::Unknown) {
            QJsonObject err;
            err["error"] = QStringLiteral("Unknown protocol: %1").arg(protocolStr);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    }

    QString error;
    if (!syncUpdateDevice(device, error)) {
        Util::Logger::error(QStringLiteral("updateDevice failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Device updated: id=%1").arg(deviceId));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}


QHttpServerResponse ApiServer::handleDeleteDevice(const QHttpServerRequest &request, const QString &id)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);
    if (rejectIfPolling(conflict)) return conflict;

    QString error;
    if (!syncDeleteDevice(id.toInt(), error)) {
        Util::Logger::error(QStringLiteral("deleteDevice failed: %1").arg(error));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

// ---------------------------------------------------------------------------
// Register Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetRegisters(const QHttpServerRequest &request,
                                                  const QString &deviceId)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    Model::DeviceInfo device;
    try {
        device = m_deviceList->get(deviceId.toInt());
    } catch (const std::out_of_range &) {
        QJsonObject err;
        err["error"] = QStringLiteral("Device not found");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    QJsonArray arr;
    for (const Model::RegisterField &f : device.registers) {
        QJsonObject obj;
        obj["tagName"]     = f.tagName;
        obj["displayName"] = f.displayName;
        obj["address"]     = f.address;
        obj["type"]        = Model::registerTypeToString(f.type);
        obj["readOnly"]    = f.readOnly;
        obj["length"]      = f.length;
        obj["unit"]        = f.unit;
        obj["scale"]       = f.scale;
        obj["isSigned"]    = f.isSigned;
        obj["bitLabels"]   = f.bitLabels;
        obj["minValue"]    = f.minValue;
        obj["maxValue"]    = f.maxValue;
        obj["byteOrder"]   = Model::byteOrderToString(f.byteOrder);

        arr.append(obj);
    }

    QJsonObject resp;
    resp["registers"] = arr;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostRegister(const QHttpServerRequest &request,
                                                  const QString &deviceId)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString tagName = body.value("tagName").toString().trimmed();
    const QString typeStr = body.value("type").toString().trimmed();
    
    if (tagName.isEmpty() || typeStr.isEmpty()) {
        QJsonObject err;
        err["error"] = QStringLiteral("tagName and type are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    Model::RegisterField field;
    field.tagName     = tagName;
    field.displayName = body.value("displayName").toString(tagName);
    field.address     = body.value("address").toInt(0);
    field.type        = Model::registerTypeFromString(typeStr);
    field.readOnly    = body.value("readOnly").toBool(false);
    field.length      = body.value("length").toInt(1);
    field.unit        = body.value("unit").toString();
    field.scale       = body.value("scale").toDouble(1.0);
    field.isSigned    = body.value("isSigned").toBool(false);
    field.bitLabels   = body.value("bitLabels").toString();

    if (body.contains("byteOrder"))
        field.byteOrder = Model::byteOrderFromString(body.value("byteOrder").toString().toLower());
    if (body.contains("minValue"))
        field.minValue = body.value("minValue").toDouble();
    if (body.contains("maxValue"))
        field.maxValue = body.value("maxValue").toDouble();

    if (field.type == Model::RegisterType::Unknown) {
        QJsonObject err;
        err["error"] = QStringLiteral("Unknown register type: %1").arg(typeStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const int devId = deviceId.toInt();
    QString error;
    if (!syncAddRegister(devId, field, error)) {
        Util::Logger::error(QStringLiteral("addRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register added: device=%1 tag=%2").arg(devId).arg(tagName));
    QJsonObject resp;
    resp["tagName"] = field.tagName;
    resp["address"] = field.address;
    resp["type"]    = Model::registerTypeToString(field.type);
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiServer::handlePutRegister(const QHttpServerRequest &request,
                                                 const QString &deviceId)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString tagName = body.value("tagName").toString().trimmed();
    const QString typeStr = body.value("type").toString().trimmed();
    if (!body.contains("address") || tagName.isEmpty() || typeStr.isEmpty()) {
        QJsonObject err;
        err["error"] = QStringLiteral("address, tagName and type are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const int devId   = deviceId.toInt();
    const int address = body.value("address").toInt();
    const Model::RegisterType type = Model::registerTypeFromString(typeStr);

    if (type == Model::RegisterType::Unknown) {
        QJsonObject err;
        err["error"] = QStringLiteral("Unknown register type: %1").arg(typeStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // unifiedRegisterId must survive the edit — RegisterTable / external Modbus TCP
    // mapping keys off it, so we look up the existing row instead of trusting the client.
    QString loadError;
    const QList<Model::RegisterField> existing = m_db->loadRegisters(devId, loadError);
    const auto it = std::find_if(existing.begin(), existing.end(), [&](const Model::RegisterField &f) {
        return f.address == address && f.type == type;
    });
    if (it == existing.end()) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register not found: addr=%1 type=%2").arg(address).arg(typeStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    Model::RegisterField field;
    field.tagName           = tagName;
    field.displayName       = body.value("displayName").toString(tagName);
    field.address           = address;
    field.type              = type;
    field.readOnly          = body.value("readOnly").toBool(false);
    field.length            = body.value("length").toInt(1);
    field.unit              = body.value("unit").toString();
    field.scale             = body.value("scale").toDouble(1.0);
    field.isSigned          = body.value("isSigned").toBool(false);
    field.bitLabels         = body.value("bitLabels").toString();
    field.unifiedRegisterId = it->unifiedRegisterId;

    if (body.contains("byteOrder"))
        field.byteOrder = Model::byteOrderFromString(body.value("byteOrder").toString().toLower());
    if (body.contains("minValue"))
        field.minValue = body.value("minValue").toDouble();
    if (body.contains("maxValue"))
        field.maxValue = body.value("maxValue").toDouble();

    QString error;
    if (!syncUpdateRegister(devId, field, error)) {
        Util::Logger::error(QStringLiteral("updateRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register updated: device=%1 addr=%2 tag=%3")
                           .arg(devId).arg(address).arg(tagName));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleDeleteRegister(const QHttpServerRequest &request,
                                                    const QString &deviceId)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString typeStr = body.value("type").toString().trimmed();
    if (!body.contains("address") || typeStr.isEmpty()) {
        QJsonObject err;
        err["error"] = QStringLiteral("address and type are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const int devId   = deviceId.toInt();
    const int address = body.value("address").toInt();

    QString error;
    if (!syncDeleteRegister(devId, address, typeStr, error)) {
        Util::Logger::error(QStringLiteral("deleteRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register deleted: device=%1 addr=%2 type=%3")
                           .arg(devId).arg(address).arg(typeStr));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleWriteRegister(const QHttpServerRequest &request,
                                                   const QString &deviceId)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString typeStr  = body.value("type").toString().trimmed();
    if (!body.contains("address") || typeStr.isEmpty() || !body.contains("rawValues")) {
        QJsonObject err;
        err["error"] = QStringLiteral("address, type, rawValues are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const int devId   = deviceId.toInt();
    const int address = body.value("address").toInt();
    const Model::RegisterType type = Model::registerTypeFromString(typeStr);

    // 실제 RegisterField를 조회해 readOnly 여부를 확인한다.
    Model::DeviceInfo device;
    try {
        device = m_deviceList->get(devId);
    } catch (const std::out_of_range &) {
        QJsonObject err;
        err["error"] = QStringLiteral("Device not found");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const auto it = std::find_if(device.registers.begin(), device.registers.end(),
                                 [&](const Model::RegisterField &f) {
                                     return f.address == address && f.type == type;
                                 });
    if (it == device.registers.end()) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register not found: addr=%1 type=%2").arg(address).arg(typeStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    if (it->readOnly) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register is read-only: %1").arg(it->tagName);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const QJsonArray rawArr = body.value("rawValues").toArray();
    QVector<quint16> rawValues;
    for (const QJsonValue &v : rawArr)
        rawValues.append(static_cast<quint16>(v.toInt()));

    Model::WriteRequest req;
    req.field     = *it;
    req.rawValues = rawValues;

    if (req.field.type == Model::RegisterType::Coil ||
        req.field.type == Model::RegisterType::BitRegister) {
        for (quint16 v : rawValues)
            req.coilValues.append(v != 0);
    }

    m_deviceList->enqueueWrite(devId, std::move(req));

    Util::Logger::info(QStringLiteral("Write enqueued: device=%1 addr=%2 type=%3")
                           .arg(devId).arg(address).arg(typeStr));
    QJsonObject resp;
    resp["ok"] = true;
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Accepted);
}

// ---------------------------------------------------------------------------
// Real Time Update.
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetRealtime(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request)){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QList<Model::UnifiedRegister> regs = m_registerTable->unifiedRegisters();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonArray arr;
    for (const Model::UnifiedRegister &r : regs) {

        //--------------------------------------------------------------------------//
        // For each register, determine the quality of the data based on the last 
        // updated time and the polling interval.
        // Polling Quality : Now time - lastUpdated vs pollingIntervalMs
        //--------------------------------------------------------------------------//
        Model::DataQuality quality = Model::DataQuality::Bad;
        if (r.lastUpdated.isValid() && r.pollingIntervalMs > 0) {
            const qint64 elapsedMs = r.lastUpdated.msecsTo(now);
            
            if (elapsedMs < r.pollingIntervalMs * 3 / 2){
                quality = Model::DataQuality::Good;
            }
            else if (elapsedMs < r.pollingIntervalMs * 3){
                quality = Model::DataQuality::Normal;
            }   
        }

        QJsonObject obj;
        obj["id"]           = r.id;
        obj["tagName"]      = r.tagName;
        obj["displayName"]  = r.displayName;
        obj["deviceId"]     = r.deviceId;
        obj["address"]      = r.deviceAddress;
        obj["sourceType"]   = Model::registerTypeToString(r.sourceType);
        obj["unit"]         = r.unit;
        obj["scaledValue"]  = r.scaledValue;
        obj["scale"]        = r.scale;
        obj["rawWord"]      = r.rawRegisters.isEmpty() ? 0 : static_cast<int>(r.rawRegisters.first());
        obj["bitLabels"]    = r.bitLabels;
        obj["readOnly"]     = r.readOnly;
        obj["isValid"]      = r.isValid;
        obj["outOfRange"]   = r.outOfRange;
        obj["quality"]      = Model::dataQualityToString(quality);
        obj["lastUpdated"]  = r.lastUpdated.toString(Qt::ISODate);
        obj["errorMessage"] = r.errorMessage;
        arr.append(obj);
    }

    QJsonObject resp;
    resp["registers"] = arr;
    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// Log Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetLogs(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QUrlQuery query(request.query());

    int limit = Util::Logger::maxLogRows();
    if (query.hasQueryItem(QStringLiteral("limit"))) {
        bool ok = false;
        const int requested = query.queryItemValue(QStringLiteral("limit")).toInt(&ok);
        if (ok && requested > 0) {
            limit = requested;
        }
    }

    const QString level = query.queryItemValue(QStringLiteral("level")).toUpper();
    const QString from  = query.queryItemValue(QStringLiteral("from"));
    const QString to    = query.queryItemValue(QStringLiteral("to"));

    QString error;
    const QList<Util::LogEntry> entries = Util::Logger::fetch(limit, level, from, to, error);

    if (!error.isEmpty()) {
        QJsonObject body;
        body["error"] = error;
        return QHttpServerResponse(body, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray arr;
    for (const Util::LogEntry &entry : entries) {
        QJsonObject obj;
        obj["id"]        = entry.id;
        obj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        obj["level"]     = Util::logLevelToString(entry.level);
        obj["message"]   = entry.message;
        arr.append(obj);
    }

    QJsonObject resp;
    resp["logs"]  = arr;
    resp["count"] = arr.size();
    
    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// User Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetUsers(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    QString error;
    const QList<Model::UserInfo> users = m_db->loadUsers(error);
    if (!error.isEmpty()) {
        Util::Logger::error(QStringLiteral("loadUsers failed: %1").arg(error));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray arr;
    for (const Model::UserInfo &u : users) {
        QJsonObject obj;
        obj["id"]               = u.id;
        obj["username"]         = u.username;
        obj["displayName"]      = u.displayName;
        obj["description"]      = u.description;
        obj["role"]             = Model::userRoleToString(u.role);
        obj["status"]           = Model::userStatusToString(u.status);
        obj["failedLoginCount"] = u.failedLoginCount;
        obj["lockedUntil"]      = u.lockedUntil;
        obj["lastLoginAt"]      = u.lastLoginAt;
        obj["lastLoginIp"]      = u.lastLoginIp;
        arr.append(obj);
    }

    QJsonObject resp;
    resp["users"] = arr;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostUser(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString username    = body.value("username").toString().trimmed();
    const QString displayName = body.value("displayName").toString(username);
    const QString password    = body.value("password").toString();
    const QString roleStr     = body.value("role").toString().toLower().trimmed();

    if (username.isEmpty() || password.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("username and password are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    if (roleStr != QLatin1String("user") &&
        roleStr != QLatin1String("manager") &&
        roleStr != QLatin1String("admin")) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("role is required: user | manager | admin");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    Model::UserInfo user;
    user.username     = username;
    user.displayName  = displayName;
    user.description  = body.value("description").toString();
    user.passwordHash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    user.role         = Model::userRoleFromString(roleStr);
    user.status       = Model::UserStatus::Active;

    QString error;
    if (!m_db->insertUser(user, error)) {
        Util::Logger::error(QStringLiteral("insertUser failed: %1").arg(error));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    const QString resolvedRole = Model::userRoleToString(user.role);
    Util::Logger::info(QStringLiteral("User created: %1 (%2)").arg(username, resolvedRole));

    QJsonObject resp;
    resp["username"] = username;
    resp["role"]     = resolvedRole;

    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiServer::handleDeleteUser(const QHttpServerRequest &request,
                                                const QString &username)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    QString error;
    if (!m_db->deleteUser(username, error)) {
        Util::Logger::error(QStringLiteral("deleteUser failed: %1").arg(error));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("User deleted: %1").arg(username));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QString ApiServer::sessionUsername(const QHttpServerRequest &request) const
{
    const QString token = QString::fromUtf8(request.value("Authorization"))
                              .remove(QStringLiteral("Bearer ")).trimmed();
    QMutexLocker locker(&m_sessionMutex);
    return m_sessionUsers.value(token);
}

bool ApiServer::isLastAdmin(const QString &excludeUsername, QString &error) const
{
    const QList<Model::UserInfo> users = m_db->loadUsers(error);
    if (!error.isEmpty()) return false;
    int count = 0;
    for (const auto &u : users) {
        if (u.role == Model::UserRole::Admin &&
            u.status == Model::UserStatus::Active &&
            u.username != excludeUsername)
            ++count;
    }
    return count == 0;
}

QHttpServerResponse ApiServer::handlePutUser(const QHttpServerRequest &request,
                                              const QString &username)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    QString dbError;
    bool found = false;
    Model::UserInfo user = m_db->loadUser(username, found, dbError);
    if (!dbError.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }
    if (!found) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("User not found: %1").arg(username);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const QJsonObject body = doc.object();

    if (body.contains(QLatin1String("displayName"))) {
        const QString dn = body.value(QLatin1String("displayName")).toString().trimmed();
        if (dn.isEmpty()) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("displayName cannot be empty");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        user.displayName = dn;
    }

    if (body.contains(QLatin1String("description")))
        user.description = body.value(QLatin1String("description")).toString();

    if (body.contains(QLatin1String("role"))) {
        const QString roleStr = body.value(QLatin1String("role")).toString().toLower().trimmed();
        if (roleStr != QLatin1String("user") &&
            roleStr != QLatin1String("manager") &&
            roleStr != QLatin1String("admin")) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("role must be: user | manager | admin");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        const Model::UserRole newRole = Model::userRoleFromString(roleStr);
        if (user.role == Model::UserRole::Admin && newRole != Model::UserRole::Admin) {
            QString checkError;
            if (isLastAdmin(username, checkError)) {
                QJsonObject err;
                err[QLatin1String("error")] = QStringLiteral("Cannot demote the last active admin.");
                return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
            }
        }
        user.role = newRole;
    }

    if (!m_db->updateUser(user, dbError)) {
        Util::Logger::error(QStringLiteral("updateUser failed: %1").arg(dbError));
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("User updated: %1").arg(username));

    QJsonObject resp;
    resp[QLatin1String("username")]    = user.username;
    resp[QLatin1String("displayName")] = user.displayName;
    resp[QLatin1String("description")] = user.description;
    resp[QLatin1String("role")]        = Model::userRoleToString(user.role);
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutUserPassword(const QHttpServerRequest &request,
                                                      const QString &username)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString newPassword = body.value(QLatin1String("newPassword")).toString();
    if (newPassword.length() < 8) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("newPassword must be at least 8 characters");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const QString caller = sessionUsername(request);
    QString dbError;
    bool found = false;
    Model::UserInfo callerInfo = m_db->loadUser(caller, found, dbError);
    if (!found || !dbError.isEmpty())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    found = false;
    Model::UserInfo user = m_db->loadUser(username, found, dbError);
    if (!dbError.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }
    if (!found) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("User not found: %1").arg(username);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const bool isSelf  = (caller == username);
    const bool isAdmin = (callerInfo.role == Model::UserRole::Admin);

    if (!isSelf && !isAdmin) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Permission denied.");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }

    if (isSelf && !isAdmin) {
        const QString currentPassword = body.value(QLatin1String("currentPassword")).toString();
        if (currentPassword.isEmpty()) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("currentPassword is required");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        const QString currentHash = QString::fromLatin1(
            QCryptographicHash::hash(currentPassword.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (currentHash != user.passwordHash) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("Current password is incorrect");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Unauthorized);
        }
    }

    user.passwordHash = QString::fromLatin1(
        QCryptographicHash::hash(newPassword.toUtf8(), QCryptographicHash::Sha256).toHex());

    if (!m_db->updateUser(user, dbError)) {
        Util::Logger::error(QStringLiteral("updateUser (password) failed: %1").arg(dbError));
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Password changed: %1 by %2").arg(username, caller));
    QJsonObject resp;
    resp[QLatin1String("username")] = username;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutUserStatus(const QHttpServerRequest &request,
                                                    const QString &username)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

        qDebug() << "handlePutUserStatus: request body:" << doc.toJson(QJsonDocument::Compact);
        
    const QString statusStr = doc.object()
                                  .value(QLatin1String("status")).toString().toLower().trimmed();
    if (statusStr != QLatin1String("active") &&
        statusStr != QLatin1String("locked") &&
        statusStr != QLatin1String("disabled")) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("status must be: active | locked | disabled");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    QString dbError;
    bool found = false;
    Model::UserInfo user = m_db->loadUser(username, found, dbError);
    if (!dbError.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }
    if (!found) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("User not found: %1").arg(username);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const Model::UserStatus newStatus = Model::userStatusFromString(statusStr);

    // active → locked/disabled 전환 시 마지막 admin 보호
    if (newStatus != Model::UserStatus::Active && user.role == Model::UserRole::Admin) {
        QString checkError;
        if (isLastAdmin(username, checkError)) {
            QJsonObject err;
            err[QLatin1String("error")] =
                QStringLiteral("Cannot change status of the last active admin.");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
        }
    }

    user.status = newStatus;

    // active 로 전환 시 실패 카운터·잠금 시각 초기화
    if (newStatus == Model::UserStatus::Active) {
        user.failedLoginCount = 0;
        user.lockedUntil      = QString();
    }
    // locked 로 수동 전환 시 만료 없음 (unlock 으로만 해제)
    if (newStatus == Model::UserStatus::Locked) {
        user.lockedUntil = QString();
    }

    if (!m_db->updateUser(user, dbError)) {
        Util::Logger::error(QStringLiteral("updateUser (status) failed: %1").arg(dbError));
        QJsonObject err;
        err[QLatin1String("error")] = dbError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("User status changed: %1 → %2").arg(username, statusStr));
    QJsonObject resp;
    resp[QLatin1String("username")] = username;
    resp[QLatin1String("status")]   = statusStr;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleGetLoginHistory(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QUrlQuery query(request.query());

    int limit = 100;
    if (query.hasQueryItem(QStringLiteral("limit"))) {
        bool ok = false;
        const int requested = query.queryItemValue(QStringLiteral("limit")).toInt(&ok);
        if (ok && requested > 0)
            limit = requested;
    }

    const QString username = query.queryItemValue(QStringLiteral("username"));

    QString error;
    const QList<Model::LoginHistoryEntry> entries =
        m_db->fetchLoginHistory(limit, username, error);

    if (!error.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QJsonArray arr;
    for (const Model::LoginHistoryEntry &e : entries) {
        QJsonObject obj;
        obj[QLatin1String("id")]        = e.id;
        obj[QLatin1String("timestamp")] = e.timestamp;
        obj[QLatin1String("username")]  = e.username;
        obj[QLatin1String("action")]    = e.action;
        obj[QLatin1String("result")]    = e.result;
        obj[QLatin1String("ip")]        = e.ip;
        arr.append(obj);
    }

    QJsonObject resp;
    resp[QLatin1String("history")] = arr;
    resp[QLatin1String("count")]   = arr.size();
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleDeleteLoginHistory(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QUrlQuery query(request.query());
    const QString username = query.queryItemValue(QStringLiteral("username"));

    QString error;
    if (!m_db->deleteLoginHistory(username, error)) {
        Util::Logger::error(QStringLiteral("deleteLoginHistory failed: %1").arg(error));
        QJsonObject err;
        err[QLatin1String("error")] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (username.isEmpty())
        Util::Logger::info(QStringLiteral("Login history cleared (all)."));
    else
        Util::Logger::info(QStringLiteral("Login history cleared: %1").arg(username));

    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

// ---------------------------------------------------------------------------
// System Config handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetConfig(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    // network
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

    // serial
    QJsonObject serial;
    serial[QLatin1String("device")]   = config.rs485.device;
    serial[QLatin1String("baudRate")] = config.rs485.baudRate;
    serial[QLatin1String("dataBits")] = config.rs485.dataBits;
    serial[QLatin1String("parity")]   = config.rs485.parity;
    serial[QLatin1String("stopBits")] = config.rs485.stopBits;

    // system
    QJsonObject sys;
    sys[QLatin1String("hostname")]  = config.system.hostname;
    sys[QLatin1String("ntpServer")] = config.system.ntpServer;

    // modbus server
    QJsonObject mbs;
    mbs[QLatin1String("enabled")] = config.modbusServer.enabled;
    mbs[QLatin1String("port")]    = config.modbusServer.port;
    mbs[QLatin1String("slaveId")] = config.modbusServer.slaveId;

    QJsonObject resp;
    resp[QLatin1String("network")]       = net;
    resp[QLatin1String("serial")]        = serial;
    resp[QLatin1String("system")]        = sys;
    resp[QLatin1String("modbusServer")]  = mbs;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostConfigReset(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const AppConfig defaults = factoryDefaultConfig();

    // Keep (version/revision/lastUpdate/specialCode)
    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));
    config.networkInterfaces = defaults.networkInterfaces;
    config.rs485             = defaults.rs485;
    config.system            = defaults.system;
    config.modbusServer      = defaults.modbusServer;

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("Config factory reset failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Config reset to factory defaults."));

    // network
    QJsonArray ifaces;
    for (const NetInterfaceConfig &iface : defaults.networkInterfaces) {
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

    // serial
    QJsonObject serial;
    serial[QLatin1String("device")]   = defaults.rs485.device;
    serial[QLatin1String("baudRate")] = defaults.rs485.baudRate;
    serial[QLatin1String("dataBits")] = defaults.rs485.dataBits;
    serial[QLatin1String("parity")]   = defaults.rs485.parity;
    serial[QLatin1String("stopBits")] = defaults.rs485.stopBits;

    // system
    QJsonObject sys;
    sys[QLatin1String("hostname")]  = defaults.system.hostname;
    sys[QLatin1String("ntpServer")] = defaults.system.ntpServer;

    // modbus server
    QJsonObject mbs;
    mbs[QLatin1String("enabled")] = defaults.modbusServer.enabled;
    mbs[QLatin1String("port")]    = defaults.modbusServer.port;
    mbs[QLatin1String("slaveId")] = defaults.modbusServer.slaveId;

    QJsonObject resp;
    resp[QLatin1String("network")]         = net;
    resp[QLatin1String("serial")]          = serial;
    resp[QLatin1String("system")]          = sys;
    resp[QLatin1String("modbusServer")]    = mbs;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutConfigNetwork(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonArray ifaces = doc.object().value(QLatin1String("interfaces")).toArray();
    if (ifaces.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("interfaces array is required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    config.networkInterfaces.clear();
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

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("saveConfig (network) failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Network config saved."));
    QJsonObject resp;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutConfigSerial(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    if (body.contains(QLatin1String("baudRate"))) config.rs485.baudRate = body.value(QLatin1String("baudRate")).toInt();
    if (body.contains(QLatin1String("dataBits"))) config.rs485.dataBits = body.value(QLatin1String("dataBits")).toInt();
    if (body.contains(QLatin1String("parity")))   config.rs485.parity   = body.value(QLatin1String("parity")).toString();
    if (body.contains(QLatin1String("stopBits"))) config.rs485.stopBits = body.value(QLatin1String("stopBits")).toInt();

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("saveConfig (serial) failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Serial config saved."));
    QJsonObject resp;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutConfigSystem(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    if (body.contains(QLatin1String("hostname")))  config.system.hostname  = body.value(QLatin1String("hostname")).toString();
    if (body.contains(QLatin1String("ntpServer"))) config.system.ntpServer = body.value(QLatin1String("ntpServer")).toString();

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("saveConfig (system) failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("System config saved."));
    QJsonObject resp;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutConfigModbusServer(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();

    if (body.contains(QLatin1String("port"))) {
        const int port = body.value(QLatin1String("port")).toInt();
        if (port < 1 || port > 65535) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("port must be 1–65535");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    }

    if (body.contains(QLatin1String("slaveId"))) {
        const int slaveId = body.value(QLatin1String("slaveId")).toInt();
        if (slaveId < 1 || slaveId > 247) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("slaveId must be 1–247");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
    }

    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    if (body.contains(QLatin1String("enabled"))) config.modbusServer.enabled = body.value(QLatin1String("enabled")).toBool();
    if (body.contains(QLatin1String("port")))    config.modbusServer.port    = static_cast<quint16>(body.value(QLatin1String("port")).toInt());
    if (body.contains(QLatin1String("slaveId"))) config.modbusServer.slaveId = body.value(QLatin1String("slaveId")).toInt();

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("saveConfig (modbus-server) failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Modbus server config saved."));
    QJsonObject resp;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleGetSystemInfo(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    struct statvfs st;
    if (::statvfs("/", &st) != 0) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Failed to read disk info");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    const qint64 blockSize = static_cast<qint64>(st.f_frsize);
    const qint64 free_b    = static_cast<qint64>(st.f_bfree)  * blockSize;
    const qint64 available = static_cast<qint64>(st.f_bavail) * blockSize;
    const qint64 used      = (static_cast<qint64>(st.f_blocks) * blockSize) - free_b;
    const qint64 total     = used + available;  // root 예약 블록 제외, df 기준 사용자 총량
    const double usedPct   = total > 0 ? (static_cast<double>(used) / total * 100.0) : 0.0;

    QJsonObject info;
    info[QLatin1String("ver")] = SR_VERSION;
    info[QLatin1String("rev")] = SR_REVISION;
    info[QLatin1String("zcode")] = SR_ZCODE;
    info[QLatin1String("lastUpdateDate")] = SR_LAST_UPDATE_DATE;

    QJsonObject disk;
    disk[QLatin1String("total")]       = total;
    disk[QLatin1String("used")]        = used;
    disk[QLatin1String("available")]   = available;
    disk[QLatin1String("usedPercent")] = qRound(usedPct * 10.0) / 10.0;

    QJsonObject resp;

    resp[QLatin1String("disk")] = disk;
    resp[QLatin1String("info")] = info;

    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostRestart(const QHttpServerRequest &request)
{
    if (!isAuthenticated(request))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    Util::Logger::info(QStringLiteral("System restart requested via API."));

    // 응답 전송 후 종료 (systemd Restart=always 또는 watchdog이 재시작)
    QTimer::singleShot(300, qApp, []() { QCoreApplication::exit(0); });

    QJsonObject resp;
    resp[QLatin1String("restarting")] = true;
    return QHttpServerResponse(resp);
}


// ---------------------------------------------------------------------------
// DB Write + DeviceList Sync helper
// ---------------------------------------------------------------------------
bool ApiServer::syncAddDevice(const Model::DeviceInfo &device, QString &error)
{
    if (!m_db->insertDevice(device, error))
        return false;

    // DB가 ID를 할당하므로 재로드해서 DeviceList 동기화
    const QList<Model::DeviceInfo> devices = m_db->loadDevices(error);
    if (!error.isEmpty()) return false;

    m_deviceList->reset(devices);
    return true;
}

bool ApiServer::syncUpdateDevice(const Model::DeviceInfo &device, QString &error)
{
    if (!m_db->updateDevice(device, error))
        return false;

    m_deviceList->update(device);
    return true;
}

bool ApiServer::syncDeleteDevice(int id, QString &error)
{
    if (!m_db->deleteDevice(id, error))
        return false;

    m_deviceList->remove(id);
    return true;
}

bool ApiServer::syncAddRegister(int deviceId, const Model::RegisterField &field, QString &error)
{
    if (!m_db->insertRegister(deviceId, field, error))
        return false;

    const QList<Model::RegisterField> fields = m_db->loadRegisters(deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(deviceId);
    device.registers = fields;
    m_deviceList->update(device);
    return true;
}

bool ApiServer::syncUpdateRegister(int deviceId, const Model::RegisterField &field, QString &error)
{
    if (!m_db->updateRegister(deviceId, field, error))
        return false;

    const QList<Model::RegisterField> fields = m_db->loadRegisters(deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(deviceId);
    device.registers = fields;
    m_deviceList->update(device);
    return true;
}

bool ApiServer::syncDeleteRegister(int deviceId, int address, const QString &type, QString &error)
{
    if (!m_db->deleteRegister(deviceId, address, type, error))
        return false;

    const QList<Model::RegisterField> fields = m_db->loadRegisters(deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(deviceId);
    device.registers = fields;
    m_deviceList->update(device);
    return true;
}


} // namespace Api
