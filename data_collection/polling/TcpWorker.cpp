#include "TcpWorker.h"
#include "../comm/DeviceClientFactory.h"
#include "../processor/DataCollector.h"
#include "../../utils/Logger.h"

#include <QDateTime>

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

    auto client = Comm::createDeviceClient(m_device.connection);
    Processor::DataCollector collector(m_device, m_deviceList, std::move(client));
    int consecutiveErrors = 0;
    Model::DeviceInfo::Status::State prevState = Model::DeviceInfo::Status::State::Unknown;

    if (m_device.registers.isEmpty())
        return;

    while (m_running) {
        const qint64 pollStart = QDateTime::currentMSecsSinceEpoch();
        bool    allOk     = true;
        QString lastError;

        const QList<Processor::DataCollector::FieldResult> results = collector.collectAllFields();

        for (int fi = 0; fi < m_device.registers.size(); ++fi) {
            if (!m_running) break;

            const Processor::DataCollector::FieldResult &r     = results.at(fi);
            const Model::RegisterField                  &field = m_device.registers.at(fi);

            m_table->updateUnifiedRegister(
                m_device.id,
                field,
                r.registerValues,
                r.coilValues,
                r.ok,
                r.error,
                m_device.polling.intervalMs);

            if (!r.ok) {
                allOk     = false;
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

        m_deviceList->updateStatus(m_device.id, status);

        // ── 상태 전환 감지 → 경보 기록 ────────────────────────────────────
        if (prevState != status.state) {
            if (status.state == Model::DeviceInfo::Status::State::Error) {
                Util::Logger::warning(
                    QStringLiteral("[경보] %1 통신 불능: %2")
                        .arg(m_device.name, lastError));
            } else if (status.state == Model::DeviceInfo::Status::State::Ok &&
                       prevState     == Model::DeviceInfo::Status::State::Error) {
                Util::Logger::info(
                    QStringLiteral("[복구] %1 통신 정상화")
                        .arg(m_device.name));
            }
            prevState = status.state;
        }

        collector.flushWrites();
        msleep(m_device.polling.intervalMs);
    }
}

} // namespace Polling
} // namespace DataCollection
