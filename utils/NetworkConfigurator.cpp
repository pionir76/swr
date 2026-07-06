#include "NetworkConfigurator.h"
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>

namespace Util {

static int netmaskToPrefix(const QString &netmask)
{
    const QRegularExpression rx(QStringLiteral("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$"));
    const QRegularExpressionMatch match = rx.match(netmask.trimmed());
    if (!match.hasMatch()) {
        return -1;
    }

    int prefix = 0;
    quint32 value = 0;
    for (int i = 1; i <= 4; ++i) {
        bool ok = false;
        const int octet = match.captured(i).toInt(&ok);
        if (!ok || octet < 0 || octet > 255) {
            return -1;
        }
        value = (value << 8) | static_cast<quint32>(octet);
    }

    bool seenZero = false;
    for (int bit = 31; bit >= 0; --bit) {
        if (value & (1u << bit)) {
            if (seenZero) {
                return -1;
            }
            ++prefix;
        } else {
            seenZero = true;
        }
    }

    return prefix;
}

static bool runCommand(const QString &program, const QStringList &arguments, QString &error)
{
    QProcess process;
    process.start(program, arguments);
    
    if (!process.waitForFinished(5000)) {
        error = QStringLiteral("Command timed out: %1").arg(program);
        return false;
    }

    if (process.exitCode() != 0) {
        QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (stderrText.isEmpty()) {
            stderrText = QStringLiteral("%1 failed with exit code %2").arg(program).arg(process.exitCode());
        }
        error = stderrText;
        return false;
    }

    return true;
}

bool applyNetworkConfig(const NetworkConfig &config, QString &error)
{
    if (config.ipAddress.isEmpty() || config.netmask.isEmpty() || config.gateway.isEmpty()) {
        error = QStringLiteral("Incomplete network configuration.");
        return false;
    }

#if defined(Q_OS_UNIX)
    const int prefix = netmaskToPrefix(config.netmask);
    if (prefix < 0) {
        error = QStringLiteral("Invalid netmask format: %1").arg(config.netmask);
        return false;
    }

    const QString iface = config.interfaceName.isEmpty() ? QStringLiteral("eth0") : config.interfaceName;
    if (!runCommand(QStringLiteral("ip"), {QStringLiteral("addr"), QStringLiteral("flush"), QStringLiteral("dev"), iface}, error)) {
        return false;
    }

    if (!runCommand(QStringLiteral("ip"), {QStringLiteral("addr"), QStringLiteral("add"), QStringLiteral("%1/%2").arg(config.ipAddress).arg(prefix), QStringLiteral("dev"), iface}, error)) {
        return false;
    }

    if (!runCommand(QStringLiteral("ip"), {QStringLiteral("route"), QStringLiteral("add"), QStringLiteral("default"), QStringLiteral("via"), config.gateway, QStringLiteral("dev"), iface}, error)) {
        return false;
    }

    return true;
#elif defined(Q_OS_WIN)
    Q_UNUSED(config);
    error = QStringLiteral("Network configuration is not supported on Windows in this implementation.");
    return false;
#else
    Q_UNUSED(config);
    error = QStringLiteral("Network configuration is not supported on this platform.");
    return false;
#endif
}

} // namespace Util
