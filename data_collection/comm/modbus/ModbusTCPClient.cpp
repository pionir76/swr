#include "ModbusTCPClient.h"

namespace DataCollection {
namespace Comm {

ModbusTCPClient::ModbusTCPClient(const Model::DeviceConnection &connection)
    : m_connection(connection)
{
}

ModbusTCPClient::~ModbusTCPClient()
{
    disconnect();
}

bool ModbusTCPClient::connect()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    m_socket.connectToHost(m_connection.ipAddress, static_cast<quint16>(m_connection.tcpPort));
    if (!m_socket.waitForConnected(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 5000)) {
        m_lastError = QStringLiteral("TCP connect failed: %1").arg(m_socket.errorString());
        return false;
    }

    return true;
}

void ModbusTCPClient::disconnect()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.disconnectFromHost();
        m_socket.waitForDisconnected(1000);
    }
}

bool ModbusTCPClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}


bool ModbusTCPClient::readWords(int address, int count, QVector<quint16> &out, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildTcpRequest(0x03, address, count, nullptr, nullptr);
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
        const quint8 hi = static_cast<quint8>(response.at(9 + i * 2));
        const quint8 lo = static_cast<quint8>(response.at(10 + i * 2));
        out.append((hi << 8) | lo);
    }

    return true;
}

bool ModbusTCPClient::readBits(int address, int count, QVector<bool> &out, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    const QByteArray request = buildTcpRequest(0x01, address, count, nullptr, nullptr);
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
        const int byteIndex = 9 + (i / 8);
        const quint8 value = static_cast<quint8>(response.at(byteIndex));
        out.append((value >> (i % 8)) & 0x01);
    }

    return true;
}

bool ModbusTCPClient::writeWords(int address, const QVector<quint16> &values, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray request;
    quint8 expectedFc;

    if (values.size() == 1) {
        request = buildTcpRequest(0x06, address, values.first(), nullptr, nullptr);
        expectedFc = 0x06;
    } else {
        request = buildTcpRequest(0x10, address, values.size(), nullptr, &values);
        expectedFc = 0x10;
    }

    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }
    return verifyResponse(response, expectedFc, 4, error);
}

bool ModbusTCPClient::writeBits(int address, const QVector<bool> &values, QString &error)
{
    if (!isConnected() && !connect()) {
        error = m_lastError;
        return false;
    }

    QByteArray request;
    quint8 expectedFc;

    if (values.size() == 1) {
        const int coilValue = values.first() ? 0xFF00 : 0x0000;
        request = buildTcpRequest(0x05, address, coilValue, nullptr, nullptr);
        expectedFc = 0x05;
    } else {
        request = buildTcpRequest(0x0F, address, values.size(), &values, nullptr);
        expectedFc = 0x0F;
    }

    QByteArray response;
    if (!sendRequest(request, response, error)) {
        return false;
    }

    return verifyResponse(response, expectedFc, 4, error);
}

QString ModbusTCPClient::errorString() const
{
    return m_lastError;
}

QByteArray ModbusTCPClient::buildTcpRequest(quint8 functionCode,
                                            int startAddress,
                                            int quantityOrValue,
                                            const QVector<bool> *coils,
                                            const QVector<quint16> *registers)
{
    QByteArray pdu;
    pdu.append(static_cast<char>(m_connection.slaveId));
    pdu.append(static_cast<char>(functionCode));
    pdu.append(static_cast<char>((startAddress >> 8) & 0xFF));
    pdu.append(static_cast<char>(startAddress & 0xFF));

    switch (functionCode) {
    case 0x01:
    case 0x03:
    case 0x05:
    case 0x06:
        pdu.append(static_cast<char>((quantityOrValue >> 8) & 0xFF));
        pdu.append(static_cast<char>(quantityOrValue & 0xFF));
        break;
    case 0x0F:
        pdu.append(static_cast<char>((quantityOrValue >> 8) & 0xFF));
        pdu.append(static_cast<char>(quantityOrValue & 0xFF));
        if (coils) {
            const QByteArray packed = packCoils(*coils);
            pdu.append(static_cast<char>(packed.size()));
            pdu.append(packed);
        }
        break;
    case 0x10:
        pdu.append(static_cast<char>((quantityOrValue >> 8) & 0xFF));
        pdu.append(static_cast<char>(quantityOrValue & 0xFF));
        if (registers) {
            pdu.append(static_cast<char>(quantityOrValue * 2));
            for (quint16 value : *registers) {
                pdu.append(static_cast<char>((value >> 8) & 0xFF));
                pdu.append(static_cast<char>(value & 0xFF));
            }
        }
        break;
    default:
        break;
    }

    QByteArray frame;
    frame.append(static_cast<char>((m_transactionId >> 8) & 0xFF));
    frame.append(static_cast<char>(m_transactionId & 0xFF));
    frame.append(char(0));
    frame.append(char(0));
    frame.append(static_cast<char>((pdu.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(pdu.size() & 0xFF));
    frame.append(pdu);

    ++m_transactionId;
    return frame;
}

QByteArray ModbusTCPClient::packCoils(const QVector<bool> &values)
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

bool ModbusTCPClient::sendRequest(const QByteArray &request, QByteArray &response, QString &error)
{
    m_socket.write(request);
    if (!m_socket.waitForBytesWritten(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 3000)) {
        m_lastError = QStringLiteral("TCP write failed: %1").arg(m_socket.errorString());
        error = m_lastError;
        return false;
    }

    if (!m_socket.waitForReadyRead(m_connection.timeoutMs > 0 ? m_connection.timeoutMs : 3000)) {
        m_lastError = QStringLiteral("TCP timeout waiting for response");
        error = m_lastError;
        return false;
    }

    response = m_socket.readAll();
    while (m_socket.waitForReadyRead(200)) {
        response.append(m_socket.readAll());
    }

    if (response.size() < 9) {
        m_lastError = QStringLiteral("TCP response too short");
        error = m_lastError;
        return false;
    }
    return true;
}

    //-------------------------------------------------------------------------//
    // Modbus TCP Response Frame Structure (MBAP Header + PDU)
    //
    // ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬──────────┐
    // │ [0]    │ [1]    │ [2]    │ [3]    │ [4]    │ [5]    │ [6]    │ [7]    │ [8]    │ [9..]    │
    // │ Tx ID  │ Tx ID  │ Proto  │ Proto  │ Length │ Length │ Unit   │   FC   │  Data  │  Data    │
    // │  Hi    │  Lo    │  Hi    │  Lo    │  Hi    │  Lo    │  ID    │        │        │          │
    // │        │        │(0x00)  │(0x00)  │        │        │(SlaveID│        │        │          │
    // └────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴──────────┘
    // <---------------- MBAP Header (6 bytes) ---------------->|<-------- PDU ------------------>
    //
    // [0][1]  Transaction ID : Echo of the ID set by master in the request
    // [2][3]  Protocol ID    : Always 0x0000 for Modbus TCP
    // [4][5]  Length         : Number of bytes from [6] to end (Unit ID + FC + Data)
    // [6]     Unit ID        : Slave ID — same role as SlaveID in RTU/ASCII
    // [7]     Function Code  : Echo of request FC (FC | 0x80 on error)
    //
    // -- Read Response (FC01 / FC03) --
    // [8]     Byte Count     : Number of data bytes that follow (FC03: count x 2, FC01: ceil(count/8))
    // [9..]   Data           : Register or coil values
    //
    // -- Write Response (FC05 / FC06 / FC0F / FC10) --
    // [8][9]  Start Address  : Echo of requested address
    // [10][11] Value / Count : Echo of written value or quantity
    //
    // -- Exception Response --
    // [7]     FC | 0x80      : e.g. FC03 error -> 0x83
    // [8]     Exception Code : 0x01 Illegal Function
    //                          0x02 Illegal Data Address
    //                          0x03 Illegal Data Value
    //                          0x04 Slave Device Failure
    //
    // Minimum valid frame size = MBAP(6) + UnitID(1) + FC(1) + Data(1) = 9 bytes
    //-------------------------------------------------------------------------//
bool ModbusTCPClient::verifyResponse(const QByteArray &response,
                                     quint8 expectedFunction,
                                     int expectedDataBytes,
                                     QString &error) const
{
    //---------------------------------------------------------------------//
    // Check if the response is long enough to contain the minimum required fields
    //---------------------------------------------------------------------//
    if (response.size() < 9) {
        m_lastError = QStringLiteral("TCP response too short");
        error = m_lastError;
        return false;
    }

    //---------------------------------------------------------------------//
    // verify Unit ID matches the requested device
    //---------------------------------------------------------------------//
    if (static_cast<quint8>(response.at(6)) != static_cast<quint8>(m_connection.slaveId)) {
        m_lastError = QStringLiteral("TCP unit ID mismatch (expected %1, got %2)")
            .arg(m_connection.slaveId)
            .arg(static_cast<quint8>(response.at(6)));
        error = m_lastError;
        return false;
    }

    //---------------------------------------------------------------------//
    // Modbus exception response: Sender FC | 0x80, Exception Code
    // e.g., if the request FC is 0x03, the exception response FC will be 0x83, 
    // followed by the exception code. 
    // FC = 0x03  →  error response FC = 0x83
    // FC = 0x06  →  error response FC = 0x86
    //---------------------------------------------------------------------//
    const quint8 functionCode = static_cast<quint8>(response.at(7));
    if (functionCode == static_cast<quint8>(expectedFunction | 0x80)) {
        const quint8 exceptionCode = static_cast<quint8>(response.at(8));
        m_lastError = QStringLiteral("Modbus exception %1").arg(exceptionCode);
        error = m_lastError;
        return false;
    }

    if (functionCode != expectedFunction) {
        m_lastError = QStringLiteral("TCP function code mismatch");
        error = m_lastError;
        return false;
    }

    //---------------------------------------------------------------------//
    // Check the byte count for read functions (0x01, 0x03) 
    // or the response length for write functions
    //---------------------------------------------------------------------//
    if (expectedFunction == 0x01 || expectedFunction == 0x03) {
        const quint8 byteCount = static_cast<quint8>(response.at(8));
        if (byteCount != expectedDataBytes) {
            m_lastError = QStringLiteral("TCP byte count mismatch");
            error = m_lastError;
            return false;
        }
        //---------------------------------------------------------------------//
        // Verify actual buffer holds enough bytes beyond the header
        //---------------------------------------------------------------------//
        if (response.size() < 9 + expectedDataBytes) {
            m_lastError = QStringLiteral("TCP response buffer too short");
            error = m_lastError;
            return false;
        }
    } else if (expectedDataBytes >= 0) {
        const int actualBytes = response.size() - 8;
        if (actualBytes != expectedDataBytes) {
            m_lastError = QStringLiteral("TCP response length mismatch");
            error = m_lastError;
            return false;
        }
    }

    return true;
}

} // namespace Comm
} // namespace DataCollection
