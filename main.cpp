#include <QCoreApplication>
#include <QFile>
#include <QProcess>

#include "config/AppConfig.h"
#include "config/SystemConfig.h"
#include "utils/Logger.h"

#include "data_collection/database/DeviceDatabase.h"
#include "data_collection/store/RegisterTable.h"
#include "data_collection/store/DeviceList.h"
#include "data_collection/polling/PollingManager.h"
#include "api/ApiServer.h"
#include "utils/SystemMonitor.h"
#include "modbus_server/ModbusTcpServer.h"

// ---------------------------------------------------------------------------
// Apply NTP Config 
//   replace of /etc/ntp.conf server as config value. and Transmit SIGHUP
//   (API → Save config → reboot).
// ---------------------------------------------------------------------------
static void applyNtpConfig(const QString &ntpServer)
{
    if (ntpServer.isEmpty()) return;

    const QString content = QStringLiteral(
        "driftfile /var/lib/ntp/drift\n"
        "server %1 iburst\n"
        "server 127.127.1.0\n"
        "fudge 127.127.1.0 stratum 14\n"
        "restrict -4 default notrap nomodify nopeer noquery\n"
        "restrict -6 default notrap nomodify nopeer noquery\n"
        "restrict 127.0.0.1\n"
        "restrict ::1\n"
    ).arg(ntpServer);

    QFile f(QStringLiteral("/etc/ntp.conf"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Util::Logger::error(QStringLiteral("NTP: failed to write /etc/ntp.conf: %1").arg(f.errorString()));
        return;
    }
    f.write(content.toUtf8());
    f.close();

    // Transmit SIG HUP to ntp -> reload
    QProcess::startDetached(QStringLiteral("/bin/sh"),
        { QStringLiteral("-c"),
          QStringLiteral("kill -HUP $(pidof ntpd) 2>/dev/null || true") });

    Util::Logger::info(QStringLiteral("NTP server applied: %1").arg(ntpServer));
}

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

    //-----------------------------------------------------------------//
    // Apply NTP Server Config & Service Reload
    //-----------------------------------------------------------------//
    applyNtpConfig(config.system.ntpServer);

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
            Util::Logger::error(QStringLiteral("Network config failed [%1]: %2").arg(iface.name, netError));
        } else {
            Util::Logger::info(QStringLiteral("Network configured: %1 (%2) → %3")
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
#ifdef SR_INIT_DATABASE_SCHEMA
    if (!db.resetSchema(dbError)) {
        Util::Logger::error(QStringLiteral("Failed to reset DB schema: %1").arg(dbError));
        return 1;
    }

    Util::Logger::info(QStringLiteral("Database schema reset completed. Default admin account ensured."));
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
    // Start Modbus TCP Server (if enabled in config)
    //-----------------------------------------------------------------//
    ModbusServer::ModbusTcpServer modbusTcpServer(registerTable, deviceList);
    if (config.modbusServer.enabled) {
        QString modbusError;
        if (!modbusTcpServer.start(config.modbusServer.port,
                                   config.modbusServer.slaveId,
                                   modbusError)) {
            Util::Logger::error(QStringLiteral("Failed to start Modbus TCP Server: %1").arg(modbusError));
        } else {
            Util::Logger::info(QStringLiteral("Modbus TCP Server started on port %1, slaveId %2")
                .arg(config.modbusServer.port)
                .arg(config.modbusServer.slaveId));
        }
    }

    //-----------------------------------------------------------------//
    // System Monitor ( Every 3 seconds, check disk and network , ntp server status )
    //-----------------------------------------------------------------//
    Util::SystemMonitor systemMonitor(
        {QStringLiteral("/"), QStringLiteral("/var")},
        {QStringLiteral("eth0"), QStringLiteral("eth1")},
        3);

    //-----------------------------------------------------------------//
    // Start API Server
    //-----------------------------------------------------------------//
    Api::ApiServer apiServer(&db,
                             registerTable,
                             deviceList,
                             &pollingManager,
                             &systemMonitor);

    QString apiError;
    if(!apiServer.start(SR_API_PORT, apiError)) {
        Util::Logger::error(QStringLiteral("Failed to start API server: %1").arg(apiError));
    }

    const int result = app.exec();

    //-----------------------------------------------------------//
    // Flush remaining entries.
    //-----------------------------------------------------------//
    Util::Logger::shutdown();

    return result;
}
