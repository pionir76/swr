#include "SerialBus.h"

#include <QThread>

namespace DataCollection {
namespace Comm {

SerialBus::SerialBus(const Rs485Config &cfg)
{
    m_port.setPortName(cfg.device);
    m_port.setBaudRate(cfg.baudRate);
    m_port.setDataBits(rs485DataBits(cfg));
    m_port.setParity(rs485Parity(cfg));
    m_port.setStopBits(rs485StopBits(cfg));
    m_port.setFlowControl(QSerialPort::NoFlowControl);
}

bool SerialBus::open(QString &error)
{
    if (m_port.isOpen()){
        return true;
    }
        
    if (!m_port.open(QIODevice::ReadWrite)) {
        error = QStringLiteral("SerialBus: failed to open %1 — %2")
                    .arg(m_port.portName(), m_port.errorString());
        return false;
    }
    return true;
}

void SerialBus::close()
{
    if (m_port.isOpen()){
        m_port.close();
    }
}

bool SerialBus::isOpen() const
{
    return m_port.isOpen();
}

void SerialBus::clearBuffers()
{
    m_port.clear(QSerialPort::AllDirections);
}

qint64 SerialBus::write(const QByteArray &data)
{
    return m_port.write(data);
}

bool SerialBus::waitForBytesWritten(int timeoutMs)
{
    return m_port.waitForBytesWritten(timeoutMs);
}

bool SerialBus::waitForReadyRead(int timeoutMs)
{
    return m_port.waitForReadyRead(timeoutMs);
}

QByteArray SerialBus::readAll()
{
    return m_port.readAll();
}

QByteArray SerialBus::readUntilSilence(int silenceMs, int maxMs)
{
    // The maxMs cap matters on a busy bus (a second master, line noise):
    // silence may never come, and without the cap the polling thread never returns.
    QByteArray data;
    QElapsedTimer elapsed;
    elapsed.start();

    for (;;) {
        const int remaining = maxMs - static_cast<int>(elapsed.elapsed());
        if (remaining <= 0 || !m_port.waitForReadyRead(qMin(silenceMs, remaining)))
            break;
        data.append(m_port.readAll());
    }
    return data;
}

QString SerialBus::errorString() const
{
    return m_port.errorString();
}

void SerialBus::markIdle()
{
    m_idleSince.start();
}

void SerialBus::waitForIdle(int minGapMs)
{
    if (!m_idleSince.isValid())
        return;
    const qint64 quietMs = m_idleSince.elapsed();
    if (quietMs < minGapMs)
        QThread::msleep(static_cast<unsigned long>(minGapMs - quietMs));
}

} // namespace Comm
} // namespace DataCollection
