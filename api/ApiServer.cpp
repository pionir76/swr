#include "ApiServer.h"

#include "../config/AppConfig.h"
#include "../config/SystemConfig.h"
#include "../data_collection/database/DeviceDatabase.h"
#include "../maintenance/BackupManager.h"
#include "../maintenance/RestoreManager.h"
#include "../data_collection/store/RegisterTable.h"
#include "../data_collection/store/DeviceList.h"
#include "../data_collection/polling/PollingManager.h"
#include "../utils/Logger.h"
#include "../utils/SystemMonitor.h"

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
                     Util::SystemMonitor *systemMonitor,
                     QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_registerTable(std::move(registerTable))
    , m_deviceList(std::move(deviceList))
    , m_pollingManager(pollingManager)
    , m_systemMonitor(systemMonitor)
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

    // Dashboard
    m_server.route("/api/dashboard", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetDashboard(req); });

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
    m_server.route("/api/registers/unified-id/check", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleCheckUnifiedId(req); });
    m_server.route("/api/registers/<arg>", QHttpServerRequest::Method::Put,
                   [this](const QString &id, const QHttpServerRequest &req) { return handlePutRegister(req, id); });
    m_server.route("/api/registers/<arg>", QHttpServerRequest::Method::Delete,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleDeleteRegister(req, id); });
    m_server.route("/api/registers/<arg>/write", QHttpServerRequest::Method::Post,
                   [this](const QString &id, const QHttpServerRequest &req) { return handleWriteRegister(req, id); });


    // realtime update.
    m_server.route("/api/registers/realtime", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetRealtime(req); });

    // logs
    m_server.route("/api/logs", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetLogs(req); });


    // User  : To Route order being alright Keep order 
    m_server.route("/api/users", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetUsers(req); });
    m_server.route("/api/users", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostUser(req); });
    m_server.route("/api/users/login-history", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetLoginHistory(req); });
    m_server.route("/api/users/login-history", QHttpServerRequest::Method::Delete,
                   [this](const QHttpServerRequest &req) { return handleDeleteLoginHistory(req); });
    m_server.route("/api/users/security-policy", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetSecurityPolicy(req); });
    m_server.route("/api/users/security-policy", QHttpServerRequest::Method::Put,
                   [this](const QHttpServerRequest &req) { return handlePutSecurityPolicy(req); });

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

    m_server.route("/api/maintenance/backup", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetBackup(req); });
    m_server.route("/api/maintenance/restore/validate", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleRestoreValidate(req); });
    m_server.route("/api/maintenance/restore/apply", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handleRestoreApply(req); });
    m_server.route("/api/maintenance/factory-reset", QHttpServerRequest::Method::Post,
                   [this](const QHttpServerRequest &req) { return handlePostFactoryReset(req); });
    m_server.route("/api/system/resources", QHttpServerRequest::Method::Get,
                   [this](const QHttpServerRequest &req) { return handleGetSystemResources(req); });
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
std::optional<QHttpServerResponse> ApiServer::requireAuth(const QHttpServerRequest &request, RequiredRole required) const
{
    const QByteArray auth = request.value("Authorization");

    if (auth.isEmpty()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QString token = QString::fromUtf8(auth).remove(QStringLiteral("Bearer ")).trimmed();

    QString username;
    {
        QMutexLocker locker(&m_sessionMutex);

        if (!m_sessions.contains(token)){
            return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
        }
        username = m_sessionUsers.value(token);
    }

    if (required == RequiredRole::Any)
        return std::nullopt;

    QString dbError;
    bool found = false;
    
    const Model::UserInfo user = m_db->loadUser(username, found, dbError);
    if (!found || !dbError.isEmpty())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);

    const bool isAdmin   = (user.role == Model::UserRole::Admin);
    const bool isManager = (user.role == Model::UserRole::Manager);

    if (required == RequiredRole::AdminOnly && !isAdmin) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Admin role required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }

    if (required == RequiredRole::ManagerOrAbove && !isAdmin && !isManager) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Manager or Admin role required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }

    return std::nullopt;
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

    if (!doc.isObject()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

    const QJsonObject body = doc.object();
    const QString username = body.value("username").toString();
    const QString password = body.value("password").toString();
    const QString ip       = request.remoteAddress().toString();

    const AppConfig cfg = loadConfig(QStringLiteral(SR_CONFIG_FILE));

    QString dbError;
    const Model::LoginResult result =
        m_db->validateUser(username, 
                        password, 
                        ip,
                        cfg.loginSecurity.maxFailedAttempts, 
                        dbError);

    using LR = Model::LoginResult;

    Model::LoginHistoryEntry hist;
    hist.username = username;
    hist.action   = QStringLiteral("login");
    hist.ip       = ip;

    switch (result) {
    case LR::Success: {
        hist.result = QStringLiteral("success");
        QString histError;
        m_db->insertLoginHistory(hist, histError);

        const QString token = createSession(username);
        Util::Logger::info(QStringLiteral("User logged in: %1 from %2").arg(username, ip));
        QJsonObject resp;
        resp[QLatin1String("token")] = token;
        return QHttpServerResponse(resp);
    }
    case LR::AccountDisabled: {
        hist.result = QStringLiteral("account_disabled");
        QString histError;
        m_db->insertLoginHistory(hist, histError);

        Util::Logger::warning(QStringLiteral("Login denied (disabled): %1 from %2").arg(username, ip));
        QJsonObject err;
        err[QLatin1String("error")]  = QStringLiteral("Account is disabled.");
        err[QLatin1String("reason")] = QStringLiteral("disabled");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }
    case LR::AccountLocked: {
        hist.result = QStringLiteral("account_locked");
        QString histError;
        m_db->insertLoginHistory(hist, histError);

        Util::Logger::warning(QStringLiteral("Login denied (locked): %1 from %2").arg(username, ip));
        QJsonObject err;
        err[QLatin1String("error")]  = QStringLiteral("Account is locked. Contact administrator.");
        err[QLatin1String("reason")] = QStringLiteral("locked");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }
    case LR::AccountJustLocked: {
        hist.result = QStringLiteral("account_locked");
        QString histError;
        m_db->insertLoginHistory(hist, histError);

        Util::Logger::warning(QStringLiteral("Account locked: %1 from %2 (max attempts reached)").arg(username, ip));
        QJsonObject err;
        err[QLatin1String("error")]  = QStringLiteral("Account is locked. Contact administrator.");
        err[QLatin1String("reason")] = QStringLiteral("locked");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }
    default: {
        hist.result = QStringLiteral("invalid_password");
        QString histError;
        m_db->insertLoginHistory(hist, histError);

        Util::Logger::warning(QStringLiteral("Login failed (bad credentials): %1 from %2").arg(username, ip));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }
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
    if (auto err = requireAuth(request)) return std::move(*err);

    QJsonObject resp;
    resp["valid"] = true;
    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// Polling Control Handler
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Dashboard — unified snapshot for the dashboard page
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetDashboard(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    static constexpr int kDashAlertLimit    = 20;
    static constexpr int kDashLogLimit      = 20;
    static constexpr int kDashRealtimeLimit = 20;

    // ── 1. Devices + KPI ────────────────────────────────────────────────────
    const QList<Model::DeviceInfo> devices = m_deviceList->getAll();

    int okCount      = 0;
    int errorCount   = 0;
    int unknownCount = 0;

    QJsonArray devArr;
    for (const Model::DeviceInfo &d : devices) {
        QString stateStr;
        switch (d.status.state) {
        case Model::DeviceInfo::Status::State::Ok:
            stateStr = QStringLiteral("ok");
            ++okCount;
            break;
        case Model::DeviceInfo::Status::State::Error:
            stateStr = QStringLiteral("error");
            ++errorCount;
            break;
        default:
            stateStr = QStringLiteral("unknown");
            ++unknownCount;
            break;
        }

        QJsonObject obj;
        obj["id"]               = d.id;
        obj["name"]             = d.name;
        obj["deviceCode"]       = d.deviceCode;
        obj["connType"]         = Model::connectionTypeToString(d.connection.type);
        obj["protocol"]         = Model::protocolToString(d.connection.protocol);
        obj["state"]            = stateStr;
        obj["lastPollDurationMs"] = d.status.lastPollDurationMs;
        obj["consecutiveErrors"]  = d.status.consecutiveErrors;
        obj["lastError"]          = d.status.lastError;
        devArr.append(obj);
    }

    // ── 2. Alerts — WARN + ERROR logs, newest first ─────────────────────────
    QString alertErr;
    QList<Util::LogEntry> alertEntries =
        Util::Logger::fetch(kDashAlertLimit, 0, QStringLiteral("WARN"),  {}, {}, alertErr);
    alertEntries +=
        Util::Logger::fetch(kDashAlertLimit, 0, QStringLiteral("ERROR"), {}, {}, alertErr);
    std::sort(alertEntries.begin(), alertEntries.end(),
              [](const Util::LogEntry &a, const Util::LogEntry &b) { return a.id > b.id; });
    if (alertEntries.size() > kDashAlertLimit)
        alertEntries.resize(kDashAlertLimit);

    QJsonArray alertArr;
    for (const Util::LogEntry &e : alertEntries) {
        QJsonObject obj;
        obj["id"]        = e.id;
        obj["timestamp"] = e.timestamp.toString(Qt::ISODate);
        obj["level"]     = Util::logLevelToString(e.level);
        obj["message"]   = e.message;
        alertArr.append(obj);
    }

    // ── 3. Realtime registers (top N, newest by lastUpdated) ────────────────
    const QList<Model::RegisterState> allRegs = m_registerTable->states();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    QJsonArray realtimeArr;
    int realtimeCount = 0;
    for (const Model::RegisterState &r : allRegs) {
        if (realtimeCount >= kDashRealtimeLimit)
            break;

        Model::DataQuality quality = Model::DataQuality::Bad;
        if (r.lastUpdated.isValid() && r.pollingIntervalMs > 0) {
            const qint64 elapsedMs = r.lastUpdated.msecsTo(now);
            if (elapsedMs < r.pollingIntervalMs * 3 / 2)
                quality = Model::DataQuality::Good;
            else if (elapsedMs < r.pollingIntervalMs * 3)
                quality = Model::DataQuality::Normal;
        }

        QJsonObject obj;
        obj["id"]           = r.config.unifiedAddress;
        obj["tagName"]      = r.config.tagName;
        obj["displayName"]  = r.config.displayName;
        obj["deviceId"]     = r.config.deviceId;
        obj["localAddress"] = r.config.localAddress;
        obj["sourceType"]   = Model::registerTypeToString(r.config.type);
        obj["unit"]         = r.config.unit;
        obj["scaledValue"]  = r.scaledValue;
        obj["scale"]        = r.config.scale;
        obj["rawWord"]      = r.rawRegisters.isEmpty() ? 0 : static_cast<int>(r.rawRegisters.first());
        obj["bitLabels"]    = r.config.bitLabels;
        obj["readOnly"]     = r.config.readOnly;
        obj["isValid"]      = r.isValid;
        obj["outOfRange"]   = r.outOfRange;
        obj["quality"]      = Model::dataQualityToString(quality);
        obj["lastUpdated"]  = r.lastUpdated.toString(Qt::ISODate);
        obj["errorMessage"] = r.errorMessage;
        realtimeArr.append(obj);
        ++realtimeCount;
    }

    // ── 4. System logs — INFO only, newest first ────────────────────────────
    QString logErr;
    const QList<Util::LogEntry> logEntries =
        Util::Logger::fetch(kDashLogLimit, 0, QStringLiteral("INFO"), {}, {}, logErr);

    QJsonArray logArr;
    for (const Util::LogEntry &e : logEntries) {
        QJsonObject obj;
        obj["id"]        = e.id;
        obj["timestamp"] = e.timestamp.toString(Qt::ISODate);
        obj["level"]     = Util::logLevelToString(e.level);
        obj["message"]   = e.message;
        logArr.append(obj);
    }

    // ── 5. Compose response ─────────────────────────────────────────────────
    QJsonObject kpi;
    kpi["totalDevices"]   = devices.size();
    kpi["okDevices"]      = okCount;
    kpi["errorDevices"]   = errorCount;
    kpi["unknownDevices"] = unknownCount;

    QJsonObject polling;
    polling["running"] = m_pollingManager->isRunning();

    QJsonObject resp;
    resp["polling"]           = polling;
    resp["kpi"]               = kpi;
    resp["devices"]           = devArr;
    resp["realtimeRegisters"] = realtimeArr;
    resp["alerts"]            = alertArr;
    resp["logs"]              = logArr;

    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleGetPollingStatus(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    QJsonObject resp;
    resp["running"] = m_pollingManager->isRunning();
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleStartPolling(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)){
        return std::move(*err);
    }

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    // We can't modify device list while polling data.
    if (rejectIfPolling(conflict)){
        return conflict;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());

    if (!doc.isObject()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
        device.connection.type = Model::connectionTypeFromString(body.value("connType").toString().toLower());

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);

    if (rejectIfPolling(conflict)) 
        return conflict;

    QString error;
    if (!syncDeleteDevice(id.toInt(), error)) {
        Util::Logger::error(QStringLiteral("deleteDevice failed: %1").arg(error));
        return QHttpServerResponse(QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Device deleted: id=%1").arg(id));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

// ---------------------------------------------------------------------------
// Register Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetRegisters(const QHttpServerRequest &request,
                                                  const QString &deviceId)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    Model::DeviceInfo device;
    try {
        device = m_deviceList->get(deviceId.toInt());
    } catch (const std::out_of_range &) {
        QJsonObject err;
        err["error"] = QStringLiteral("Device not found");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    QJsonArray arr;
    for (const Model::RegisterConfig &f : device.registers) {
        QJsonObject obj;
        obj["id"]          = f.id;
        obj["tagName"]     = f.tagName;
        obj["displayName"] = f.displayName;
        obj["localAddress"]    = f.localAddress;
        obj["type"]            = Model::registerTypeToString(f.type);
        obj["readOnly"]        = f.readOnly;
        obj["length"]          = f.length;
        obj["unit"]            = f.unit;
        obj["scale"]           = f.scale;
        obj["isSigned"]        = f.isSigned;
        obj["bitLabels"]       = f.bitLabels;
        obj["minValue"]        = f.minValue;
        obj["maxValue"]        = f.maxValue;
        obj["byteOrder"]       = Model::byteOrderToString(f.byteOrder);
        obj["unifiedAddress"]  = f.unifiedAddress;

        arr.append(obj);
    }

    QJsonObject resp;
    resp["registers"] = arr;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostRegister(const QHttpServerRequest &request,
                                                  const QString &deviceId)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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

    Model::RegisterConfig config;
    config.tagName     = tagName;
    config.displayName = body.value("displayName").toString(tagName);
    config.localAddress = body.value("localAddress").toInt(0);
    config.type        = Model::registerTypeFromString(typeStr);
    config.readOnly    = body.value("readOnly").toBool(false);
    config.length      = body.value("length").toInt(1);
    config.unit        = body.value("unit").toString();
    config.scale       = body.value("scale").toDouble(1.0);
    config.isSigned    = body.value("isSigned").toBool(false);
    config.bitLabels   = body.value("bitLabels").toString();

    if (body.contains("byteOrder"))
        config.byteOrder = Model::byteOrderFromString(body.value("byteOrder").toString().toLower());

    if (body.contains("minValue"))
        config.minValue = body.value("minValue").toDouble();

    if (body.contains("maxValue"))
        config.maxValue = body.value("maxValue").toDouble();

    if (body.contains("unifiedAddress")) {
        const int uid = body.value("unifiedAddress").toInt(-1);
        if (uid >= 0 && uid < Model::kManualUnifiedIdMin) {
            QJsonObject err;
            err["error"] = QStringLiteral("Manual unifiedAddress must be >= %1").arg(Model::kManualUnifiedIdMin);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        config.unifiedAddress = uid;
    }

    if (config.type == Model::RegisterType::Unknown) {
        QJsonObject err;
        err["error"] = QStringLiteral("Unknown register type: %1").arg(typeStr);

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const int devId = deviceId.toInt();
    QString error;
    if (!syncAddRegister(devId, config, error)) {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
            QJsonObject err;
            err["error"] = QStringLiteral("unifiedAddress already in use");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
        }
        Util::Logger::error(QStringLiteral("addRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register added: device=%1 tag=%2").arg(devId).arg(tagName));

    // return the created register ID info in the response
    QJsonObject resp;
    resp["tagName"]        = config.tagName;
    resp["localAddress"]   = config.localAddress;
    resp["type"]           = Model::registerTypeToString(config.type);
    resp["unifiedAddress"] = config.unifiedAddress;

    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Created);
}

QHttpServerResponse ApiServer::handlePutRegister(const QHttpServerRequest &request,
                                                 const QString &registerId)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);
    if (rejectIfPolling(conflict))
        return conflict;

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    const QString tagName = body.value("tagName").toString().trimmed();
    if (tagName.isEmpty()) {
        QJsonObject err;
        err["error"] = QStringLiteral("tagName is required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    bool found = false;
    const Model::RegisterConfig existing = m_deviceList->findByRegisterId(registerId.toInt(), found);
    if (!found) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register not found: id=%1").arg(registerId);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    const QString typeStr = body.value("type").toString(Model::registerTypeToString(existing.type)).trimmed();
    const Model::RegisterType type = Model::registerTypeFromString(typeStr);
    if (type == Model::RegisterType::Unknown) {
        QJsonObject err;
        err["error"] = QStringLiteral("Unknown register type: %1").arg(typeStr);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    Model::RegisterConfig config;
    config.id                = existing.id;
    config.deviceId          = existing.deviceId;
    config.tagName           = tagName;
    config.displayName       = body.value("displayName").toString(tagName);
    config.localAddress      = body.value("localAddress").toInt(existing.localAddress);
    config.type              = type;
    config.readOnly          = body.value("readOnly").toBool(existing.readOnly);
    config.length            = body.value("length").toInt(existing.length);
    config.unit              = body.value("unit").toString(existing.unit);
    config.scale             = body.value("scale").toDouble(existing.scale);
    config.isSigned          = body.value("isSigned").toBool(existing.isSigned);
    config.bitLabels         = body.value("bitLabels").toString(existing.bitLabels);
    config.byteOrder         = body.contains("byteOrder")
                                  ? Model::byteOrderFromString(body.value("byteOrder").toString().toLower())
                                  : existing.byteOrder;
    config.minValue          = body.contains("minValue") ? body.value("minValue").toDouble() : existing.minValue;
    config.maxValue          = body.contains("maxValue") ? body.value("maxValue").toDouble() : existing.maxValue;

    if (body.contains("unifiedAddress")) {
        const int uid = body.value("unifiedAddress").toInt(existing.unifiedAddress);
        if (uid >= 0 && uid < Model::kManualUnifiedIdMin) {
            QJsonObject err;
            err["error"] = QStringLiteral("Manual unifiedAddress must be >= %1").arg(Model::kManualUnifiedIdMin);
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        config.unifiedAddress = uid;
    } else {
        config.unifiedAddress = existing.unifiedAddress;
    }

    QString error;
    if (!syncUpdateRegister(config, error)) {
        if (error.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
            QJsonObject err;
            err["error"] = QStringLiteral("unifiedAddress already in use");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
        }
        Util::Logger::error(QStringLiteral("updateRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register updated: id=%1 tag=%2").arg(registerId).arg(tagName));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleDeleteRegister(const QHttpServerRequest &request,
                                                    const QString &registerId)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    QHttpServerResponse conflict(QHttpServerResponse::StatusCode::Ok);
    if (rejectIfPolling(conflict))
        return conflict;

    QString error;
    if (!syncDeleteRegister(registerId.toInt(), error)) {
        Util::Logger::error(QStringLiteral("deleteRegister failed: %1").arg(error));
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Register deleted: id=%1").arg(registerId));
    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleCheckUnifiedId(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    const QUrlQuery query(request.query());
    bool ok = false;
    const int unifiedId = query.queryItemValue(QStringLiteral("id")).toInt(&ok);

    if (!ok || unifiedId < 0) {
        QJsonObject err;
        err["error"] = QStringLiteral("id query parameter is required and must be a non-negative integer");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    bool found = false;
    m_deviceList->findByUnifiedAddress(unifiedId, found);

    QJsonObject resp;
    resp["available"] = !found;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleWriteRegister(const QHttpServerRequest &request,
                                                   const QString &registerId)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    if (!body.contains("rawValues")) {
        QJsonObject err;
        err["error"] = QStringLiteral("rawValues is required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    bool found = false;
    const Model::RegisterConfig reg = m_deviceList->findByRegisterId(registerId.toInt(), found);
    if (!found) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register not found: id=%1").arg(registerId);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    if (reg.readOnly) {
        QJsonObject err;
        err["error"] = QStringLiteral("Register is read-only: %1").arg(reg.tagName);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const QJsonArray rawArr = body.value("rawValues").toArray();
    QVector<quint16> rawValues;
    for (const QJsonValue &v : rawArr)
        rawValues.append(static_cast<quint16>(v.toInt()));

    Model::WriteRequest req;
    req.config    = reg;
    req.rawValues = rawValues;

    if (reg.type == Model::RegisterType::Coil) {
        for (quint16 v : rawValues)
            req.coilValues.append(v != 0);
    }

    m_deviceList->enqueueWrite(reg.deviceId, std::move(req));

    Util::Logger::info(QStringLiteral("Write enqueued: id=%1 tag=%2").arg(registerId).arg(reg.tagName));
    QJsonObject resp;
    resp["ok"] = true;
    return QHttpServerResponse(resp, QHttpServerResponse::StatusCode::Accepted);
}

// ---------------------------------------------------------------------------
// Real Time Update.
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetRealtime(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    const QList<Model::RegisterState> regs = m_registerTable->states();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonArray arr;
    for (const Model::RegisterState &r : regs) {

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

        obj["id"]           = r.config.unifiedAddress;
        obj["registerId"]   = r.config.id;
        obj["tagName"]      = r.config.tagName;
        obj["displayName"]  = r.config.displayName;
        obj["deviceId"]     = r.config.deviceId;
        obj["localAddress"] = r.config.localAddress;
        obj["sourceType"]   = Model::registerTypeToString(r.config.type);
        obj["unit"]         = r.config.unit;
        obj["scaledValue"]  = r.scaledValue;
        obj["minValue"]     = r.config.minValue;
        obj["maxValue"]     = r.config.maxValue;
        obj["scale"]        = r.config.scale;
        obj["rawWord"]      = r.rawRegisters.isEmpty() ? 0 : static_cast<int>(r.rawRegisters.first());
        obj["bitLabels"]    = r.config.bitLabels;
        obj["readOnly"]     = r.config.readOnly;
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
// GET /api/logs?limit=20&offset=0  → 1페이지 (0~19)
// GET /api/logs?limit=20&offset=20 → 2페이지 (20~39)
// GET /api/logs?level=WARN         → 레벨 필터
// GET /api/logs?from=2026-06-01&to=2026-06-30 → 기간 필터
// 파라미터 없으면 → "level": "ALL", "limit": 1000, "offset": 0

QHttpServerResponse ApiServer::handleGetLogs(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    const QUrlQuery query(request.query());

    int limit = Util::Logger::maxLogRows();
    if (query.hasQueryItem(QStringLiteral("limit"))) {
        bool ok = false;
        const int requested = query.queryItemValue(QStringLiteral("limit")).toInt(&ok);
        if (ok && requested > 0)
            limit = requested;
    }

    int offset = 0;
    if (query.hasQueryItem(QStringLiteral("offset"))) {
        bool ok = false;
        const int requested = query.queryItemValue(QStringLiteral("offset")).toInt(&ok);
        if (ok && requested >= 0)
            offset = requested;
    }

    const QString level = query.queryItemValue(QStringLiteral("level")).toUpper();
    const QString from  = query.queryItemValue(QStringLiteral("from"));
    const QString to    = query.queryItemValue(QStringLiteral("to"));

    QString error;
    const QList<Util::LogEntry> entries =
        Util::Logger::fetch(limit, offset, level, from, to, error);

    if (!error.isEmpty()) {
        QJsonObject body;
        body["error"] = error;
        return QHttpServerResponse(body, QHttpServerResponse::StatusCode::InternalServerError);
    }

    QString countErr;
    const qint64 total = Util::Logger::count(level, from, to, countErr);

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
    resp["logs"]   = arr;
    resp["count"]  = arr.size();
    resp["total"]  = total;
    resp["limit"]  = limit;
    resp["offset"] = offset;
    resp["level"]  = level.isEmpty() ? QStringLiteral("ALL") : level;
    resp["from"]   = from;
    resp["to"]     = to;

    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// User Handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetUsers(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();
    
    const QString username    = body.value("username").toString().trimmed();
    const QString displayName = body.value("displayName").toString(username);
    const QString password    = body.value("password").toString();
    const QString roleStr     = body.value("role").toString().toLower().trimmed();

    // Validate required fields
    if (username.isEmpty() || password.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("username and password are required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // Validate password length
    const int minPwLen = SystemConfig::config().loginSecurity.minPasswordLength;
    if (password.length() < minPwLen) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("password must be at least %1 characters").arg(minPwLen);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // Validate role
    if (roleStr != QLatin1String("user") &&
        roleStr != QLatin1String("manager") &&
        roleStr != QLatin1String("admin")) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("role is required: user | manager | admin");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    // Check if username already exists
    QString error;
    bool exists = false;
    m_db->loadUser(username, exists, error);

    if (!error.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }
    if (exists) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Username already exists: %1").arg(username);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
    }

    Model::UserInfo user;
    user.username     = username;
    user.displayName  = displayName;
    user.description  = body.value("description").toString();
    user.passwordHash = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
    user.role         = Model::userRoleFromString(roleStr);
    user.status       = Model::UserStatus::Active;

    // Insert the new user into the database
    if (!m_db->insertUser(user, error)) {
        Util::Logger::error(QStringLiteral("insertUser failed: %1").arg(error));

        QJsonObject err;
        err[QLatin1String("error")] = error;

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    QString error;
    bool found = false;

    const Model::UserInfo user = m_db->loadUser(username, found, error);
    
    if (!error.isEmpty()) {
        QJsonObject err;
        err["error"] = error;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (!found) {
        QJsonObject err;
        err["error"] = QStringLiteral("User not found: %1").arg(username);
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::NotFound);
    }

    //-------------------------------------------------------------------------------//
    // Prevent deletion of the admin account ID 0, which is the default superuser.
    //-------------------------------------------------------------------------------//
    if (user.id == 0) {
        QJsonObject err;
        err["error"] = QStringLiteral("admin 계정은 삭제할 수 없습니다.");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }

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


QHttpServerResponse ApiServer::handlePutUser(const QHttpServerRequest &request,
                                              const QString &username)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());

    if (!doc.isObject()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

    QString dbError;
    bool found = false;
    Model::UserInfo user = m_db->loadUser(username, found, dbError);
    
    if (!dbError.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = dbError;

        return QHttpServerResponse(
            err, 
            QHttpServerResponse::StatusCode::InternalServerError);
    }

    if (!found) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("User not found: %1").arg(username);

        return QHttpServerResponse(
            err,
            QHttpServerResponse::StatusCode::NotFound);
    }

    // Prevent modification of the default admin account (ID 0)
    if (user.id == 0) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Default admin account cannot be modified.");
        
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
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

        user.role = Model::userRoleFromString(roleStr);
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
    if (auto err = requireAuth(request)) return std::move(*err);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());

    if (!doc.isObject()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);
    }

    const QJsonObject body = doc.object();
    const QString newPassword = body.value(QLatin1String("newPassword")).toString();
    
    // Validate new password length
    const int minPwLen = SystemConfig::config().loginSecurity.minPasswordLength;
    if (newPassword.length() < minPwLen) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("newPassword must be at least %1 characters").arg(minPwLen);

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const QString caller = sessionUsername(request);
    QString dbError;
    bool found = false;
    
    Model::UserInfo callerInfo = m_db->loadUser(caller, found, dbError);
    if (!found || !dbError.isEmpty()){
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Unauthorized);
    }

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

    //--------------------------------------------------------------------------//
    // Only allow the user themselves or an admin to change the password
    //--------------------------------------------------------------------------//
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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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

    //---------------------------------------------------------------------------//
    // Prevent changing the status of the default admin account (ID 0)
    //---------------------------------------------------------------------------//
    if (user.id == 0) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("admin 계정의 status는 변경할 수 없습니다.");

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Forbidden);
    }

    const QString statusStr = doc.object()
                                  .value(QLatin1String("status")).toString().toLower().trimmed();

    if (statusStr != QLatin1String("active") &&
        statusStr != QLatin1String("locked") &&
        statusStr != QLatin1String("disabled")) {

        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("status must be: active | locked | disabled");

        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const Model::UserStatus newStatus = Model::userStatusFromString(statusStr);

    user.status = newStatus;

    //---------------------------------------------------------------------------//
    // Reset failedLoginCount to 0 when the status is set to Active
    //---------------------------------------------------------------------------//
    if (newStatus == Model::UserStatus::Active)
        user.failedLoginCount = 0;

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const QUrlQuery query(request.query());
    const QString username = query.queryItemValue(QStringLiteral("username"));

    QString error;
    if (!m_db->deleteLoginHistory(username, error)) {
        Util::Logger::error(QStringLiteral("deleteLoginHistory failed: %1").arg(error));

        QJsonObject err;
        err[QLatin1String("error")] = error;
        
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    // Empty username means all history. Log accordingly.
    if (username.isEmpty())
        Util::Logger::info(QStringLiteral("Login history cleared (all)."));
    else
        Util::Logger::info(QStringLiteral("Login history cleared: %1").arg(username));

    return QHttpServerResponse(QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse ApiServer::handleGetSecurityPolicy(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const LoginSecurityConfig &ls = SystemConfig::config().loginSecurity;

    QJsonObject resp;
    resp[QLatin1String("maxFailedAttempts")]     = ls.maxFailedAttempts;
    resp[QLatin1String("sessionTimeoutMinutes")] = ls.sessionTimeoutMinutes;
    resp[QLatin1String("minPasswordLength")]     = ls.minPasswordLength;
    resp[QLatin1String("autoLogout")]            = ls.autoLogout;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePutSecurityPolicy(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject())
        return QHttpServerResponse(QHttpServerResponse::StatusCode::BadRequest);

    const QJsonObject body = doc.object();

    AppConfig config = loadConfig(QStringLiteral(SR_CONFIG_FILE));
    LoginSecurityConfig &ls = config.loginSecurity;

    if (body.contains(QLatin1String("maxFailedAttempts"))) {
        const int v = body.value(QLatin1String("maxFailedAttempts")).toInt();
        if (v < 1 || v > 20) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("maxFailedAttempts must be 1–20");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        ls.maxFailedAttempts = v;
    }
    if (body.contains(QLatin1String("sessionTimeoutMinutes"))) {
        const int v = body.value(QLatin1String("sessionTimeoutMinutes")).toInt();
        if (v < 1 || v > 1440) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("sessionTimeoutMinutes must be 1–1440");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        ls.sessionTimeoutMinutes = v;
    }
    if (body.contains(QLatin1String("minPasswordLength"))) {
        const int v = body.value(QLatin1String("minPasswordLength")).toInt();
        if (v < 4 || v > 32) {
            QJsonObject err;
            err[QLatin1String("error")] = QStringLiteral("minPasswordLength must be 4–32");
            return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
        }
        ls.minPasswordLength = v;
    }
    if (body.contains(QLatin1String("autoLogout")))
        ls.autoLogout = body.value(QLatin1String("autoLogout")).toBool();

    QString saveError;
    if (!saveConfig(QStringLiteral(SR_CONFIG_FILE), config, saveError)) {
        Util::Logger::error(QStringLiteral("saveConfig (security-policy) failed: %1").arg(saveError));
        QJsonObject err;
        err[QLatin1String("error")] = saveError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(QStringLiteral("Login security policy updated."));

    QJsonObject resp;
    resp[QLatin1String("maxFailedAttempts")]     = ls.maxFailedAttempts;
    resp[QLatin1String("sessionTimeoutMinutes")] = ls.sessionTimeoutMinutes;
    resp[QLatin1String("minPasswordLength")]     = ls.minPasswordLength;
    resp[QLatin1String("autoLogout")]            = ls.autoLogout;
    return QHttpServerResponse(resp);
}

// ---------------------------------------------------------------------------
// System Config handler
// ---------------------------------------------------------------------------
QHttpServerResponse ApiServer::handleGetConfig(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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
    if (auto err = requireAuth(request)) return std::move(*err);

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
    info[QLatin1String("ver")]            = SR_VERSION;
    info[QLatin1String("rev")]            = SR_REVISION;
    info[QLatin1String("zcode")]          = SR_ZCODE;
    info[QLatin1String("schemaVersion")]  = SR_SCHEMA_VERSION;
    info[QLatin1String("lastUpdateDate")] = SR_LAST_UPDATE_DATE;

    QJsonObject disk;
    disk[QLatin1String("total")]       = total;
    disk[QLatin1String("used")]        = used;
    disk[QLatin1String("available")]   = available;
    disk[QLatin1String("usedPercent")] = qRound(usedPct * 10.0) / 10.0;

    // summary
    const QList<Model::DeviceInfo> devices = m_deviceList->getAll();
    int registerCount = 0;
    for (const Model::DeviceInfo &d : devices)
        registerCount += d.registers.size();

    QString usrErr;
    const QList<Model::UserInfo> users = m_db->loadUsers(usrErr);

    QString logErr;
    const qint64 logTotal = Util::Logger::count({}, {}, {}, logErr);

    QJsonObject summary;
    summary[QLatin1String("deviceCount")]   = devices.size();
    summary[QLatin1String("registerCount")] = registerCount;
    summary[QLatin1String("userCount")]     = static_cast<int>(users.size());
    summary[QLatin1String("logCount")]      = logTotal;
    summary[QLatin1String("pollingActive")] = m_pollingManager->isRunning();

    const Util::SystemResources res = m_systemMonitor->resources();
    const AppConfig cfg = loadConfig(QStringLiteral(SR_CONFIG_FILE));
    QJsonObject ntp;
    ntp[QLatin1String("server")]     = cfg.system.ntpServer;
    ntp[QLatin1String("synced")]     = res.ntp.synced;
    ntp[QLatin1String("maxErrorMs")] = res.ntp.maxErrorMs;

    QJsonObject resp;
    resp[QLatin1String("info")]    = info;
    resp[QLatin1String("disk")]    = disk;
    resp[QLatin1String("summary")] = summary;
    resp[QLatin1String("ntp")]     = ntp;

    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostRestart(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::ManagerOrAbove)) return std::move(*err);

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

bool ApiServer::syncAddRegister(int deviceId, Model::RegisterConfig &config, QString &error)
{
    if (!m_db->insertRegister(deviceId, config, error))
        return false;

    const QList<Model::RegisterConfig> configs = m_db->loadRegisters(deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(deviceId);
    device.registers = configs;
    m_deviceList->update(device);

    return true;
}

bool ApiServer::syncUpdateRegister(Model::RegisterConfig config, QString &error)
{
    if (!m_db->updateRegister(config, error))
        return false;

    const QList<Model::RegisterConfig> configs = m_db->loadRegisters(config.deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(config.deviceId);
    device.registers = configs;
    m_deviceList->update(device);
    
    return true;
}

bool ApiServer::syncDeleteRegister(int registerId, QString &error)
{
    bool found = false;
    const Model::RegisterConfig reg = m_deviceList->findByRegisterId(registerId, found);

    if (!found) {
        error = QStringLiteral("Register not found: id=%1").arg(registerId);
        return false;
    }
    const int deviceId = reg.deviceId;

    if (!m_db->deleteRegister(registerId, error))
        return false;

    const QList<Model::RegisterConfig> configs = m_db->loadRegisters(deviceId, error);
    if (!error.isEmpty()) return false;

    Model::DeviceInfo device = m_deviceList->get(deviceId);
    device.registers = configs;
    m_deviceList->update(device);
    
    return true;
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

QHttpServerResponse ApiServer::handleGetBackup(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::AdminOnly)) return std::move(*err);

    const QString caller = sessionUsername(request);

    QString backupError;
    QByteArray zipData = Maintenance::BackupManager::create(m_db, backupError);

    if (zipData.isEmpty()) {
        Util::Logger::error(QStringLiteral("Backup creation failed: %1").arg(backupError));

        QJsonObject err;
        err[QLatin1String("error")] = backupError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    const QString filename = QStringLiteral("swr_backup_%1.zip")
                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    Util::Logger::info(QStringLiteral("Backup created by %1: %2 bytes").arg(caller).arg(zipData.size()));

    QHttpServerResponse resp(QByteArrayLiteral("application/zip"), std::move(zipData));
    QHttpHeaders hdrs = resp.headers();

    hdrs.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
                QStringLiteral("attachment; filename=\"%1\"").arg(filename));
    resp.setHeaders(std::move(hdrs));
    return resp;
}

QHttpServerResponse ApiServer::handleRestoreValidate(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::AdminOnly)) return std::move(*err);

    const QString caller = sessionUsername(request);

    const QByteArray zipData = request.body();
    if (zipData.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Empty body");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    Util::Logger::info(
        QStringLiteral("Restore validate requested by %1: %2 bytes")
        .arg(caller).arg(zipData.size()));

    QString validateError;
    const Maintenance::RestorePreview preview =
        Maintenance::RestoreManager::validate(zipData, validateError);

    if (!preview.valid) {
        QJsonObject err;
        err[QLatin1String("error")] = validateError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::UnprocessableEntity);
    }

    auto itemToJson = [](const Maintenance::RestoreItemInfo &item) {
        QJsonObject obj;
        obj[QLatin1String("available")] = item.available;
        obj[QLatin1String("count")]     = item.count;
        obj[QLatin1String("warning")]   = item.warning;
        return obj;
    };

    QJsonObject backupInfo;
    backupInfo[QLatin1String("product")]       = preview.backupInfo.product;
    backupInfo[QLatin1String("createdAt")]     = preview.backupInfo.createdAt;
    backupInfo[QLatin1String("hostname")]      = preview.backupInfo.hostname;
    backupInfo[QLatin1String("version")]       = preview.backupInfo.version;
    backupInfo[QLatin1String("revision")]      = preview.backupInfo.revision;
    backupInfo[QLatin1String("zcode")]         = preview.backupInfo.zcode;
    backupInfo[QLatin1String("schemaVersion")] = preview.backupInfo.schemaVersion;

    QJsonArray warnings;
    for (const QString &w : preview.warnings)
        warnings.append(w);

    QJsonObject resp;
    resp[QLatin1String("restoreId")]  = preview.restoreId;
    resp[QLatin1String("backupInfo")] = backupInfo;
    resp[QLatin1String("config")]     = itemToJson(preview.config);
    resp[QLatin1String("devices")]    = itemToJson(preview.devices);
    resp[QLatin1String("registers")]  = itemToJson(preview.registers);
    resp[QLatin1String("users")]      = itemToJson(preview.users);
    resp[QLatin1String("hmi")]        = itemToJson(preview.hmi);
    resp[QLatin1String("warnings")]   = warnings;

    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleRestoreApply(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::AdminOnly)) return std::move(*err);

    const QString caller = sessionUsername(request);

    QHttpServerResponse pollingCheck(QHttpServerResponse::StatusCode::Ok);
    if (rejectIfPolling(pollingCheck))
        return pollingCheck;

    const QJsonDocument doc = QJsonDocument::fromJson(request.body());
    if (!doc.isObject()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Invalid JSON body");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }
    const QJsonObject body = doc.object();

    const QString restoreId = body[QLatin1String("restoreId")].toString();
    if (restoreId.isEmpty()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("restoreId required");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::BadRequest);
    }

    const QJsonObject optJson = body[QLatin1String("options")].toObject();
    Maintenance::RestoreOptions options;
    options.config    = optJson[QLatin1String("config")].toBool(true);
    options.network   = optJson[QLatin1String("network")].toBool(false);
    options.devices   = optJson[QLatin1String("devices")].toBool(true);
    options.registers = optJson[QLatin1String("registers")].toBool(true);
    options.users     = optJson[QLatin1String("users")].toBool(true);
    options.hmi       = optJson[QLatin1String("hmi")].toBool(false);

    bool restartRequired = false;
    QString applyError;
    const bool ok = Maintenance::RestoreManager::apply(
        restoreId, options, m_db, m_pollingManager, restartRequired, applyError);

    if (!ok) {
        QJsonObject err;
        err[QLatin1String("error")] = applyError;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    Util::Logger::info(
        QStringLiteral("Restore applied by %1").arg(caller));

    QJsonObject resp;
    resp[QLatin1String("ok")]              = true;
    resp[QLatin1String("restartRequired")] = restartRequired;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handlePostFactoryReset(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request, RequiredRole::AdminOnly)) return std::move(*err);

    const QString caller = sessionUsername(request);

    //-----------------------------------------------------------------------//
    // Polling must be stopped before factory reset
    //-----------------------------------------------------------------------//
    if (m_pollingManager->isRunning()) {
        QJsonObject err;
        err[QLatin1String("error")] = QStringLiteral("Stop polling before factory reset");
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::Conflict);
    }

    Util::Logger::info(QStringLiteral("Factory reset requested by %1").arg(caller));

    //-----------------------------------------------------------------------//
    // 1. Initialize DB
    //-----------------------------------------------------------------------//
    QString resetErr;
    if (!m_db->factoryReset(resetErr)) {
        Util::Logger::error(QStringLiteral("Factory reset DB failed: %1").arg(resetErr));
        QJsonObject err;
        err[QLatin1String("error")] = resetErr;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    //-----------------------------------------------------------------------//
    // 2. Initialize Config
    //-----------------------------------------------------------------------//
    QString cfgErr;
    if (!factoryReset(QStringLiteral(SR_CONFIG_FILE), cfgErr)) {
        Util::Logger::error(QStringLiteral("Factory reset config failed: %1").arg(cfgErr));
        QJsonObject err;
        err[QLatin1String("error")] = cfgErr;
        return QHttpServerResponse(err, QHttpServerResponse::StatusCode::InternalServerError);
    }

    //-----------------------------------------------------------------------//
    // 3. Initialize Logs
    //-----------------------------------------------------------------------//
    QString logErr;
    Util::Logger::clearAll(logErr);

    Util::Logger::info(QStringLiteral("Factory reset completed"));

    QJsonObject resp;
    resp[QLatin1String("ok")]              = true;
    resp[QLatin1String("restartRequired")] = true;
    return QHttpServerResponse(resp);
}

QHttpServerResponse ApiServer::handleGetSystemResources(const QHttpServerRequest &request)
{
    if (auto err = requireAuth(request)) return std::move(*err);

    const Util::SystemResources res = m_systemMonitor->resources();

    QJsonObject cpu;
    cpu[QLatin1String("usagePercent")] = res.cpuUsagePercent;
    cpu[QLatin1String("loadAvg1")]     = res.loadAvg1;
    cpu[QLatin1String("loadAvg5")]     = res.loadAvg5;
    cpu[QLatin1String("loadAvg15")]    = res.loadAvg15;
    cpu[QLatin1String("tempCelsius")]  = res.cpuTempCelsius;

    QJsonObject memory;
    memory[QLatin1String("totalKb")]      = res.memTotalKb;
    memory[QLatin1String("usedKb")]       = res.memUsedKb;
    memory[QLatin1String("usagePercent")] = res.memUsagePercent;

    QJsonObject swap;
    swap[QLatin1String("totalKb")]      = res.swapTotalKb;
    swap[QLatin1String("usedKb")]       = res.swapUsedKb;
    swap[QLatin1String("usagePercent")] = res.swapUsagePercent;

    QJsonArray disks;
    for (const Util::DiskStat &d : res.disks) {
        QJsonObject obj;
        obj[QLatin1String("mount")]        = d.mount;
        obj[QLatin1String("totalMb")]      = d.totalMb;
        obj[QLatin1String("usedMb")]       = d.usedMb;
        obj[QLatin1String("usagePercent")] = d.usagePercent;
        disks.append(obj);
    }

    QJsonObject network;
    for (const Util::NetStat &n : res.network) {
        QJsonObject obj;
        obj[QLatin1String("rxBytes")] = n.rxBytes;
        obj[QLatin1String("txBytes")] = n.txBytes;
        network[n.iface] = obj;
    }

    QJsonObject resp;
    resp[QLatin1String("cpu")]           = cpu;
    resp[QLatin1String("memory")]        = memory;
    resp[QLatin1String("swap")]          = swap;
    resp[QLatin1String("disk")]          = disks;
    resp[QLatin1String("network")]       = network;
    resp[QLatin1String("uptimeSeconds")] = res.uptimeSeconds;
    resp[QLatin1String("cachedAt")]      = res.cachedAt.toString(Qt::ISODate);
    return QHttpServerResponse(resp);
}

} // namespace Api
