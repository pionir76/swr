#include "PcLinkSumTCPClient.h"

namespace DataCollection {
namespace Comm {

PcLinkSumTCPClient::PcLinkSumTCPClient(const Model::DeviceConnection &connection)
    : m_connection(connection)
{
}

PcLinkSumTCPClient::~PcLinkSumTCPClient()
{
    disconnect();
}

bool PcLinkSumTCPClient::connect()
{
    m_lastError = QStringLiteral("PcLinkSum TCP: not implemented");
    return false;
}

void PcLinkSumTCPClient::disconnect()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.disconnectFromHost();
        m_socket.waitForDisconnected(1000);
    }
}

bool PcLinkSumTCPClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool PcLinkSumTCPClient::readWords(int /*address*/, int /*count*/,
                                    QVector<quint16> & /*out*/, QString &error)
{
    error = m_lastError = QStringLiteral("PcLinkSum TCP: not implemented");
    return false;
}

bool PcLinkSumTCPClient::readBits(int /*address*/, int /*count*/,
                                   QVector<bool> & /*out*/, QString &error)
{
    error = m_lastError = QStringLiteral("PcLinkSum TCP: not implemented");
    return false;
}

bool PcLinkSumTCPClient::writeWords(int /*address*/,
                                     const QVector<quint16> & /*values*/,
                                     QString &error)
{
    error = m_lastError = QStringLiteral("PcLinkSum TCP: not implemented");
    return false;
}

bool PcLinkSumTCPClient::writeBits(int /*address*/,
                                    const QVector<bool> & /*values*/,
                                    QString &error)
{
    error = m_lastError = QStringLiteral("PcLinkSum TCP: not implemented");
    return false;
}

QString PcLinkSumTCPClient::errorString() const
{
    return m_lastError;
}

} // namespace Comm
} // namespace DataCollection
