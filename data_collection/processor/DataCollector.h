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
                  std::shared_ptr<Store::DeviceList> deviceList = nullptr);

    bool initialize(QString &error);
    bool collectField(const Model::RegisterField &field,
                      QVector<quint16> &registerValues,
                      QVector<bool> &coilValues,
                      QString &error);

    QList<FieldResult> collectAllFields();

    void flushWrites();

private:
    struct FieldSlice {
        int fieldIndex;
        int offset;
        int length;
    };
    struct RegisterBatch {
        int startAddress;
        int totalLength;
        Model::RegisterType type;
        QList<FieldSlice> slices;
    };

    QList<RegisterBatch> buildBatches() const;
    bool ensureConnected(QString &error);

    Model::DeviceInfo m_device;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    std::unique_ptr<Comm::RegisterExecutor> m_executor;
};

} // namespace Processor
} // namespace DataCollection
