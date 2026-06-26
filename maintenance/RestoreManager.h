#pragma once

#include "BackupModels.h"

#include <QByteArray>
#include <QString>

namespace DataCollection::Database { class DeviceDatabase; }
namespace DataCollection::Polling  { class PollingManager; }

namespace Maintenance {

class RestoreManager
{
public:
    static RestorePreview validate(const QByteArray &zipData, QString &error);

    static bool apply(const QString &restoreId,
                      const RestoreOptions &options,
                      DataCollection::Database::DeviceDatabase *db,
                      DataCollection::Polling::PollingManager *pollingManager,
                      bool &restartRequired,
                      QString &error);

};

} // namespace Maintenance
