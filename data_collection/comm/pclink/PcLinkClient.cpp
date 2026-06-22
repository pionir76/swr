#include "PcLinkClient.h"
#include "../../../config/SystemConfig.h"

#include <QString>

// ---------------------------------------------------------------------------
// Samwontech PCLink ASCII protocol
//
// Read request frame layout  (all ASCII, terminated with CR):
//   ENQ(05h) + STN(2) + CMD(2) + CNT(2) + ADDR(4) + NUM(2) + SUM(2) + EOT(04h)
//
// Read response frame layout:
//   STX(02h) + STN(2) + CMD(2) + CNT(2) + DATA(count*4 hex chars) + SUM(2) + ETX(03h)
//
// Write request frame layout:
//   ENQ(05h) + STN(2) + CMD(2) + CNT(2) + ADDR(4) + NUM(2) + DATA(count*4) + SUM(2) + EOT(04h)
//
// Write response frame layout:
//   ACK(06h)  on success
//   NAK(15h)  on failure
//
// STN  = station number, zero-padded 2-char hex  (e.g. "01")
// CMD  = "RD" (read data register) / "WD" (write data register)
// CNT  = block count = "01" (single block request)
// ADDR = start address, zero-padded 4-char hex
// NUM  = word count,    zero-padded 2-char hex
// DATA = each word as 4-char hex, big-endian
// SUM  = checksum: sum of all ASCII bytes between ENQ/STX and SUM, lower byte, 2-char hex
//
// NOTE: Verify these details against the actual Samwontech PCLink specification
//       for your target PLC model before use.
// ---------------------------------------------------------------------------

namespace DataCollection {
namespace Comm {

PcLinkClient::PcLinkClient(const Model::DeviceConnection &connection)
    : m_connection(connection)
{
    const Rs485Config &rs485 = SystemConfig::rs485();
    m_serialPort.setPortName(rs485.device);
    m_serialPort.setBaudRate(rs485.baudRate);
    m_serialPort.setDataBits(rs485DataBits(rs485));
    m_serialPort.setParity(rs485Parity(rs485));
    m_serialPort.setStopBits(rs485StopBits(rs485));
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);
}

PcLinkClient::~PcLinkClient()
{
    disconnect();
}

bool PcLinkClient::connect()
{
    if (m_serialPort.isOpen()) {
        return true;
    }

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        m_lastError = QStringLiteral("PCLink connect failed: %1").arg(m_serialPort.errorString());
        return false;
    }

    m_serialPort.clear();
    return true;
}

void PcLinkClient::disconnect()
{
    if (m_serialPort.isOpen()) {
        m_serialPort.close();
    }
}

bool PcLinkClient::isConnected() const
{
    return m_serialPort.isOpen();
}

bool PcLinkClient::readWords(int address, int count,
                             QVector<quint16> &out, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildReadFrame(address, count);
    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }

    return parseReadResponse(response, count, out, error);
}

bool PcLinkClient::writeWords(int address, const QVector<quint16> &values, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildWriteFrame(address, values);
    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }

    return parseWriteResponse(response, error);
}

bool PcLinkClient::readBits(int, int, QVector<bool> &, QString &error)
{
    m_lastError = QStringLiteral("PCLink: bit register read not supported");
    error = m_lastError;
    return false;
}

bool PcLinkClient::writeBits(int, const QVector<bool> &, QString &error)
{
    m_lastError = QStringLiteral("PCLink: bit register write not supported");
    error = m_lastError;
    return false;
}

QString PcLinkClient::errorString() const
{
    return m_lastError;
}

// --- Frame builders ---------------------------------------------------------

QByteArray PcLinkClient::buildReadFrame(int address, int count) const
{
    const QString stn  = QStringLiteral("%1").arg(m_connection.slaveId, 2, 16, QLatin1Char('0')).toUpper();
    const QString addr = QStringLiteral("%1").arg(address, 4, 16, QLatin1Char('0')).toUpper();
    const QString num  = QStringLiteral("%1").arg(count,   2, 16, QLatin1Char('0')).toUpper();

    const QByteArray body = (stn + QLatin1String("RD") + QLatin1String("01") + addr + num).toLatin1();

    QByteArray frame;
    frame.append(char(0x05));   // ENQ
    frame.append(body);
    frame.append(checksum(body));
    frame.append(char(0x04));   // EOT
    return frame;
}

QByteArray PcLinkClient::buildWriteFrame(int address, const QVector<quint16> &values) const
{
    const QString stn  = QStringLiteral("%1").arg(m_connection.slaveId, 2, 16, QLatin1Char('0')).toUpper();
    const QString addr = QStringLiteral("%1").arg(address,       4, 16, QLatin1Char('0')).toUpper();
    const QString num  = QStringLiteral("%1").arg(values.size(), 2, 16, QLatin1Char('0')).toUpper();

    QString dataStr;
    for (quint16 word : values) {
        dataStr += QStringLiteral("%1").arg(word, 4, 16, QLatin1Char('0')).toUpper();
    }

    const QByteArray body = (stn + QLatin1String("WD") + QLatin1String("01") + addr + num + dataStr).toLatin1();

    QByteArray frame;
    frame.append(char(0x05));   // ENQ
    frame.append(body);
    frame.append(checksum(body));
    frame.append(char(0x04));   // EOT
    return frame;
}

// --- Response parsers -------------------------------------------------------

bool PcLinkClient::parseReadResponse(const QByteArray &response,
                                     int expectedCount,
                                     QVector<quint16> &out,
                                     QString &error) const
{
    // STX(1) + STN(2) + CMD(2) + CNT(2) + DATA(count*4) + SUM(2) + ETX(1)
    const int minLen = 1 + 2 + 2 + 2 + expectedCount * 4 + 2 + 1;
    if (response.size() < minLen) {
        const_cast<PcLinkClient *>(this)->m_lastError = QStringLiteral("PCLink read response too short");
        error = m_lastError;
        return false;
    }

    if (static_cast<quint8>(response.at(0)) != 0x02) {
        const_cast<PcLinkClient *>(this)->m_lastError = QStringLiteral("PCLink: missing STX in response");
        error = m_lastError;
        return false;
    }

    const QByteArray body = response.mid(1, response.size() - 4);
    const QByteArray receivedSum = response.mid(response.size() - 3, 2);
    if (checksum(body) != receivedSum) {
        const_cast<PcLinkClient *>(this)->m_lastError = QStringLiteral("PCLink: checksum mismatch in read response");
        error = m_lastError;
        return false;
    }

    const int dataOffset = 7; // STX(1) + STN(2) + CMD(2) + CNT(2)
    out.clear();
    out.reserve(expectedCount);
    for (int i = 0; i < expectedCount; ++i) {
        const QByteArray wordHex = response.mid(dataOffset + i * 4, 4);
        bool ok = false;
        const quint16 word = static_cast<quint16>(wordHex.toUShort(&ok, 16));
        if (!ok) {
            const_cast<PcLinkClient *>(this)->m_lastError = QStringLiteral("PCLink: invalid hex data in response");
            error = m_lastError;
            return false;
        }
        out.append(word);
    }

    return true;
}

bool PcLinkClient::parseWriteResponse(const QByteArray &response, QString &error) const
{
    if (response.isEmpty() || static_cast<quint8>(response.at(0)) != 0x06) {
        const_cast<PcLinkClient *>(this)->m_lastError = QStringLiteral("PCLink: write NAK or unexpected response");
        error = m_lastError;
        return false;
    }
    return true;
}

// --- Transport --------------------------------------------------------------

bool PcLinkClient::sendRequest(const QByteArray &request,
                               QByteArray &response,
                               QString &error)
{
    m_serialPort.clear(QSerialPort::AllDirections);

    const qint64 written = m_serialPort.write(request);
    if (written != request.size() || !m_serialPort.waitForBytesWritten(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 1000)) {
        m_lastError = QStringLiteral("PCLink write failed: %1").arg(m_serialPort.errorString());
        error = m_lastError;
        return false;
    }

    if (!m_serialPort.waitForReadyRead(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 3000)) {
        m_lastError = QStringLiteral("PCLink timeout waiting for response");
        error = m_lastError;
        return false;
    }

    response = m_serialPort.readAll();
    while (m_serialPort.waitForReadyRead(50)) {
        response.append(m_serialPort.readAll());
    }

    return true;
}

QByteArray PcLinkClient::checksum(const QByteArray &data)
{
    quint8 sum = 0;
    for (auto byte : data) {
        sum += static_cast<quint8>(byte);
    }
    return QStringLiteral("%1").arg(sum, 2, 16, QLatin1Char('0')).toUpper().toLatin1();
}

} // namespace Comm
} // namespace DataCollection
