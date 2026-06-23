#include "SerialWorker.h"
#include "../processor/DataCollector.h"
#include "../../utils/Logger.h"

#include <QDateTime>
#include <QDebug>
#include <vector>

namespace DataCollection {
namespace Polling {

SerialWorker::SerialWorker(QList<Model::DeviceInfo> devices,
                           std::shared_ptr<Store::RegisterTable> table,
                           std::shared_ptr<Store::DeviceList> deviceList,
                           PollLogQueue *logQueue,
                           QObject *parent)
    : QThread(parent)
    , m_devices(std::move(devices))
    , m_table(std::move(table))
    , m_deviceList(std::move(deviceList))
    , m_logQueue(logQueue)
{
}

void SerialWorker::stop()
{
    m_running = false;
    wait();
}

void SerialWorker::run()
{
    m_running = true;

    std::vector<std::unique_ptr<Processor::DataCollector>> collectors;
    QVector<qint64> lastPollMs;
    QVector<int> consecutiveErrors;

    collectors.reserve(m_devices.size());
    lastPollMs.reserve(m_devices.size());
    consecutiveErrors.reserve(m_devices.size());

    for (const Model::DeviceInfo &device : m_devices) {
        collectors.push_back(std::make_unique<Processor::DataCollector>(device, m_deviceList));
        lastPollMs.append(0);
        consecutiveErrors.append(0);
    }

    while (m_running){
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool anyPolled = false;

        for( int i=0; i<m_devices.size(); ++i){
            if (!m_running) break;

            //-------------------------------------------------//
            // Write, If Write Queue is not empty.
            //-------------------------------------------------//
            collectors[i]->flushWrites();

            if (now - lastPollMs[i] < m_devices[i].polling.intervalMs) continue;
            if (m_devices[i].registers.isEmpty()) continue;

            anyPolled = true;

            const qint64 pollStart = QDateTime::currentMSecsSinceEpoch();
            lastPollMs[i] = pollStart;

            bool allOk = true;
            QString lastError;

            if (!m_running) continue;

            //-----------------------------------------------------------------------//
            // Read Batch of fields for the device
            // collectAllFields() → buildBatches() → readBatch()  → readWords()(FC03)
            //-----------------------------------------------------------------------//
            const QList<Processor::DataCollector::FieldResult> results = collectors[i]->collectAllFields();

            for (int fi = 0; fi < m_devices[i].registers.size(); ++fi) {
                if (!m_running) break;

                const Processor::DataCollector::FieldResult &r = results.at(fi);
                const Model::RegisterField &field = m_devices[i].registers.at(fi);

                m_table->updateUnifiedRegister(
                    m_devices[i].id,
                    field,
                    r.registerValues,
                    r.coilValues,
                    r.ok,
                    r.error,
                    m_devices[i].polling.intervalMs);

                if (!r.ok) {
                    allOk = false;
                    lastError = r.error;
                    qWarning("Serial poll failed [device %d, field %s]: %s",
                             m_devices[i].id, qPrintable(field.tagName), qPrintable(r.error));
                }
            }

            Model::DeviceInfo::Status status;
            status.lastPollTimestamp  = pollStart;
            status.lastPollDurationMs = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - pollStart);

            if (allOk) {
                consecutiveErrors[i] = 0;
                status.state = Model::DeviceInfo::Status::State::Ok;
            } else {
                ++consecutiveErrors[i];
                status.state     = Model::DeviceInfo::Status::State::Error;
                status.lastError = lastError;
            }
            status.consecutiveErrors = consecutiveErrors[i];

            m_deviceList->updateStatus(m_devices[i].id, status);

            if (m_logQueue) {
                Model::PollLogEntry entry;
                entry.deviceId      = m_devices[i].id;
                entry.deviceName    = m_devices[i].name;
                entry.timestamp     = QDateTime::fromMSecsSinceEpoch(pollStart)
                                          .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                entry.success       = allOk;
                entry.durationMs    = status.lastPollDurationMs;
                entry.registerCount = m_devices[i].registers.size();
                entry.message       = allOk
                    ? QStringLiteral("Read %1 registers OK").arg(m_devices[i].registers.size())
                    : lastError;
                m_logQueue->push(entry);
            }

            // Util::Logger::info(QStringLiteral("Serial Worker Running [%1] : %2")
            //                        .arg(m_devices[i].name)
            //                        .arg(status.lastError));
        }

        if (!anyPolled) {
            msleep(10);
        }
    }
}

} // namespace Polling
} // namespace DataCollection
