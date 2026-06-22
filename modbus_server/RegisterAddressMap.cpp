#include "RegisterAddressMap.h"

// TODO: 실제 구현 필요
// - load(): DeviceDatabase에서 modbus_address_map 테이블 조회 → m_addrToId / m_idToAddr 초기화
// - syncDefaults(): unifiedIds를 순회하며 m_idToAddr에 없는 항목을 기본 매핑으로 추가
// - setCustomAddress(): 중복 없음 확인 후 DB upsert, in-memory 갱신
// - removeCustomAddress(): DB DELETE, in-memory에서 제거 후 기본 매핑으로 복귀
// - DeviceDatabase에 modbus_address_map 테이블 DDL 추가 필요 (README.md 참조)

namespace ModbusServer {

RegisterAddressMap::RegisterAddressMap(DataCollection::Database::DeviceDatabase *db)
    : m_db(db)
{
}

bool RegisterAddressMap::load(QString &error)
{
    Q_UNUSED(error)
    // TODO
    return true;
}

void RegisterAddressMap::syncDefaults(const QList<int> &unifiedIds)
{
    Q_UNUSED(unifiedIds)
    // TODO
}

int RegisterAddressMap::unifiedIdFromAddress(quint16 modbusAddress) const
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(modbusAddress)
    // TODO
    return -1;
}

quint16 RegisterAddressMap::addressFromUnifiedId(int unifiedId) const
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(unifiedId)
    // TODO
    return 0;
}

bool RegisterAddressMap::isAddressInUse(quint16 modbusAddress) const
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(modbusAddress)
    // TODO
    return false;
}

bool RegisterAddressMap::setCustomAddress(int unifiedId, quint16 modbusAddress, QString &error)
{
    Q_UNUSED(unifiedId)
    Q_UNUSED(modbusAddress)
    Q_UNUSED(error)
    // TODO
    return false;
}

bool RegisterAddressMap::removeCustomAddress(int unifiedId, QString &error)
{
    Q_UNUSED(unifiedId)
    Q_UNUSED(error)
    // TODO
    return false;
}

QList<AddressEntry> RegisterAddressMap::allEntries() const
{
    QMutexLocker locker(&m_mutex);
    // TODO
    return {};
}

} // namespace ModbusServer
