#pragma once

#include "../model/DeviceModels.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QList>
#include <QString>
#include <QStringList>

namespace Hmi { struct ParamDef; }

namespace DataCollection{
namespace Database{

// Copies a checked setting-tag definition into a tag (kind "param");
// the current value is left to the caller.
void fillHmiParamTag(Model::HmiTag &tag, const Hmi::ParamDef &def);

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

    Model::LoginResult validateUser(const QString &username,
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

    // HMI Tags
    QList<Model::HmiTag> loadHmiTags(QString &error) const;
    Model::HmiTag        loadHmiTag(const QString &id, bool &found, QString &error) const;
    bool insertHmiTag(const Model::HmiTag &tag, QString &error);
    bool updateHmiTag(const Model::HmiTag &tag, QString &error);
    // Current value of a setting (param) tag, as JSON scalar text
    bool setHmiParamValue(const QString &id, const QString &valueText, QString &error);
    bool deleteHmiTag(const QString &id, QString &error);

    // HMI Project / Screens
    bool loadHmiProject(Model::HmiProject &project,
                        QList<Model::HmiScreen> &screens,
                        QString &error) const;

    Model::HmiOpResult updateHmiProject(const Model::HmiProjectUpdate &update,
                                        Model::HmiProject &result,
                                        QString &error);

    Model::HmiScreen loadHmiScreen(const QString &id, bool &found, QString &error) const;

    Model::HmiOpResult insertHmiScreen(const Model::HmiScreen &screen,
                                       int maxScreens,
                                       Model::HmiProject &project,
                                       QString &error);

    // On RevisionMismatch, `current` carries the stored revision/updatedAt/updatedBy.
    Model::HmiOpResult updateHmiScreen(const QString &id,
                                       const QString *title,      // nullptr = keep current
                                       const QString &content,
                                       const QString &updatedBy,
                                       int revision,
                                       Model::HmiScreen &current,
                                       QString &error);

    Model::HmiOpResult deleteHmiScreen(const QString &id,
                                       Model::HmiProject &project,
                                       QString &error);

    // Restore
    bool restoreData(bool restoreDevices,
                     const QJsonArray &devices,
                     const QJsonArray &registers,
                     bool restoreUsers,
                     const QJsonArray &users,
                     bool restoreHmi,
                     const QJsonObject &hmi,
                     QStringList &hmiWarnings,
                     QString &error);

    // HMI import — replaces the project, screens and tags (no restart).
    // Tags whose unifiedAddress has no register here are skipped (ids in skippedTags).
    // The project and every screen get a revision above any current one.
    bool importHmi(const QJsonObject &hmi,
                   QStringList &warnings,
                   QStringList &skippedTags,
                   int &projectRevision,
                   QString &error);

    // Factory Reset
    bool factoryReset(QString &error);

private:
    bool migrateHmiTagKinds(QString &error);

    // Expr tags whose expression refers to a tag that does not exist,
    // as "ID (missing: A, B)" — for restore and import warnings.
    bool brokenExprTags(QSqlDatabase &db, QStringList &broken, QString &error) const;

    bool replaceHmi(QSqlDatabase &db,
                    const QJsonObject &hmi,
                    int revisionFloor,
                    QStringList &warnings,
                    QStringList &skippedTags,
                    QString &error);

    QString m_connectionName;
};

} // namespace Database
} // namespace DataCollection
