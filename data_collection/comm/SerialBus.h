#pragma once

#include "../../config/AppConfig.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QString>

namespace DataCollection {
namespace Comm {

class SerialBus {
public:
    explicit SerialBus(const Rs485Config &cfg);

    bool open(QString &error);
    void close();
    bool isOpen() const;

    void       clearBuffers();
    qint64     write(const QByteArray &data);
    bool       waitForBytesWritten(int timeoutMs);
    bool       waitForReadyRead(int timeoutMs);
    QByteArray readAll();
    QString    errorString() const;

    // Modbus RTU needs a silent interval between frames on the shared bus.
    void       markIdle();                  // a request/response exchange just ended
    void       waitForIdle(int minGapMs);   // block until the bus has been quiet for minGapMs

private:
    QSerialPort   m_port;
    QElapsedTimer m_idleSince;
};

} // namespace Comm
} // namespace DataCollection
