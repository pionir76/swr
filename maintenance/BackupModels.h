#pragma once

#include <QString>
#include <QStringList>

namespace Maintenance {

// ---------------------------------------------------------------------------
// Backup
// ---------------------------------------------------------------------------

struct BackupManifest {
    QString product   = QStringLiteral("SmartRoute");
    QString createdAt;

    struct SourceDevice {
        QString hostname;
        QString version;
        QString revision;
        QString zcode;
        int     schemaVersion = SR_SCHEMA_VERSION;
    } sourceDevice;

    struct Contents {
        bool config    = true;
        bool devices   = true;
        bool registers = true;
        bool users     = true;
        bool hmi       = false;
    } contents;
};

// ---------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------

struct RestoreOptions {
    bool config    = true;
    bool network   = false;  // 네트워크 설정은 기본 OFF (장치 접근 불능 방지)
    bool devices   = true;
    bool registers = true;
    bool users     = true;
    bool hmi       = false;
};

struct RestoreItemInfo {
    bool    available = false;
    int     count     = 0;
    QString warning;
};

struct RestorePreview {
    bool    valid     = false;
    QString restoreId;

    struct BackupInfo {
        QString product;
        QString createdAt;
        QString hostname;
        QString version;
        QString revision;
        QString zcode;
        int     schemaVersion = 0;
    } backupInfo;

    RestoreItemInfo config;
    RestoreItemInfo devices;
    RestoreItemInfo registers;
    RestoreItemInfo users;
    RestoreItemInfo hmi;

    QStringList warnings;
};

} // namespace Maintenance
