#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

namespace DataCollection::Database { class DeviceDatabase; }

namespace Maintenance {

class BackupManager
{
public:
    static QByteArray create(const DataCollection::Database::DeviceDatabase *db, QString &error);

private:
    static QByteArray buildManifest();
    static QByteArray buildConfig();
    static QByteArray buildDevices(const DataCollection::Database::DeviceDatabase *db, QString &error);
    static QByteArray buildRegisters(const DataCollection::Database::DeviceDatabase *db, QString &error);
    static QByteArray buildUsers(const DataCollection::Database::DeviceDatabase *db, QString &error);
    static QByteArray buildHmi();
    static QByteArray buildChecksum(const QMap<QString, QByteArray> &files);
};

} // namespace Maintenance
