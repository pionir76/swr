#include "TcpWorker.h"
#include "../processor/DataCollector.h"
#include "../../utils/Logger.h"

namespace DataCollection {
namespace Polling {

TcpWorker::TcpWorker(Model::DeviceInfo device,
                     std::shared_ptr<Store::RegisterTable> table,
                     std::shared_ptr<Store::DeviceList> deviceList,
                     QObject *parent)
    : QThread(parent)
    , m_device(std::move(device))
    , m_table(std::move(table))
    , m_deviceList(std::move(deviceList))
{
}

void TcpWorker::stop()
{
    m_running = false;
    wait();
}

void TcpWorker::run()
{
    m_running = true;

    Processor::DataCollector collector(m_device, m_deviceList);
    int consecutiveErrors = 0;

    while(m_running) {
        const qint64 pollStart = QDateTime::currentMSecsSinceEpoch();
        bool allOk = true;
        QString lastError;

        for(const Model::RegisterField& field : m_device.registers){
            if(!m_running) break;

            QVector<quint16> regValues;
            QVector<bool> coilValues;
            QString error;

            const bool ok = collector.collectField(field, regValues, coilValues, error);
            
            m_table->updateUnifiedRegister(
                m_device.id, 
                field, 
                regValues, 
                coilValues, 
                ok, 
                error, 
                m_device.polling.intervalMs);

            if (!ok) {
                allOk = false;
                lastError = error;

                qWarning("TCP poll failed [device %d, field %s]: %s",
                         m_device.id, qPrintable(field.tagName), qPrintable(error));
            }
        }

        Model::DeviceInfo::Status status;
        status.lastPollTimestamp  = pollStart;
        status.lastPollDurationMs = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - pollStart);

        if (allOk) {
            consecutiveErrors = 0;
            status.state = Model::DeviceInfo::Status::State::Ok;
        } else {
            ++consecutiveErrors;
            status.state     = Model::DeviceInfo::Status::State::Error;
            status.lastError = lastError;
        }
        status.consecutiveErrors = consecutiveErrors;

        // Util::Logger::info(QStringLiteral("TCP Worker Running [%1] : %2")
        //                        .arg(m_device.name)
        //                        .arg(status.lastError));

        m_deviceList->updateStatus(m_device.id, status);

        //-------------------------------------------------//
        // Write, If Write Queue is not empty.
        //-------------------------------------------------//
        collector.flushWrites();

        msleep(m_device.polling.intervalMs);
    }
}

} // namespace Polling
} // namespace DataCollection
