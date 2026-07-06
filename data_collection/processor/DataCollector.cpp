#include "DataCollector.h"

namespace DataCollection {
namespace Processor {

DataCollector::DataCollector(const Model::DeviceInfo &device,
                             std::shared_ptr<Store::DeviceList> deviceList,
                             std::unique_ptr<Comm::IDeviceClient> client)
    : m_device(device)
    , m_deviceList(std::move(deviceList))
    , m_executor(std::make_unique<Comm::RegisterExecutor>(
          std::move(client), m_device.connection.defaultByteOrder))
    , m_batches(buildBatches())
{}

bool DataCollector::ensureConnected(QString &error)
{
    if (!m_executor->isConnected()){
        return m_executor->connect(error);
    }

    return true;
}

bool DataCollector::collectField(const Model::RegisterConfig &config,
                                 QVector<quint16> &registerValues,
                                 QVector<bool> &coilValues,
                                 QString &error)
{
    if (!ensureConnected(error)){
        return false;
    }

    return m_executor->readField(config,
                                 registerValues,
                                 coilValues,
                                 error);
}

QList<DataCollector::RegisterBatch> DataCollector::buildBatches() const
{
    //-----------------------------------------------------------//
    // Think about the following example:
    // Registers: [0,1,2,3,4,5,10,100,200,7,6]
    //
    // Just Sort indices by (type, address) so contiguous registers are 
    // always merged
    // keep original order of the registers as inserted into the DB.
    //-----------------------------------------------------------//
    QList<int> indices;
    indices.reserve(m_device.registers.size());
    for (int i = 0; i < m_device.registers.size(); ++i){
        indices.append(i);
    }
        
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        const auto &ra = m_device.registers.at(a);
        const auto &rb = m_device.registers.at(b);
        
        if (ra.type != rb.type){
            return static_cast<int>(ra.type) < static_cast<int>(rb.type);
        }
        return ra.address < rb.address;
    });

    QList<RegisterBatch> batches;

    for (const int i : indices) {
        const Model::RegisterConfig &config = m_device.registers.at(i);

        bool canMerge = false;
        if (!batches.isEmpty()) {
            const RegisterBatch &last = batches.last();
            canMerge = (last.type == config.type)
                    && (config.address == last.startAddress + last.totalLength)
                    && (last.totalLength + config.length <= Comm::RegisterExecutor::MAX_READ_QUANTITY);
        }

        if (canMerge) {
            RegisterBatch &last = batches.last();
            last.slices.append({i, last.totalLength, config.length});
            last.totalLength += config.length;
        } else {
            RegisterBatch batch;
            batch.startAddress = config.address;
            batch.totalLength  = config.length;
            batch.type         = config.type;
            batch.slices.append({i, 0, config.length});
            batches.append(batch);
        }
    }

    return batches;
}


// ---------------------------------------------------------------------------//
// Collect all fields for the device at once, returning a list of FieldResult objects
// ---------------------------------------------------------------------------//
QList<DataCollector::FieldResult> DataCollector::collectAllFields()
{
    const int count = m_device.registers.size();
    QList<FieldResult> results(count);

    QString connError;
    if (!ensureConnected(connError)) {
        for (auto &r : results) { r.ok = false; r.error = connError; }
        return results;
    }

    for (const RegisterBatch &batch : m_batches) {
        QVector<quint16> rawWords;
        QVector<bool>    rawBits;
        QString error;

        const bool ok = m_executor->readBatch(batch.startAddress,
                                              batch.totalLength,
                                              batch.type,
                                              rawWords,
                                              rawBits,
                                              error);

        for (const FieldSlice &slice : batch.slices) {
            FieldResult &r = results[slice.fieldIndex];
            if (!ok) {
                r.ok    = false;
                r.error = error;
                continue;
            }
            r.ok = true;

            switch (batch.type) {
            case Model::RegisterType::Coil:
            case Model::RegisterType::DiscreteInput:
                r.coilValues = rawBits.mid(slice.offset, slice.length);
                break;
            default:
                r.registerValues = m_executor->applyFieldByteOrder(
                    rawWords.mid(slice.offset, slice.length),
                    m_device.registers.at(slice.fieldIndex));
                break;
            }
        }
    }

    return results;
}


// ---------------------------------------------------------------------------//
// Flush any pending write requests for this device
// ---------------------------------------------------------------------------//
void DataCollector::flushWrites()
{
    if (!m_deviceList) return;

    for (const Model::WriteRequest &req : m_deviceList->takeWrites(m_device.id)) {
        QString error;

        if (!m_executor->isConnected() && !m_executor->connect(error)) {
            qWarning("flushWrites: connect failed [device %d]: %s",
                     m_device.id, qPrintable(error));
            continue;
        }

        if (!m_executor->writeField(req.config, req.rawValues, req.coilValues, error)) {
            if (req.retryCount > 0) {
                Model::WriteRequest retry = req;
                retry.retryCount--;

                //-------------------------------------------//
                // Re-enqueue the write request for retry
                //-------------------------------------------//
                m_deviceList->enqueueWrite(m_device.id, std::move(retry));
                qWarning("flushWrites: write failed, retrying (%d left) [device %d, addr %d]: %s",
                         req.retryCount - 1, m_device.id, req.config.address, qPrintable(error));
            } else {
                qWarning("flushWrites: write failed, no retries left [device %d, addr %d]: %s",
                         m_device.id, req.config.address, qPrintable(error));
            }
        }
    }
}

} // namespace Processor
} // namespace DataCollection
