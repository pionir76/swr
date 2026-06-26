cmake_minimum_required(VERSION 3.16)

project(swr LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# System Version / Revision / last update date
# ---------------------------------------------------------------------------
set(SR_VERSION 1)
set(SR_REVISION 0)
set(SR_ZCODE 0)
set(SR_SCHEMA_VERSION 1)
string(TIMESTAMP SR_LAST_UPDATE_DATE "%Y-%m-%d")   # cmake 실행 시 자동 설정

# ---------------------------------------------------------------------------
# File paths (configurable at cmake-configure time)
#   cmake -DSR_CONFIG_FILE=/custom/path/config.xml ...
# ---------------------------------------------------------------------------
set(SR_CONFIG_FILE       "/etc/swr/config.json"              CACHE STRING "Config file path")
set(SR_DB_FILE           "/var/lib/swr/smartroute.db"        CACHE STRING "Database file path")
set(SR_LOG_FILE          "/var/log/swr/smartroute_log.db"    CACHE STRING "Log database file path")
set(SR_RESTORE_TEMP_DIR  "/tmp/swr_restore"                  CACHE STRING "Restore temp directory")

set(SR_ETH_DEV       "eth0"               CACHE STRING "Ethernet interface device")
set(SR_LOG_PRINT     1                    CACHE STRING "Enable log print: 0 or 1")
set(SR_MAX_LOG_LINES 1000                 CACHE STRING "Maximum log lines")
set(SR_MAX_DEVICES   20                   CACHE STRING "Maximum devices")
set(SR_API_PORT      8080                 CACHE STRING "Qt HTTP API port")

message(STATUS "SR_CONFIG_FILE      : ${SR_CONFIG_FILE}")
message(STATUS "SR_DB_FILE          : ${SR_DB_FILE}")
message(STATUS "SR_LOG_FILE         : ${SR_LOG_FILE}")
message(STATUS "SR_RESTORE_TEMP_DIR : ${SR_RESTORE_TEMP_DIR}")

message(STATUS "SR_ETH_DEV       : ${SR_ETH_DEV}")
message(STATUS "SR_LOG_PRINT     : ${SR_LOG_PRINT}")
message(STATUS "SR_MAX_LOG_LINES : ${SR_MAX_LOG_LINES}")
message(STATUS "SR_MAX_DEVICES   : ${SR_MAX_DEVICES}")
message(STATUS "SR_API_PORT      : ${SR_API_PORT}") 

MESSAGE(STATUS "SR_VERSION        : ${SR_VERSION}")
MESSAGE(STATUS "SR_REVISION       : ${SR_REVISION}")
MESSAGE(STATUS "SR_ZCODE          : ${SR_ZCODE}")
MESSAGE(STATUS "SR_SCHEMA_VERSION : ${SR_SCHEMA_VERSION}")
MESSAGE(STATUS "SR_LAST_UPDATE_DATE : ${SR_LAST_UPDATE_DATE}")


find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
  Core
  Network
  HttpServer
  SerialPort
  SerialBus
  Sql)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
  Core
  Network
  HttpServer
  SerialPort
  SerialBus
  Sql)

add_executable(swr
  main.cpp
  development-plan.md


  utils/Logger.h
  utils/Logger.cpp
  utils/SystemMonitor.h utils/SystemMonitor.cpp
  data_collection/database/DeviceDatabase.h data_collection/database/DeviceDatabase.cpp
  data_collection/model/DeviceModels.h
  data_collection/store/RegisterTable.h data_collection/store/RegisterTable.cpp
  data_collection/model/UnifiedRegister.h

  data_collection/polling/PollingManager.h data_collection/polling/PollingManager.cpp
  data_collection/polling/SerialWorker.h data_collection/polling/SerialWorker.cpp
  data_collection/polling/TcpWorker.h data_collection/polling/TcpWorker.cpp
  data_collection/store/DeviceList.h data_collection/store/DeviceList.cpp
  api/ApiServer.h api/ApiServer.cpp

  data_collection/processor/DataCollector.h data_collection/processor/DataCollector.cpp




  data_collection/comm/modbus/ModbusASCIIClient.h data_collection/comm/modbus/ModbusASCIIClient.cpp
  data_collection/comm/modbus/ModbusRTUClient.h data_collection/comm/modbus/ModbusRTUClient.cpp
  data_collection/comm/modbus/ModbusTCPClient.h data_collection/comm/modbus/ModbusTCPClient.cpp
  data_collection/comm/pclink/PcLinkClient.h data_collection/comm/pclink/PcLinkClient.cpp
  data_collection/comm/IDeviceClient.h
  data_collection/comm/DeviceClientFactory.h data_collection/comm/DeviceClientFactory.cpp
  data_collection/comm/RegisterExecutor.h data_collection/comm/RegisterExecutor.cpp
  data_collection/store/RegisterTable.md
  data_collection/comm/pclink/PcLinkSumTCPClient.h data_collection/comm/pclink/PcLinkSumTCPClient.cpp
  config/AppConfig.h config/AppConfig.cpp
  config/SystemConfig.h config/SystemConfig.cpp
  utils/NetworkConfigurator.h utils/NetworkConfigurator.cpp
  spec.md
  data-collection-architecture.md
  Modbus_Comm_Spec.md

  modbus_server/RegisterAddressMap.h modbus_server/RegisterAddressMap.cpp
  modbus_server/ModbusTcpServer.h modbus_server/ModbusTcpServer.cpp
  modbus_server/README.md

  maintenance/BackupModels.h
  maintenance/BackupManager.h maintenance/BackupManager.cpp
  maintenance/RestoreManager.h maintenance/RestoreManager.cpp

)

target_link_libraries(
  swr
  Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::CorePrivate
  Qt${QT_VERSION_MAJOR}::Network
  Qt${QT_VERSION_MAJOR}::HttpServer
  Qt${QT_VERSION_MAJOR}::SerialPort
  Qt${QT_VERSION_MAJOR}::SerialBus
  Qt${QT_VERSION_MAJOR}::Sql
)

# ---------------------------------------------------------------------------
# Shoud be place behind of add_executable(..) Compile-time path definitions
# ---------------------------------------------------------------------------
target_compile_definitions(swr PRIVATE
    SR_CONFIG_FILE="${SR_CONFIG_FILE}"
    SR_DB_FILE="${SR_DB_FILE}"
    SR_LOG_FILE="${SR_LOG_FILE}"
    SR_RESTORE_TEMP_DIR="${SR_RESTORE_TEMP_DIR}"

    SR_ETH_DEV="${SR_ETH_DEV}"
    SR_LOG_PRINT=${SR_LOG_PRINT}
    SR_MAX_LOG_LINES=${SR_MAX_LOG_LINES}
    SR_MAX_DEVICES=${SR_MAX_DEVICES}
    SR_API_PORT=${SR_API_PORT}

    SR_VERSION="${SR_VERSION}"
    SR_REVISION="${SR_REVISION}"
    SR_ZCODE="${SR_ZCODE}"
    SR_SCHEMA_VERSION=${SR_SCHEMA_VERSION}
    SR_LAST_UPDATE_DATE="${SR_LAST_UPDATE_DATE}"
)

include(GNUInstallDirs)
install(TARGETS swr
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

option(SR_INIT_SAMPLE_DATA "Insert sample data on startup" OFF)

if(SR_INIT_SAMPLE_DATA)
    target_compile_definitions(swr PRIVATE SR_INIT_SAMPLE_DATA)
endif()

