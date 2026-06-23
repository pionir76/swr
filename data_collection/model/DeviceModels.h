#pragma once

#include <QString>
#include <QList>
#include <QMetaType>
#include <limits>

namespace DataCollection {
namespace Model {

enum class ByteOrder {
    Default,
    BigEndian,
    LittleEndian
};

enum class RegisterType {
    Unknown,

    // Modbus-specific types
    Coil,           // FC01 — RW bits
    DiscreteInput,  // FC02 — RO bits
    HoldingRegister,// FC03 — RW words
    InputRegister,  // FC04 — RO words

    // Generic types for non-Modbus protocols (PcLink, etc.)
    WordRegister,   // RW word register
    BitRegister     // RW bit register
};


struct RegisterField {
    QString tagName;        // Real Name (temp pv)
    QString displayName;    // Display Name (cool water temp)

    int address = 0;
    RegisterType type = RegisterType::Unknown;
    bool readOnly = false;
    int length = 1;
    int unifiedRegisterId = -1;
    QString unit;

    double scale = 1.0;
    bool isSigned = false;
    QString bitLabels;  // JSON {"0":"label0","1":"label1"} — empty = Unused
    double minValue = std::numeric_limits<double>::lowest();
    double maxValue = std::numeric_limits<double>::max();
    ByteOrder byteOrder = ByteOrder::Default;
};

struct PollingConfig {
    int intervalMs = 1000;
    int retryCount = 3;
};


struct DeviceConnection {
    // Connection Type
    enum class ConnectionType {
        Serial,
        Tcp
    };

    // Communication Protocol
    enum class Protocol {
        Unknown,
        ModbusRtu,
        ModbusTcp,
        ModbusAscii,
        PcLink,
        PcLinkSum
    };

    ConnectionType type = ConnectionType::Serial;
    Protocol protocol = Protocol::Unknown;

    // Default Byte Order.
    ByteOrder defaultByteOrder = ByteOrder::BigEndian;

    QString ipAddress = "192.168.0.100";
    int tcpPort = 502;

    int slaveId = 1;
    int timeoutMs = 5000;
};

struct DeviceInfo {
    int id = -1;

    QString deviceCode;
    QString name;
    QString displayName;

    DeviceConnection connection;
    QList<RegisterField> registers;
    PollingConfig polling;

    // Runtime status, Updated By Polling Worker.
    struct Status {
        enum class State {
            Unknown,
            Ok,
            Error
        } state = State::Unknown;

        qint64  lastPollTimestamp  = 0;
        int     lastPollDurationMs = 0;
        int     consecutiveErrors  = 0;
        QString lastError;
    } status;
};

enum class UserRole {
    User,
    Manager,
    Admin
};

enum class UserStatus {
    Active,
    Disabled,
    Locked
};

struct UserInfo
{
    int id = -1;

    QString username;
    QString displayName;
    QString description;

    QString passwordHash;

    UserRole   role   = UserRole::User;
    UserStatus status = UserStatus::Active;

    int     failedLoginCount = 0;
    QString lastLoginAt;
    QString lastLoginIp;
    QString createdAt;
    QString updatedAt;
};

struct WriteRequest {
    RegisterField field;
    QVector<quint16> rawValues;
    QVector<bool> coilValues;
    int retryCount = 3;
};

struct LoginHistoryEntry {
    qint64  id = -1;
    QString timestamp;
    QString username;
    QString action;   // "login" | "logout"
    QString result;   // "success" | "fail"
    QString ip;
};

struct PollLogEntry {
    qint64  id            = -1;
    int     deviceId      = -1;
    QString deviceName;
    QString timestamp;
    bool    success       = false;
    int     durationMs    = -1;
    int     registerCount = 0;
    QString message;
};


//--------------------------------------------------------------------//
// Helper
//--------------------------------------------------------------------//
inline QString connectionTypeToString(Model::DeviceConnection::ConnectionType type)
{
    return type == Model::DeviceConnection::ConnectionType::Tcp
               ? QStringLiteral("tcp")
               : QStringLiteral("serial");
}

inline DeviceConnection::ConnectionType connectionTypeFromString(const QString &s)
{
    if (s == QLatin1String("tcp")) return DeviceConnection::ConnectionType::Tcp;
    return DeviceConnection::ConnectionType::Serial;
}

inline QString protocolToString(DeviceConnection::Protocol protocol)
{
    switch (protocol) {
    case DeviceConnection::Protocol::ModbusRtu:   return QStringLiteral("modbus_rtu");
    case DeviceConnection::Protocol::ModbusTcp:   return QStringLiteral("modbus_tcp");
    case DeviceConnection::Protocol::ModbusAscii: return QStringLiteral("modbus_ascii");
    case DeviceConnection::Protocol::PcLink:      return QStringLiteral("pclink");
    case DeviceConnection::Protocol::PcLinkSum:   return QStringLiteral("pclink_sum");
    default:                                      return QStringLiteral("unknown");
    }
}

inline DeviceConnection::Protocol protocolFromString(const QString &s)
{
    if (s == QLatin1String("modbus_rtu"))   return DeviceConnection::Protocol::ModbusRtu;
    if (s == QLatin1String("modbus_tcp"))   return DeviceConnection::Protocol::ModbusTcp;
    if (s == QLatin1String("modbus_ascii")) return DeviceConnection::Protocol::ModbusAscii;
    if (s == QLatin1String("pclink"))       return DeviceConnection::Protocol::PcLink;
    if (s == QLatin1String("pclink_sum"))   return DeviceConnection::Protocol::PcLinkSum;
    return DeviceConnection::Protocol::Unknown;
}

inline QString userRoleToString(UserRole role)
{
    switch (role) {
    case UserRole::Admin:   return QStringLiteral("admin");
    case UserRole::Manager: return QStringLiteral("manager");
    default:               return QStringLiteral("user");
    }
}

inline UserRole userRoleFromString(const QString &s)
{
    if (s == QLatin1String("admin"))   return UserRole::Admin;
    if (s == QLatin1String("manager")) return UserRole::Manager;
    return UserRole::User;
}

inline QString userStatusToString(UserStatus status)
{
    switch (status) {
    case UserStatus::Disabled: return QStringLiteral("disabled");
    case UserStatus::Locked:   return QStringLiteral("locked");
    default:                   return QStringLiteral("active");
    }
}

inline UserStatus userStatusFromString(const QString &s)
{
    if (s == QLatin1String("disabled")) return UserStatus::Disabled;
    if (s == QLatin1String("locked"))   return UserStatus::Locked;
    return UserStatus::Active;
}


inline QString byteOrderToString(ByteOrder order)
{
    switch (order) {
    case ByteOrder::LittleEndian: return QStringLiteral("little");
    case ByteOrder::BigEndian:    return QStringLiteral("big");
    default:                      return QStringLiteral("default");
    }
}

inline ByteOrder byteOrderFromString(const QString &s)
{
    if (s == QLatin1String("little")) return ByteOrder::LittleEndian;
    if (s == QLatin1String("big"))    return ByteOrder::BigEndian;
    return ByteOrder::Default;
}

inline QString registerTypeToString(RegisterType type)
{
    switch (type) {
    case RegisterType::Coil:            return QStringLiteral("coil");
    case RegisterType::DiscreteInput:   return QStringLiteral("discrete_input");
    case RegisterType::HoldingRegister: return QStringLiteral("holding_register");
    case RegisterType::InputRegister:   return QStringLiteral("input_register");
    case RegisterType::WordRegister:    return QStringLiteral("word_register");
    case RegisterType::BitRegister:     return QStringLiteral("bit_register");
    default:                            return QStringLiteral("unknown");
    }
}

inline RegisterType registerTypeFromString(const QString &s)
{
    const QString n = s.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('_'));
    if (n == QLatin1String("coil"))            return RegisterType::Coil;
    if (n == QLatin1String("discreteinput"))   return RegisterType::DiscreteInput;
    if (n == QLatin1String("holdingregister")) return RegisterType::HoldingRegister;
    if (n == QLatin1String("inputregister"))   return RegisterType::InputRegister;
    if (n == QLatin1String("wordregister"))    return RegisterType::WordRegister;
    if (n == QLatin1String("bitregister"))     return RegisterType::BitRegister;
    return RegisterType::Unknown;
}

} // namespace Model
} // namespace DataCollection

Q_DECLARE_METATYPE(DataCollection::Model::RegisterField)
Q_DECLARE_METATYPE(DataCollection::Model::PollingConfig)
Q_DECLARE_METATYPE(DataCollection::Model::DeviceConnection)
Q_DECLARE_METATYPE(DataCollection::Model::DeviceInfo)
