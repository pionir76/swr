#include "ModbusRTUClient.h"

#include "../../../config/SystemConfig.h"
#include <QSerialPortInfo>
#include <QThread>
#include <QDebug>

namespace DataCollection {
namespace Comm {

ModbusRTUClient::ModbusRTUClient(const Model::DeviceConnection &connection)
    :m_connection(connection)
{
    const Rs485Config &rs485 = SystemConfig::rs485();

    m_serialPort.setPortName(rs485.device);
    m_serialPort.setBaudRate(rs485.baudRate);
    m_serialPort.setDataBits(rs485DataBits(rs485));
    m_serialPort.setParity(rs485Parity(rs485));
    m_serialPort.setStopBits(rs485StopBits(rs485));
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);
}

ModbusRTUClient::~ModbusRTUClient()
{
    disconnect();
}

bool ModbusRTUClient::connect()
{
    if (m_serialPort.isOpen()) {
        return true;
    }

    if (!m_serialPort.open(QIODevice::ReadWrite)) {
        m_lastError = QStringLiteral("RTU connect failed: %1").arg(m_serialPort.errorString());
        return false;
    }

    m_serialPort.clear();
    return true;
}

void ModbusRTUClient::disconnect()
{
    if (m_serialPort.isOpen()) {
        m_serialPort.close();
    }
}

bool ModbusRTUClient::isConnected() const
{
    return m_serialPort.isOpen();
}


bool ModbusRTUClient::readWords(int address,
               int count,
               QVector<quint16> &out,
               QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildReadRequest(0x03, address, count);
    QByteArray response;

    if (!sendRequest(request, response, error)) {
        return false;
    }

    if (!verifyResponse(response, 0x03, count * 2, error)) {
        return false;
    }

    out.clear();
    out.reserve(count);

    for (int i = 0; i < count; ++i) {
        const quint8 hi = static_cast<quint8>(response.at(3 + i * 2));
        const quint8 lo = static_cast<quint8>(response.at(4 + i * 2));
        out.append((hi << 8) | lo);
    }

    return true;
}

bool ModbusRTUClient::readBits(int address,
              int count,
              QVector<bool> &out,
              QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildReadRequest(0x01, address, count);
    QByteArray response;

    if (!sendRequest(request, response, error)) {
        return false;
    }

    if (!verifyResponse(response, 0x01, (count + 7) / 8, error)) {
        return false;
    }

    out.clear();
    out.reserve(count);

    for (int i = 0; i < count; ++i) {
        const int byteIndex = 3 + (i / 8);
        const quint8 value = static_cast<quint8>(response.at(byteIndex));
        out.append((value >> (i % 8)) & 0x01);
    }

    return true;
}


bool ModbusRTUClient::writeWords(int address,
                const QVector<quint16> &values,
                QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray request;
    quint8 expectedFc;

    if (values.size() == 1) {
        request = buildWriteSingleRegisterRequest(address, values.first());
        expectedFc = 0x06;
    } else {
        request = buildWriteMultipleRegistersRequest(address, values);
        expectedFc = 0x10;
    }

    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }

    return verifyResponse(response, expectedFc, 4, error);
}


bool ModbusRTUClient::writeBits(int address,
               const QVector<bool> &values,
               QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray request;
    quint8 expectedFc;

    if (values.size() == 1) {
        request = buildWriteSingleCoilRequest(address, values.first());
        expectedFc = 0x05;
    } else {
        request = buildWriteMultipleCoilsRequest(address, values);
        expectedFc = 0x0F;
    }

    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }

    return verifyResponse(response, expectedFc, 4, error);
}


QString ModbusRTUClient::errorString() const
{
    return m_lastError;
}

QByteArray ModbusRTUClient::buildReadRequest(quint8 functionCode, int startAddress, int quantity) const
{
    QByteArray frame;
    frame.append(static_cast<char>(m_connection.slaveId));
    frame.append(static_cast<char>(functionCode));
    frame.append(static_cast<char>((startAddress >> 8) & 0xFF));
    frame.append(static_cast<char>(startAddress & 0xFF));
    frame.append(static_cast<char>((quantity >> 8) & 0xFF));
    frame.append(static_cast<char>(quantity & 0xFF));

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray ModbusRTUClient::buildWriteSingleCoilRequest(int address, bool value) const
{
    QByteArray frame;
    frame.append(static_cast<char>(m_connection.slaveId));
    frame.append(static_cast<char>(0x05));
    frame.append(static_cast<char>((address >> 8) & 0xFF));
    frame.append(static_cast<char>(address & 0xFF));
    frame.append(static_cast<char>(value ? 0xFF : 0x00));
    frame.append(static_cast<char>(0x00));

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray ModbusRTUClient::buildWriteSingleRegisterRequest(int address, quint16 value) const
{
    QByteArray frame;
    frame.append(static_cast<char>(m_connection.slaveId));
    frame.append(static_cast<char>(0x06));
    frame.append(static_cast<char>((address >> 8) & 0xFF));
    frame.append(static_cast<char>(address & 0xFF));
    frame.append(static_cast<char>((value >> 8) & 0xFF));
    frame.append(static_cast<char>(value & 0xFF));

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray ModbusRTUClient::buildWriteMultipleCoilsRequest(int startAddress, const QVector<bool> &values) const
{
    const QByteArray packed = packCoils(values);
    const int quantity = values.size();
    QByteArray frame;
    frame.append(static_cast<char>(m_connection.slaveId));
    frame.append(static_cast<char>(0x0F));
    frame.append(static_cast<char>((startAddress >> 8) & 0xFF));
    frame.append(static_cast<char>(startAddress & 0xFF));
    frame.append(static_cast<char>((quantity >> 8) & 0xFF));
    frame.append(static_cast<char>(quantity & 0xFF));
    frame.append(static_cast<char>(packed.size()));
    frame.append(packed);

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

QByteArray ModbusRTUClient::buildWriteMultipleRegistersRequest(int startAddress, const QVector<quint16> &values) const
{
    const int quantity = values.size();
    QByteArray frame;
    frame.append(static_cast<char>(m_connection.slaveId));
    frame.append(static_cast<char>(0x10));
    frame.append(static_cast<char>((startAddress >> 8) & 0xFF));
    frame.append(static_cast<char>(startAddress & 0xFF));
    frame.append(static_cast<char>((quantity >> 8) & 0xFF));
    frame.append(static_cast<char>(quantity & 0xFF));
    frame.append(static_cast<char>(quantity * 2));
    for (quint16 value : values) {
        frame.append(static_cast<char>((value >> 8) & 0xFF));
        frame.append(static_cast<char>(value & 0xFF));
    }

    const quint16 crc = crc16(frame);
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}

quint16 ModbusRTUClient::crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (auto byte : data) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray ModbusRTUClient::packCoils(const QVector<bool> &values)
{
    const int byteCount = (values.size() + 7) / 8;
    QByteArray packed(byteCount, 0);
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i)) {
            packed[i / 8] |= static_cast<char>(1 << (i % 8));
        }
    }
    return packed;
}

bool ModbusRTUClient::sendRequest(const QByteArray &request,
                                  QByteArray &response,
                                  QString &error)
{
    m_serialPort.clear(QSerialPort::AllDirections);

    const qint64 written = m_serialPort.write(request);
    if (written != request.size() || !m_serialPort.waitForBytesWritten(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 1000)) {
        m_lastError = QStringLiteral("RTU write failed: %1").arg(m_serialPort.errorString());
        error = m_lastError;
        return false;
    }

    if (!m_serialPort.waitForReadyRead(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 3000)) {
        m_lastError = QStringLiteral("RTU timeout waiting for response");
        error = m_lastError;
        return false;
    }

    response = m_serialPort.readAll();
    while (m_serialPort.waitForReadyRead(50)) {
        response.append(m_serialPort.readAll());
    }

    if (response.size() < 5) {
        m_lastError = QStringLiteral("RTU response too short");
        error = m_lastError;
        return false;
    }

    return true;
}

bool ModbusRTUClient::verifyResponse(const QByteArray &response,
                                     quint8 expectedFunction,
                                     int expectedDataBytes,
                                     QString &error) const
{
    if (response.size() < 5) {
        const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("RTU response too short");
        error = m_lastError;
        return false;
    }

    const int frameSize = response.size();
    const quint16 receivedCrc = static_cast<quint8>(response.at(frameSize - 2)) |
                                (static_cast<quint8>(response.at(frameSize - 1)) << 8);
    const QByteArray payload = response.mid(0, frameSize - 2);
    if (crc16(payload) != receivedCrc) {
        const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("RTU CRC mismatch");
        error = m_lastError;
        return false;
    }

    const quint8 functionCode = static_cast<quint8>(payload.at(1));
    if (functionCode == static_cast<quint8>(expectedFunction | 0x80)) {
        const quint8 exceptionCode = static_cast<quint8>(payload.at(2));
        const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("Modbus exception %1").arg(exceptionCode);
        error = m_lastError;
        return false;
    }

    if (functionCode != expectedFunction) {
        const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("RTU function code mismatch");
        error = m_lastError;
        return false;
    }

    if (expectedFunction == 0x01 || expectedFunction == 0x03) {
        const int byteCount = static_cast<quint8>(payload.at(2));
        if (byteCount != expectedDataBytes) {
            const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("RTU byte count mismatch");
            error = m_lastError;
            return false;
        }
    } else if (expectedDataBytes >= 0) {
        if ((payload.size() - 2) != expectedDataBytes) {
            const_cast<ModbusRTUClient *>(this)->m_lastError = QStringLiteral("RTU response length mismatch");
            error = m_lastError;
            return false;
        }
    }

    return true;
}

} // namespace Comm
} // namespace DataCollection
