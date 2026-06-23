#include "DataCollector.h"

namespace DataCollection {
namespace Processor {

DataCollector::DataCollector(const Model::DeviceInfo &device,
                             std::shared_ptr<Store::DeviceList> deviceList)
    : m_device(device)
    , m_deviceList(std::move(deviceList))
{
}

bool DataCollector::initialize(QString &error)
{
    auto client = Comm::createDeviceClient(m_device.connection);

    if (!client) {
        error = QStringLiteral("Failed to create device client for device %1 (unsupported protocol)")
        .arg(m_device.id);
        return false;
    }

    m_executor = std::make_unique<Comm::RegisterExecutor>(std::move(client), m_device.connection.defaultByteOrder);
    return m_executor->connect(error);
}

bool DataCollector::ensureConnected(QString &error)
{
    if (!m_executor && !initialize(error))
        return false;
    if (!m_executor->isConnected() && !m_executor->connect(error))
        return false;
    return true;
}

bool DataCollector::collectField(const Model::RegisterField &field,
                                 QVector<quint16> &registerValues,
                                 QVector<bool> &coilValues,
                                 QString &error)
{
    if (!ensureConnected(error))
        return false;
    return m_executor->readField(field, registerValues, coilValues, error);
}

QList<DataCollector::RegisterBatch> DataCollector::buildBatches() const
{
    QList<RegisterBatch> batches;

    for (int i = 0; i < m_device.registers.size(); ++i) {
        const Model::RegisterField &field = m_device.registers.at(i);

        bool canMerge = false;
        if (!batches.isEmpty()) {
            const RegisterBatch &last = batches.last();
            canMerge = (last.type == field.type)
                    && (field.address == last.startAddress + last.totalLength)
                    && (last.totalLength + field.length <= Comm::RegisterExecutor::MAX_READ_QUANTITY);
        }

        if (canMerge) {
            RegisterBatch &last = batches.last();
            last.slices.append({i, last.totalLength, field.length});
            last.totalLength += field.length;
        } else {
            RegisterBatch batch;
            batch.startAddress = field.address;
            batch.totalLength  = field.length;
            batch.type         = field.type;
            batch.slices.append({i, 0, field.length});
            batches.append(batch);
        }
    }

    return batches;
}

QList<DataCollector::FieldResult> DataCollector::collectAllFields()
{
    const int count = m_device.registers.size();
    QList<FieldResult> results(count);

    QString connError;
    if (!ensureConnected(connError)) {
        for (auto &r : results) { r.ok = false; r.error = connError; }
        return results;
    }

    for (const RegisterBatch &batch : buildBatches()) {
        QVector<quint16> rawWords;
        QVector<bool>    rawBits;
        QString error;

        const bool ok = m_executor->readBatch(batch.startAddress, 
                                            batch.totalLength,
                                            batch.type, 
                                            rawWords, 
                                            rawBits, 
                                            error);

        //-----------------------------------------------------------------//
        // Batch: start=100, total=7
        // rawWords = [A0, A1,      B0, B1, B2, B3,  C0]
        //             ↑Slice0     ↑Slice1        ↑Slice2
        // Put results  of every slice into the results vector.
        //-----------------------------------------------------------------//
        for (const FieldSlice &slice : batch.slices) {
            FieldResult &r = results[slice.fieldIndex];
            if (!ok) {
                r.ok    = false;
                r.error = error;
                continue;
            }
            r.ok = true;

            // Coil Registers: extract from rawBits
            if (!rawBits.isEmpty()) {
                r.coilValues = rawBits.mid(slice.offset, slice.length);
            } 
            // Word Registers 
            else {
                const QVector<quint16> sliceVals = rawWords.mid(slice.offset, slice.length);
                r.registerValues = m_executor->applyFieldByteOrder(sliceVals, m_device.registers.at(slice.fieldIndex));
            }
        }
    }

    return results;
}


void DataCollector::flushWrites()
{
    if (!m_deviceList) return;

    for (const Model::WriteRequest &req : m_deviceList->takeWrites(m_device.id)) {
        QString error;
        if (!m_executor || !m_executor->isConnected()) {
            if (!initialize(error)) {
                qWarning("flushWrites: connect failed [device %d]: %s",
                         m_device.id, qPrintable(error));
                continue;
            }
        }
        if (!m_executor->writeField(req.field, req.rawValues, req.coilValues, error)) {
            if (req.retryCount > 0) {
                Model::WriteRequest retry = req;
                retry.retryCount--;
                m_deviceList->enqueueWrite(m_device.id, std::move(retry));

                qWarning("flushWrites: write failed, retrying (%d left) [device %d, addr %d]: %s",
                         req.retryCount - 1, m_device.id, req.field.address, qPrintable(error));
            } else {
                qWarning("flushWrites: write failed, no retries left [device %d, addr %d]: %s",
                         m_device.id, req.field.address, qPrintable(error));
            }
        }
    }
}


} // namespace Processor
} // namespace DataCollection
