#pragma once

#include <QObject>
#include <QHttpServer>
#include <optional>

namespace DataCollection::Database { class DeviceDatabase; }
namespace DataCollection::Store   { class RegisterTable; class DeviceList; }
namespace DataCollection::Polling { class PollingManager; }
namespace DataCollection::Model   { struct DeviceInfo; struct RegisterConfig; }
namespace Util { class SystemMonitor; }

namespace Api{

class ApiServer : public QObject
{
    Q_OBJECT

public:
    explicit ApiServer(DataCollection::Database::DeviceDatabase *db,
                       std::shared_ptr<DataCollection::Store::RegisterTable> registerTable,
                       std::shared_ptr<DataCollection::Store::DeviceList> deviceList,
                       DataCollection::Polling::PollingManager *pollingManager,
                       QObject *parent = nullptr);

    ~ApiServer() override;

    bool start(quint16 port, QString& error);
    void stop();

private:
    enum class RequiredRole { Any, ManagerOrAbove, AdminOnly };

    void setupRoutes();

    std::optional<QHttpServerResponse> requireAuth(const QHttpServerRequest &request,
                                                   RequiredRole required = RequiredRole::Any) const;
    QString createSession(const QString &username);
    QString removeSession(const QString &token);  // returns username

    // return 409 while polling status, when ask modify.
    bool rejectIfPolling(QHttpServerResponse &out) const;
    QString sessionUsername(const QHttpServerRequest &request) const;
    // Authenticate
    QHttpServerResponse handleLogin(const QHttpServerRequest &request);
    QHttpServerResponse handleLogout(const QHttpServerRequest &request);
    QHttpServerResponse handleSession(const QHttpServerRequest &request);

    // Dashboard
    QHttpServerResponse handleGetDashboard(const QHttpServerRequest &request);

    // Polling Control
    QHttpServerResponse handleGetPollingStatus(const QHttpServerRequest &request);
    QHttpServerResponse handleStartPolling(const QHttpServerRequest &request);
    QHttpServerResponse handleStopPolling(const QHttpServerRequest &request);

    // Devices
    QHttpServerResponse handleGetDevices(const QHttpServerRequest &request);
    QHttpServerResponse handleGetDeviceStatus(const QHttpServerRequest &request);
    QHttpServerResponse handlePostDevice(const QHttpServerRequest &request);
    QHttpServerResponse handlePutDevice(const QHttpServerRequest &request, const QString &id);
    QHttpServerResponse handleDeleteDevice(const QHttpServerRequest &request, const QString &id);

    // Register
    QHttpServerResponse handleGetRegisters(const QHttpServerRequest &request, const QString &deviceId);
    QHttpServerResponse handlePostRegister(const QHttpServerRequest &request, const QString &deviceId);
    QHttpServerResponse handlePutRegister(const QHttpServerRequest &request, const QString &registerId);
    QHttpServerResponse handleDeleteRegister(const QHttpServerRequest &request, const QString &registerId);
    QHttpServerResponse handleCheckUnifiedId(const QHttpServerRequest &request);
    QHttpServerResponse handleWriteRegister(const QHttpServerRequest &request, const QString &registerId);

    // Real time value
    QHttpServerResponse handleGetRealtime(const QHttpServerRequest &request);

    // Logs
    QHttpServerResponse handleGetLogs(const QHttpServerRequest &request);

    // User
    QHttpServerResponse handleGetUsers(const QHttpServerRequest &request);
    QHttpServerResponse handlePostUser(const QHttpServerRequest &request);
    QHttpServerResponse handleDeleteUser(const QHttpServerRequest &request, const QString &username);
    QHttpServerResponse handlePutUser(const QHttpServerRequest &request, const QString &username);
    QHttpServerResponse handlePutUserPassword(const QHttpServerRequest &request, const QString &username);
    QHttpServerResponse handlePutUserStatus(const QHttpServerRequest &request, const QString &username);

    // System Config
    QHttpServerResponse handleGetConfig(const QHttpServerRequest &request);
    QHttpServerResponse handlePostConfigReset(const QHttpServerRequest &request);
    QHttpServerResponse handlePutConfigNetwork(const QHttpServerRequest &request);
    QHttpServerResponse handlePutConfigSerial(const QHttpServerRequest &request);
    QHttpServerResponse handlePutConfigSystem(const QHttpServerRequest &request);
    QHttpServerResponse handlePutConfigModbusServer(const QHttpServerRequest &request);
    QHttpServerResponse handlePostRestart(const QHttpServerRequest &request);
    QHttpServerResponse handleGetSystemInfo(const QHttpServerRequest &request);

    // Login History
    QHttpServerResponse handleGetLoginHistory(const QHttpServerRequest &request);
    QHttpServerResponse handleDeleteLoginHistory(const QHttpServerRequest &request);

    // Maintenance
    QHttpServerResponse handleGetBackup(const QHttpServerRequest &request);
    QHttpServerResponse handleRestoreValidate(const QHttpServerRequest &request);
    QHttpServerResponse handleRestoreApply(const QHttpServerRequest &request);
    QHttpServerResponse handlePostFactoryReset(const QHttpServerRequest &request);

    // System Resources
    QHttpServerResponse handleGetSystemResources(const QHttpServerRequest &request);

    // Security Policy
    QHttpServerResponse handleGetSecurityPolicy(const QHttpServerRequest &request);
    QHttpServerResponse handlePutSecurityPolicy(const QHttpServerRequest &request);

    // DB & DeviceList Sync Helper
    bool syncAddDevice(const DataCollection::Model::DeviceInfo &device, QString &error);
    bool syncUpdateDevice(const DataCollection::Model::DeviceInfo &device, QString &error);
    bool syncDeleteDevice(int id, QString &error);
    bool syncAddRegister(int deviceId, DataCollection::Model::RegisterConfig &config, QString &error);
    bool syncUpdateRegister(DataCollection::Model::RegisterConfig config, QString &error);
    bool syncDeleteRegister(int registerId, QString &error);

private:
    DataCollection::Database::DeviceDatabase *m_db;
    std::shared_ptr<DataCollection::Store::RegisterTable> m_registerTable;
    std::shared_ptr<DataCollection::Store::DeviceList> m_deviceList;
    DataCollection::Polling::PollingManager *m_pollingManager;
    Util::SystemMonitor *m_systemMonitor;
    QHttpServer m_server;

    mutable QMutex m_sessionMutex;
    QSet<QString> m_sessions;
    QMap<QString, QString> m_sessionUsers;  // token → username
};

} //namespace Api
