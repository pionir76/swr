#pragma once

#include "../model/DeviceModels.h"

#include <QJsonArray>
#include <QList>
#include <QString>

namespace DataCollection{
namespace Database{

enum class LoginResult {
    Success,
    InvalidCredentials,  // Username or password incorrect
    AccountDisabled,     // User account is disabled
    AccountLocked        // User account is locked due to too many failed login attempts
};

class DeviceDatabase
{
public:
    DeviceDatabase();
    ~DeviceDatabase();

    bool open(const QString& dbPath, QString& error);
    void close();
    bool isOpen() const;
    bool initSchema(QString& error);
    bool resetSchema(QString& error);

    // Device
    QList<Model::DeviceInfo> loadDevices(QString &error) const;
    bool insertDevice(const Model::DeviceInfo &device, QString &error);
    bool updateDevice(const Model::DeviceInfo &device, QString &error);
    bool deleteDevice(int deviceId, QString &error);

    // Register
    QList<Model::RegisterConfig> loadRegisters(int deviceId, QString &error) const;
    bool insertRegister(int deviceId, Model::RegisterConfig &config, QString &error);
    bool updateRegister(Model::RegisterConfig &config, QString &error);
    bool deleteRegister(int registerId, QString &error);

    // User
    QList<Model::UserInfo> loadUsers(QString &error) const;
    Model::UserInfo loadUser(const QString &username, bool &found, QString &error) const;

    bool insertUser(const Model::UserInfo &user, QString &error);
    bool updateUser(const Model::UserInfo &user, QString &error);
    bool deleteUser(const QString &username, QString &error);

    LoginResult validateUser(const QString &username,
                             const QString &password,
                             const QString &ip,
                             int maxFailedAttempts,
                             QString &error);

    // Login History
    bool insertLoginHistory(const Model::LoginHistoryEntry &entry, QString &error);
    QList<Model::LoginHistoryEntry> fetchLoginHistory(int limit,
                                                      const QString &username,
                                                      QString &error) const;
    bool deleteLoginHistory(const QString &username, QString &error);

    // Restore
    bool restoreData(bool restoreDevices,
                     const QJsonArray &devices,
                     const QJsonArray &registers,
                     bool restoreUsers,
                     const QJsonArray &users,
                     QString &error);

    // Factory Reset
    bool factoryReset(QString &error);

private:

    QString m_connectionName;
};

} // namespace Database
} // namespace DataCollection
