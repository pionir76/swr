#pragma once

#include "../comm/DeviceClientFactory.h"
#include "../comm/RegisterExecutor.h"
#include "../model/DeviceModels.h"
#include "../store/DeviceList.h"

#include <QString>
#include <QVector>
#include <memory>

namespace DataCollection {
namespace Processor {

class DataCollector {
public:
    struct FieldResult {
        QVector<quint16> registerValues;
        QVector<bool>    coilValues;
        bool             ok    = false;
        QString          error;
    };

    DataCollector(const Model::DeviceInfo &device,
                  std::shared_ptr<Store::DeviceList> deviceList,
                  std::unique_ptr<Comm::IDeviceClient> client);

    bool collectField(const Model::RegisterConfig &config,
                      QVector<quint16> &registerValues,
                      QVector<bool> &coilValues,
                      QString &error);

    QList<FieldResult> collectAllFields();

    void flushWrites();

private:
    //-----------------------------------------------------------//
    // Records the position each register occupies in the batch response.
    // Each FieldSlice corresponds to a single field in m_device.registers.
    // Example: 
    // fieldIndex = 3   → m_device.registers[3] 
    // offset     = 2   → 2th word in the batch response
    // length     = 1   → 1 word length
    //-----------------------------------------------------------//
    struct FieldSlice {
        int fieldIndex;
        int offset;
        int length;
    };

    //-----------------------------------------------------------//
    // A batch of contiguous registers to read at once, along 
    // with the slices for each field
    //
    // Example: startAddress = 100, totalLength = 5, type = HoldingRegister
    // Read Address 100~104 at once, then slice the results into 
    // individual fields
    //-----------------------------------------------------------//
    struct RegisterBatch {
        int startAddress;
        int totalLength;
        Model::RegisterType type;
        QList<FieldSlice> slices;
    };

    QList<RegisterBatch> buildBatches() const;
    bool ensureConnected(QString &error);

    Model::DeviceInfo m_device;
    QList<RegisterBatch> m_batches;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    std::unique_ptr<Comm::RegisterExecutor>  m_executor;
};

} // namespace Processor
} // namespace DataCollection
