#include "SerialWorker.h"
#include "../comm/DeviceClientFactory.h"
#include "../processor/DataCollector.h"
#include "../../utils/Logger.h"
#include "../../config/SystemConfig.h"

#include <QDateTime>
#include <QDebug>
#include <vector>

namespace DataCollection {
namespace Polling {

SerialWorker::SerialWorker(QList<Model::DeviceInfo> devices,
                           std::shared_ptr<Store::RegisterTable> table,
                           std::shared_ptr<Store::DeviceList> deviceList,
                           QObject *parent)
    : QThread(parent)
    , m_devices(std::move(devices))
    , m_table(std::move(table))
    , m_deviceList(std::move(deviceList))
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

    QString openError;
    Comm::SerialBus bus(SystemConfig::rs485());
    if (!bus.open(openError)) {
        Util::Logger::error(QStringLiteral("SerialWorker: %1").arg(openError));
        return;
    }

    std::vector<std::unique_ptr<Processor::DataCollector>> collectors;
    QVector<qint64> lastPollMs;
    QVector<int>    consecutiveErrors;
    QVector<Model::DeviceInfo::Status::State> prevStates;

    collectors.reserve(m_devices.size());
    lastPollMs.reserve(m_devices.size());
    consecutiveErrors.reserve(m_devices.size());
    prevStates.reserve(m_devices.size());

    for (const Model::DeviceInfo &device : m_devices) {
        auto client = Comm::createDeviceClient(device.connection, &bus);
        collectors.push_back(
            std::make_unique<Processor::DataCollector>(device, m_deviceList, std::move(client)));
        lastPollMs.append(0);
        consecutiveErrors.append(0);
        prevStates.append(Model::DeviceInfo::Status::State::Unknown);
    }

    while (m_running) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool anyPolled = false;

        for (int i = 0; i < m_devices.size(); ++i) {
            if (!m_running) break;

            collectors[i]->flushWrites();

            if (now - lastPollMs[i] < m_devices[i].polling.intervalMs) continue;
            if (m_devices[i].registers.isEmpty()) continue;

            anyPolled = true;

            const qint64 pollStart = QDateTime::currentMSecsSinceEpoch();
            lastPollMs[i] = pollStart;

            bool    allOk     = true;
            QString lastError;

            if (!m_running) continue;

            const QList<Processor::DataCollector::FieldResult> results = collectors[i]->collectAllFields();

            for (int fi = 0; fi < m_devices[i].registers.size(); ++fi) {
                if (!m_running) break;

                const Processor::DataCollector::FieldResult &r     = results.at(fi);
                const Model::RegisterField                  &field = m_devices[i].registers.at(fi);

                m_table->updateUnifiedRegister(
                    m_devices[i].id,
                    field,
                    r.registerValues,
                    r.coilValues,
                    r.ok,
                    r.error,
                    m_devices[i].polling.intervalMs);

                if (!r.ok) {
                    allOk     = false;
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

            if (prevStates[i] != status.state) {
                if (status.state == Model::DeviceInfo::Status::State::Error) {
                    Util::Logger::warning(
                        QStringLiteral("[경보] %1 통신 불능: %2")
                            .arg(m_devices[i].name, lastError));
                } else if (status.state == Model::DeviceInfo::Status::State::Ok &&
                           prevStates[i]  == Model::DeviceInfo::Status::State::Error) {
                    Util::Logger::info(
                        QStringLiteral("[복구] %1 통신 정상화")
                            .arg(m_devices[i].name));
                }
                prevStates[i] = status.state;
            }
        }

        if (!anyPolled)
            msleep(10);
    }

    bus.close();
}

} // namespace Polling
} // namespace DataCollection
