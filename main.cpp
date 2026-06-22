#include <QCoreApplication>
#include <QFile>
#include <QXmlStreamReader>
#include <QCryptographicHash>

#include "config/AppConfig.h"
#include "config/SystemConfig.h"
#include "utils/Logger.h"

#include "data_collection/database/DeviceDatabase.h"
#include "data_collection/store/RegisterTable.h"
#include "data_collection/store/DeviceList.h"
#include "data_collection/polling/PollingManager.h"
#include "api/ApiServer.h"

// ---------------------------------------------------------------------------
// Namespace alias
// ---------------------------------------------------------------------------
namespace DCStore = DataCollection::Store;
namespace DCModel = DataCollection::Model;
namespace DCDB    = DataCollection::Database;
namespace DCPoll  = DataCollection::Polling;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    //-----------------------------------------------------------------//
    // Load Config
    //-----------------------------------------------------------------//
    const QString configPath = QStringLiteral(SR_CONFIG_FILE);

    if (!QFile::exists(configPath)) {
        QString resetError;
        if (!factoryReset(configPath, resetError)) {
            qWarning() << "Factory reset failed:" << resetError;
        }
    }

    const AppConfig config = loadConfig(configPath);
    SystemConfig::init(config);

    //-----------------------------------------------------------------//
    // Logger Initialize.
    //-----------------------------------------------------------------//
    const QString logDbPath = QStringLiteral(SR_LOG_FILE);
    if(!Util::Logger::initialize(logDbPath, SR_LOG_PRINT, SR_MAX_LOG_LINES)){
        qWarning() << "Failed to initialize logger:" << logDbPath;
    }

    Util::Logger::info(
        QStringLiteral("Application started. Version: %1 Revision: %2 SpecialCode: Z%3 Last Update Date:%4")
        .arg(SR_VERSION)
        .arg(SR_REVISION)
        .arg(SR_ZCODE)
        .arg(SR_LAST_UPDATE_DATE)
    );

    Util::Logger::info(
        QStringLiteral("Ethernet Device %1 IP : %2")
                       .arg(config.networkInterfaces[0].name)
                       .arg(config.networkInterfaces[0].ipAddress)
    );

    Util::Logger::info(
        QStringLiteral("Ethernet Device %1 IP : %2")
                       .arg(config.networkInterfaces[1].name)
                       .arg(config.networkInterfaces[1].ipAddress)
    );

    Util::Logger::info(
        QStringLiteral("Serial 485 Device %1 : %2 | %3 | %4 | %5")
            .arg(config.rs485.device)
            .arg(config.rs485.baudRate)
            .arg(config.rs485.parity)
            .arg(config.rs485.stopBits)
            .arg(config.rs485.dataBits)
        );

    Util::Logger::info(
        QStringLiteral("Modbus Server %1 : %2 | %3")
            .arg(config.modbusServer.enabled ? "Enabled" : "Disabled")
            .arg(config.modbusServer.port)
            .arg(config.modbusServer.slaveId)
        );

    //---------------------------------------------------------------------//
    // Init Network Config
    //---------------------------------------------------------------------//
    /*
    for (const NetInterfaceConfig &iface : config.networkInterfaces) {
        if (!iface.enabled) continue;

        Util::NetworkConfig netConfig;
        netConfig.interfaceName = iface.name;
        netConfig.ipAddress     = iface.ipAddress;
        netConfig.netmask       = iface.netmask;
        netConfig.gateway       = iface.gateway;

        QString netError;
        if (!Util::applyNetworkConfig(netConfig, netError)) {
            Util::Logger::error(
                QStringLiteral("Network config failed [%1]: %2").arg(iface.name, netError));
        } else {
            Util::Logger::info(
                QStringLiteral("Network configured: %1 (%2) → %3")
                    .arg(iface.name, iface.role, iface.ipAddress));
        }
    }
    */


    //-----------------------------------------------------------------//
    // Open SQLite DB & Init Schema
    //-----------------------------------------------------------------//
    const QString dbFilePath = QStringLiteral(SR_DB_FILE);
    DCDB::DeviceDatabase db;
    QString dbError;

    if(!db.open(dbFilePath, dbError)){
        Util::Logger::error(
            QStringLiteral("Failed to open database: %1")
            .arg(dbError));

        return 1;
    }
    Util::Logger::info(QStringLiteral("Database opened: %1").arg(dbFilePath));

    //-----------------------------------------------------------------//
    // To Reset DataBase.
    //-----------------------------------------------------------------//
#ifdef SR_INIT_SAMPLE_DATA
    if (!db.resetSchema(dbError)) {
        Util::Logger::error(QStringLiteral("Failed to reset DB schema: %1").arg(dbError));
        return 1;
    }

    // Add default admin account.
    DCModel::UserInfo admin;
    admin.username     = QStringLiteral("admin");
    admin.displayName  = QStringLiteral("Administrator");
    admin.description  = QStringLiteral("Default administrator account");
    admin.passwordHash = QString::fromLatin1(
        QCryptographicHash::hash(
            QByteArrayLiteral("1234"),
            QCryptographicHash::Sha256
            ).toHex()
        );
    admin.role   = DCModel::userRoleFromString(QStringLiteral("admin"));
    admin.status = DCModel::UserStatus::Active;

    if (!db.insertUser(admin, dbError)) {
        Util::Logger::error(QStringLiteral("Failed to create default admin user: %1").arg(dbError));
        return 1;
    }

    Util::Logger::info(QStringLiteral("All Database Table Resetted."));
    Util::Logger::info(QStringLiteral("Default admin user created. username=admin"));

    return 0;
#endif

    //-----------------------------------------------------------------//
    // Create Register Table
    //-----------------------------------------------------------------//
    auto registerTable = std::make_shared<DCStore::RegisterTable>();

    //-----------------------------------------------------------------//
    // Init DeviceList
    //-----------------------------------------------------------------//
    auto deviceList = std::make_shared<DCStore::DeviceList>();
    {
        QString loadError;

        const QList<DCModel::DeviceInfo> devices = db.loadDevices(loadError);
        if (!loadError.isEmpty()) {
            Util::Logger::error(QStringLiteral("Failed to load devices: %1").arg(loadError));
        }
        else {
            deviceList->reset(devices);
            Util::Logger::info(QStringLiteral("DeviceList loaded: %1 device(s)").arg(devices.size()));
        }
    }

    //-----------------------------------------------------------------//
    // Start Polling Manager
    //-----------------------------------------------------------------//
    DCPoll::PollingManager pollingManager(registerTable, deviceList);

    QString pollError;
    if(!pollingManager.start(pollError)){
        Util::Logger::error(QStringLiteral("Failed to start polling: %1").arg(pollError));
    }

    //-----------------------------------------------------------------//
    // Start API Server
    //-----------------------------------------------------------------//
    Api::ApiServer apiServer(&db,
                             registerTable,
                             deviceList,
                             &pollingManager);


    QString apiError;
    if(!apiServer.start(SR_API_PORT, apiError)) {
        Util::Logger::error(QStringLiteral("Failed to start API server: %1").arg(apiError));
    }

    return app.exec();
}
