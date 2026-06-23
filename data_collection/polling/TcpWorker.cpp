#include "TcpWorker.h"
#include "../processor/DataCollector.h"
#include "../../utils/Logger.h"

#include <QDateTime>

namespace DataCollection {
namespace Polling {

TcpWorker::TcpWorker(Model::DeviceInfo device,
                     std::shared_ptr<Store::RegisterTable> table,
                     std::shared_ptr<Store::DeviceList> deviceList,
                     PollLogQueue *logQueue,
                     QObject *parent)
    : QThread(parent)
    , m_device(std::move(device))
    , m_table(std::move(table))
    , m_deviceList(std::move(deviceList))
    , m_logQueue(logQueue)
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

    if (m_device.registers.isEmpty()) {
        qWarning("TcpWorker: device %d has no registers, skipping.", m_device.id);
        return;
    }

    while(m_running) {
        const qint64 pollStart = QDateTime::currentMSecsSinceEpoch();
        bool allOk = true;
        QString lastError;

        //-----------------------------------------------------------------------//
        // Read Batch of fields for the device
        // collectAllFields() → buildBatches() → readBatch()  → readWords()(FC03)
        //-----------------------------------------------------------------------//
        const QList<Processor::DataCollector::FieldResult> results = collector.collectAllFields();

        for (int fi = 0; fi < m_device.registers.size(); ++fi) {
            if (!m_running) break;

            const Processor::DataCollector::FieldResult &r = results.at(fi);
            const Model::RegisterField &field = m_device.registers.at(fi);

            m_table->updateUnifiedRegister(
                m_device.id,
                field,
                r.registerValues,
                r.coilValues,
                r.ok,
                r.error,
                m_device.polling.intervalMs);

            if (!r.ok) {
                allOk = false;
                lastError = r.error;

                qWarning("TCP poll failed [device %d, field %s]: %s",
                         m_device.id, qPrintable(field.tagName), qPrintable(r.error));
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

        if (m_logQueue) {
            Model::PollLogEntry entry;
            entry.deviceId      = m_device.id;
            entry.deviceName    = m_device.name;
            entry.timestamp     = QDateTime::fromMSecsSinceEpoch(pollStart)
                                      .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            entry.success       = allOk;
            entry.durationMs    = status.lastPollDurationMs;
            entry.registerCount = m_device.registers.size();
            entry.message       = allOk
                ? QStringLiteral("Read %1 registers OK").arg(m_device.registers.size())
                : lastError;
            m_logQueue->push(entry);
        }

        //-------------------------------------------------//
        // Write, If Write Queue is not empty.
        //-------------------------------------------------//
        collector.flushWrites();

        msleep(m_device.polling.intervalMs);
    }
}

} // namespace Polling
} // namespace DataCollection
