#include "PcLinkSumTCPClient.h"

#include <QAbstractSocket>

namespace DataCollection {
namespace Comm {

// PCLink SUM frame structure:
//   Request:  [STX(0x02)][STN(2dec)][CMD(3)],[CNT(2dec)],[ADDR(4dec)][SUM(2HEX)][CR][LF]
//   Response: [STX(0x02)][STN(2dec)][CMD,OK][,XXXX]...[SUM(2HEX)][CR][LF]
//   NG:       [STX(0x02)][STN(2dec)][NG][ERR(2dec)][SUM(2HEX)][CR][LF]
// SUM = sum of all body bytes (between STX and SUM field), lower byte, uppercase 2-char hex.

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
    if (isConnected()) return true;

    m_socket.connectToHost(m_connection.ipAddress,
                           static_cast<quint16>(m_connection.tcpPort));
    if (!m_socket.waitForConnected(m_connection.timeoutMs)) {
        m_lastError = QStringLiteral("PCLink SUM: connect failed to %1:%2 — %3")
                          .arg(m_connection.ipAddress)
                          .arg(m_connection.tcpPort)
                          .arg(m_socket.errorString());
        return false;
    }
    return true;
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

bool PcLinkSumTCPClient::readWords(int address, int count,
                                    QVector<quint16> &out, QString &error)
{
    if (!isConnected()) {
        error = m_lastError = QStringLiteral("PCLink SUM: not connected");
        return false;
    }

    const QByteArray request = buildRsdFrame(address, count);
    QByteArray response;

    if (!sendRequest(request, response, error)){
        return false;
    }

    // qDebug() << "PCLink SUM TCP: request:" << request << "response:" << response;

    return parseRsdResponse(response, count, out, error);
}

bool PcLinkSumTCPClient::readBits(int /*address*/, int /*count*/,
                                   QVector<bool> & /*out*/, QString &error)
{
    error = m_lastError = QStringLiteral("PCLink SUM TCP: bit read not supported");
    return false;
}

bool PcLinkSumTCPClient::writeWords(int address,
                                     const QVector<quint16> &values, QString &error)
{
    if (!isConnected()) {
        error = m_lastError = QStringLiteral("PCLink SUM: not connected");
        return false;
    }

    const QByteArray request = buildWsdFrame(address, values);
    QByteArray response;
    if (!sendRequest(request, response, error)) return false;

    return parseWsdResponse(response, error);
}

bool PcLinkSumTCPClient::writeBits(int /*address*/,
                                    const QVector<bool> & /*values*/, QString &error)
{
    error = m_lastError = QStringLiteral("PCLink SUM TCP: bit write not supported");
    return false;
}

QString PcLinkSumTCPClient::errorString() const
{
    return m_lastError;
}

// ---------- private frame builders ----------

QByteArray PcLinkSumTCPClient::buildRsdFrame(int address, int count) const
{
    // body: STN(2dec) + "RSD," + CNT(2dec) + "," + ADDR(4dec)
    QByteArray body;
    body += QStringLiteral("%1").arg(m_connection.slaveId, 2, 10, QChar('0')).toLatin1();
    body += "RSD,";
    body += QStringLiteral("%1").arg(count, 2, 10, QChar('0')).toLatin1();
    body += ',';
    body += QStringLiteral("%1").arg(address, 4, 10, QChar('0')).toLatin1();
    return wrapFrame(body);
}

QByteArray PcLinkSumTCPClient::buildWsdFrame(int address,
                                               const QVector<quint16> &values) const
{
    // body: STN(2dec) + "WSD," + CNT(2dec) + "," + ADDR(4dec) + ",XXXX" per value
    QByteArray body;
    body += QStringLiteral("%1").arg(m_connection.slaveId, 2, 10, QChar('0')).toLatin1();
    body += "WSD,";
    body += QStringLiteral("%1").arg(values.size(), 2, 10, QChar('0')).toLatin1();
    body += ',';
    body += QStringLiteral("%1").arg(address, 4, 10, QChar('0')).toLatin1();
    for (quint16 v : values) {
        body += ',';
        body += QStringLiteral("%1").arg(v, 4, 16, QChar('0')).toUpper().toLatin1();
    }
    return wrapFrame(body);
}

QByteArray PcLinkSumTCPClient::wrapFrame(const QByteArray &body)
{
    QByteArray frame;
    frame += '\x02';  // STX
    frame += body;
    frame += QStringLiteral("%1").arg(calcBodySum(body), 2, 16, QChar('0')).toUpper().toLatin1();
    frame += '\r';
    frame += '\n';
    return frame;
}

quint8 PcLinkSumTCPClient::calcBodySum(const QByteArray &body)
{
    quint8 sum = 0;
    for (auto b : body) sum += static_cast<quint8>(b);
    return sum;
}

bool PcLinkSumTCPClient::verifyFrameSum(const QByteArray &frame)
{
    // layout: [STX(1)][body][SUM(2)][CR(1)][LF(1)]
    if (frame.size() < 6) return false;
    if (static_cast<quint8>(frame[0]) != 0x02) return false;
    if (frame[frame.size() - 2] != '\r' || frame[frame.size() - 1] != '\n') return false;

    const int sumPos = frame.size() - 4;  // SUM field starts here
    quint8 sum = 0;
    for (int i = 1; i < sumPos; i++) sum += static_cast<quint8>(frame[i]);

    const QString received   = QString::fromLatin1(frame.mid(sumPos, 2)).toUpper();
    const QString calculated = QStringLiteral("%1").arg(sum, 2, 16, QChar('0')).toUpper();
    return received == calculated;
}

// ---------- private I/O ----------

bool PcLinkSumTCPClient::sendRequest(const QByteArray &request,
                                      QByteArray &response, QString &error)
{
    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(m_connection.timeoutMs)) {
        error = m_lastError = QStringLiteral("PCLink SUM: write timeout");
        return false;
    }

    response.clear();
    while (true) {
        if (!m_socket.waitForReadyRead(m_connection.timeoutMs)) {
            error = m_lastError = QStringLiteral("PCLink SUM: read timeout");
            return false;
        }
        response += m_socket.readAll();
        if (response.endsWith("\r\n")) break;
    }
    return true;
}

// ---------- private parsers ----------

bool PcLinkSumTCPClient::checkNgResponse(const QByteArray &response, QString &error)
{
    // NG frame: [STX(1)][STN(2)][NG(2)][ERR(2)][SUM(2)][CR(1)][LF(1)] = 11 bytes
    if (response.size() < 11) return false;
    if (response.mid(3, 2) != "NG") return false;

    const int errCode = response.mid(5, 2).toInt(nullptr, 10);
    error = m_lastError = QStringLiteral("PCLink SUM: PLC returned NG (error code %1)").arg(errCode);
    return true;
}

bool PcLinkSumTCPClient::parseRsdResponse(const QByteArray &response, int expectedCount,
                                           QVector<quint16> &out, QString &error)
{
    if (checkNgResponse(response, error)) return false;

    if (!verifyFrameSum(response)) {
        error = m_lastError = QStringLiteral("PCLink SUM: checksum mismatch in RSD response");
        return false;
    }

    // RSD OK: [STX(1)][STN(2)][RSD,OK(6)][,XXXX(5) x count][SUM(2)][CR(1)][LF(1)]
    const int minLen = 1 + 2 + 6 + expectedCount * 5 + 2 + 2;
    if (response.size() < minLen) {
        error = m_lastError = QStringLiteral("PCLink SUM: RSD response too short (%1 < %2)")
                                  .arg(response.size()).arg(minLen);
        return false;
    }

    if (response.mid(3, 6) != "RSD,OK") {
        error = m_lastError = QStringLiteral("PCLink SUM: unexpected RSD response header: %1")
                                  .arg(QString::fromLatin1(response.mid(3, 6)));
        return false;
    }

    // Values start at index 9; each is ",XXXX" (5 bytes)
    out.clear();
    out.reserve(expectedCount);
    int pos = 9;
    const int dataEnd = response.size() - 4;  // SUM field starts here
    for (int i = 0; i < expectedCount; i++) {
        if (pos + 5 > dataEnd) {
            error = m_lastError = QStringLiteral("PCLink SUM: RSD data truncated at value %1").arg(i);
            return false;
        }
        if (response[pos] != ',') {
            error = m_lastError = QStringLiteral("PCLink SUM: expected ',' at position %1").arg(pos);
            return false;
        }
        bool ok = false;
        const quint16 val = response.mid(pos + 1, 4).toUShort(&ok, 16);
        if (!ok) {
            error = m_lastError = QStringLiteral("PCLink SUM: invalid hex value at position %1").arg(pos + 1);
            return false;
        }
        out.append(val);
        pos += 5;
    }
    return true;
}

bool PcLinkSumTCPClient::parseWsdResponse(const QByteArray &response, QString &error)
{
    if (checkNgResponse(response, error)) return false;

    if (!verifyFrameSum(response)) {
        error = m_lastError = QStringLiteral("PCLink SUM: checksum mismatch in WSD response");
        return false;
    }

    // WSD OK: [STX(1)][STN(2)][WSD,OK(6)][SUM(2)][CR(1)][LF(1)] = 13 bytes
    if (response.size() < 13) {
        error = m_lastError = QStringLiteral("PCLink SUM: WSD response too short");
        return false;
    }

    if (response.mid(3, 6) != "WSD,OK") {
        error = m_lastError = QStringLiteral("PCLink SUM: unexpected WSD response header: %1")
                                  .arg(QString::fromLatin1(response.mid(3, 6)));
        return false;
    }
    return true;
}

} // namespace Comm
} // namespace DataCollection
