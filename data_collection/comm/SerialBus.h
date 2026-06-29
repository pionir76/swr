#pragma once

#include "../../config/AppConfig.h"

#include <QByteArray>
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

private:
    QSerialPort m_port;
};

} // namespace Comm
} // namespace DataCollection
