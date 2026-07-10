#pragma once

#include <QVector>
#include <QString>

namespace DataCollection {
namespace Comm {

class IDeviceClient {
public:
    //---------------------------------------------------------------------------------------------------------//
    // Virtual destructor to ensure proper cleanup of derived classes
    // This is important for polymorphic base classes to avoid undefined behavior (UB)
    //
    // IDeviceClient* client = new ModbusRTUClient(conn, bus); 
    // delete client;
    //
    // In this case, the destructor of ModbusRTUClient will be called, ensuring that any resources allocated by the derived class are properly released.
    // ModbusRTUClient::~ModbusRTUClient()  <-- Naver call this without a virtual destructor in the base class
    //
    // So, If no defalut virtual destructor, the derived class destructor will not be called, 
    // leading to resource leaks and undefined behavior.
    //---------------------------------------------------------------------------------------------------------//
    virtual ~IDeviceClient() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual bool readWords(int address, int count, QVector<quint16> &out, QString &error) = 0;
    virtual bool readBits(int address, int count, QVector<bool> &out, QString &error) = 0;
    virtual bool writeWords(int address, const QVector<quint16> &values, QString &error) = 0;
    virtual bool writeBits(int address, const QVector<bool> &values, QString &error) = 0;

    virtual QString errorString() const = 0;
};

} // namespace Comm
} // namespace DataCollection
