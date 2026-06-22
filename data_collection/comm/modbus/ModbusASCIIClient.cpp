#include "ModbusASCIIClient.h"
#include "../../../config/SystemConfig.h"

#include <QString>

namespace DataCollection {
namespace Comm {

ModbusASCIIClient::ModbusASCIIClient(const Model::DeviceConnection &connection)
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

ModbusASCIIClient::~ModbusASCIIClient()
{
    disconnect();
}

bool ModbusASCIIClient::connect()
{
    if (m_serialPort.isOpen()) {
        return true;
    }

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        m_lastError = QStringLiteral("ASCII connect failed: %1").arg(m_serialPort.errorString());
        return false;
    }

    m_serialPort.clear();
    return true;
}

void ModbusASCIIClient::disconnect()
{
    if (m_serialPort.isOpen()) {
        m_serialPort.close();
    }
}

bool ModbusASCIIClient::isConnected() const
{
    return m_serialPort.isOpen();
}

bool ModbusASCIIClient::readWords(int address, int count,
                                  QVector<quint16> &out, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildAsciiFrame(buildReadRequest(0x03, address, count));
    QByteArray pdu;
    if (!sendRequest(request, pdu, error)) {
        return false;
    }

    if (!verifyResponse(pdu, 0x03, count * 2, error)) {
        return false;
    }

    out.clear();
    out.reserve(count);

    for (int i = 0; i < count; ++i) {
        const quint8 hi = static_cast<quint8>(pdu.at(3 + i * 2));
        const quint8 lo = static_cast<quint8>(pdu.at(4 + i * 2));
        out.append((hi << 8) | lo);
    }

    return true;
}

bool ModbusASCIIClient::readBits(int address, int count,
                                 QVector<bool> &out, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildAsciiFrame(buildReadRequest(0x01, address, count));
    QByteArray pdu;
    if (!sendRequest(request, pdu, error)) {
        return false;
    }

    if (!verifyResponse(pdu, 0x01, (count + 7) / 8, error)) {
        return false;
    }

    out.clear();
    out.reserve(count);

    for (int i = 0; i < count; ++i) {
        const int byteIndex = 3 + (i / 8);
        const quint8 value = static_cast<quint8>(pdu.at(byteIndex));
        out.append((value >> (i % 8)) & 0x01);
    }

    return true;
}

bool ModbusASCIIClient::writeWords(int address, const QVector<quint16> &values, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray pdu;
    quint8 expectedFc;

    if (values.size() == 1) {
        pdu = buildWriteSingleRegisterRequest(address, values.first());
        expectedFc = 0x06;
    } else {
        pdu = buildWriteMultipleRegistersRequest(address, values);
        expectedFc = 0x10;
    }

    const QByteArray request = buildAsciiFrame(pdu);
    QByteArray responsePdu;
    if (!sendRequest(request, responsePdu, error)) {
        return false;
    }

    return verifyResponse(responsePdu, expectedFc, 4, error);
}

bool ModbusASCIIClient::writeBits(int address, const QVector<bool> &values, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray pdu;
    quint8 expectedFc;

    if (values.size() == 1) {
        pdu = buildWriteSingleCoilRequest(address, values.first());
        expectedFc = 0x05;
    } else {
        pdu = buildWriteMultipleCoilsRequest(address, values);
        expectedFc = 0x0F;
    }

    const QByteArray request = buildAsciiFrame(pdu);
    QByteArray responsePdu;
    if (!sendRequest(request, responsePdu, error)) {
        return false;
    }

    return verifyResponse(responsePdu, expectedFc, 4, error);
}

QString ModbusASCIIClient::errorString() const
{
    return m_lastError;
}

QByteArray ModbusASCIIClient::buildAsciiFrame(const QByteArray &pdu) const
{
    const quint8 lrcValue = lrc(pdu);
    const QByteArray frame = pdu.toHex().toUpper();
    const QString asciiFrame = QStringLiteral(":%1%2\r\n")
        .arg(QString::fromLatin1(frame))
        .arg(QStringLiteral("%1").arg(lrcValue, 2, 16, QLatin1Char('0')).toUpper());
    return asciiFrame.toLatin1();
}

QByteArray ModbusASCIIClient::buildReadRequest(quint8 functionCode, int startAddress, int quantity) const
{
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(functionCode));
    pdu.append(static_cast<char>((startAddress >> 8) & 0xFF));
    pdu.append(static_cast<char>(startAddress & 0xFF));
    pdu.append(static_cast<char>((quantity >> 8) & 0xFF));
    pdu.append(static_cast<char>(quantity & 0xFF));
    return pdu;
}

QByteArray ModbusASCIIClient::buildWriteSingleCoilRequest(int address, bool value) const
{
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(0x05));
    pdu.append(static_cast<char>((address >> 8) & 0xFF));
    pdu.append(static_cast<char>(address & 0xFF));
    pdu.append(static_cast<char>(value ? 0xFF : 0x00));
    pdu.append(static_cast<char>(0x00));
    return pdu;
}

QByteArray ModbusASCIIClient::buildWriteSingleRegisterRequest(int address, quint16 value) const
{
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(0x06));
    pdu.append(static_cast<char>((address >> 8) & 0xFF));
    pdu.append(static_cast<char>(address & 0xFF));
    pdu.append(static_cast<char>((value >> 8) & 0xFF));
    pdu.append(static_cast<char>(value & 0xFF));
    return pdu;
}

QByteArray ModbusASCIIClient::buildWriteMultipleCoilsRequest(int startAddress, const QVector<bool> &values) const
{
    const QByteArray coilBytes = packCoils(values);
    const int quantity = values.size();
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(0x0F));
    pdu.append(static_cast<char>((startAddress >> 8) & 0xFF));
    pdu.append(static_cast<char>(startAddress & 0xFF));
    pdu.append(static_cast<char>((quantity >> 8) & 0xFF));
    pdu.append(static_cast<char>(quantity & 0xFF));
    pdu.append(static_cast<char>(coilBytes.size()));
    pdu.append(coilBytes);
    return pdu;
}

QByteArray ModbusASCIIClient::buildWriteMultipleRegistersRequest(int startAddress, const QVector<quint16> &values) const
{
    const int quantity = values.size();
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(0x10));
    pdu.append(static_cast<char>((startAddress >> 8) & 0xFF));
    pdu.append(static_cast<char>(startAddress & 0xFF));
    pdu.append(static_cast<char>((quantity >> 8) & 0xFF));
    pdu.append(static_cast<char>(quantity & 0xFF));
    pdu.append(static_cast<char>(quantity * 2));
    for (quint16 value : values) {
        pdu.append(static_cast<char>((value >> 8) & 0xFF));
        pdu.append(static_cast<char>(value & 0xFF));
    }
    return pdu;
}

QByteArray ModbusASCIIClient::packCoils(const QVector<bool> &values)
{
    const int byteCount = (values.size() + 7) / 8;
    QByteArray bytes(byteCount, 0);
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i)) {
            bytes[i / 8] |= static_cast<char>(1 << (i % 8));
        }
    }
    return bytes;
}

quint8 ModbusASCIIClient::lrc(const QByteArray &data)
{
    quint8 sum = 0;
    for (auto byte : data) {
        sum += static_cast<quint8>(byte);
    }
    return static_cast<quint8>((~sum + 1) & 0xFF);
}

bool ModbusASCIIClient::sendRequest(const QByteArray &request,
                                    QByteArray &responsePdu,
                                    QString &error)
{
    m_serialPort.clear(QSerialPort::AllDirections);
    const qint64 written = m_serialPort.write(request);
    if (written != request.size() || !m_serialPort.waitForBytesWritten(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 1000)) {
        m_lastError = QStringLiteral("ASCII write failed: %1").arg(m_serialPort.errorString());
        error = m_lastError;
        return false;
    }

    QByteArray response;
    if (!m_serialPort.waitForReadyRead(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 3000)) {
        m_lastError = QStringLiteral("ASCII timeout waiting for response");
        error = m_lastError;
        return false;
    }

    response.append(m_serialPort.readAll());
    while (m_serialPort.waitForReadyRead(50)) {
        response.append(m_serialPort.readAll());
    }

    return parseAsciiFrame(response, responsePdu, error);
}

bool ModbusASCIIClient::parseAsciiFrame(const QByteArray &frame,
                                        QByteArray &responsePdu,
                                        QString &error) const
{
    const int start = frame.indexOf(':');
    const int end = frame.indexOf("\r\n", start >= 0 ? start : 0);
    if (start < 0 || end < 0) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII response framing error");
        error = m_lastError;
        return false;
    }

    const QByteArray payload = frame.mid(start + 1, end - start - 1);
    if (payload.size() % 2 != 0) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII invalid hex length");
        error = m_lastError;
        return false;
    }

    const QByteArray raw = QByteArray::fromHex(payload);
    if (raw.size() < 3) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII response too short");
        error = m_lastError;
        return false;
    }

    const quint8 receivedLrc = static_cast<quint8>(raw.at(raw.size() - 1));
    const QByteArray data = raw.left(raw.size() - 1);
    if (lrc(data) != receivedLrc) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII LRC mismatch");
        error = m_lastError;
        return false;
    }

    responsePdu = data;
    return true;
}

bool ModbusASCIIClient::verifyResponse(const QByteArray &responsePdu,
                                       quint8 expectedFunction,
                                       int expectedDataBytes,
                                       QString &error) const
{
    if (responsePdu.size() < 3) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII response too short");
        error = m_lastError;
        return false;
    }

    const quint8 functionCode = static_cast<quint8>(responsePdu.at(1));
    if (functionCode == static_cast<quint8>(expectedFunction | 0x80)) {
        const quint8 exceptionCode = static_cast<quint8>(responsePdu.at(2));
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("Modbus exception %1").arg(exceptionCode);
        error = m_lastError;
        return false;
    }

    if (functionCode != expectedFunction) {
        const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII function code mismatch");
        error = m_lastError;
        return false;
    }

    if (expectedFunction == 0x01 || expectedFunction == 0x03) {
        const int byteCount = static_cast<quint8>(responsePdu.at(2));
        if (byteCount != expectedDataBytes) {
            const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII byte count mismatch");
            error = m_lastError;
            return false;
        }
    } else if (expectedDataBytes >= 0) {
        if ((responsePdu.size() - 2) != expectedDataBytes) {
            const_cast<ModbusASCIIClient *>(this)->m_lastError = QStringLiteral("ASCII response length mismatch");
            error = m_lastError;
            return false;
        }
    }

    return true;
}

} // namespace Comm
} // namespace DataCollection
