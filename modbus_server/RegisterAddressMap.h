#pragma once

// RegisterAddressMap
//
// Modbus 주소(quint16) ↔ UnifiedRegisterId(int) 사이의 매핑을 관리한다.
//
// [기본 동작]
//   modbusAddress == unifiedRegisterId (별도 설정 없이 자동 대응)
//
// [사용자 지정]
//   웹 UI를 통해 특정 레지스터의 Modbus 주소를 수동 지정 가능.
//   저장 전 반드시 isAddressInUse()로 중복 여부를 확인해야 한다.
//   지정된 매핑은 DB의 modbus_address_map 테이블에 영구 저장된다.
//
// [쓰레드 안전성]
//   읽기/쓰기 모두 내부 뮤텍스로 보호된다. ModbusTcpServer의 콜백 스레드와
//   ApiServer(웹 UI 설정) 스레드가 동시에 접근할 수 있다.

#include <QHash>
#include <QMutex>
#include <QList>

namespace DataCollection::Database { class DeviceDatabase; }

namespace ModbusServer {

// 단일 주소 매핑 항목
struct AddressEntry {
    int      unifiedId;     // UnifiedRegister ID
    quint16  modbusAddress; // 외부 Modbus 마스터에 노출될 주소
    bool     isCustom;      // false: 자동(기본), true: 사용자 지정
};

class RegisterAddressMap
{
public:
    // db: 매핑 영구 저장에 사용. nullptr이면 in-memory only.
    explicit RegisterAddressMap(DataCollection::Database::DeviceDatabase *db = nullptr);

    // DB에서 사용자 지정 매핑을 불러와 in-memory 맵을 초기화한다.
    bool load(QString &error);

    // RegisterTable의 현재 unifiedId 목록을 받아 기본 매핑(id==address)을 생성한다.
    // 이미 사용자 지정 항목이 있는 id는 덮어쓰지 않는다.
    void syncDefaults(const QList<int> &unifiedIds);

    // modbusAddress → unifiedId 조회. 매핑이 없으면 -1 반환.
    int unifiedIdFromAddress(quint16 modbusAddress) const;

    // unifiedId → modbusAddress 조회. 매핑이 없으면 0 반환.
    quint16 addressFromUnifiedId(int unifiedId) const;

    // 해당 modbusAddress가 이미 사용 중인지 확인한다.
    // 웹 UI에서 저장 전 반드시 호출해야 한다.
    bool isAddressInUse(quint16 modbusAddress) const;

    // 사용자 지정 주소 설정. 저장 전 isAddressInUse() 확인 필수.
    // DB에 영구 저장. 이미 해당 unifiedId의 기존 항목이 있으면 갱신한다.
    bool setCustomAddress(int unifiedId, quint16 modbusAddress, QString &error);

    // 사용자 지정 주소 제거 → 기본값(unifiedId == address)으로 복귀.
    // DB에서 해당 항목 삭제.
    bool removeCustomAddress(int unifiedId, QString &error);

    // 현재 전체 매핑 목록 반환 (웹 UI 조회용).
    QList<AddressEntry> allEntries() const;

private:
    DataCollection::Database::DeviceDatabase *m_db;

    mutable QMutex          m_mutex;
    QHash<quint16, int>     m_addrToId;   // modbusAddress → unifiedId
    QHash<int, quint16>     m_idToAddr;   // unifiedId → modbusAddress
    QHash<int, bool>        m_isCustom;   // unifiedId → 사용자 지정 여부
};

} // namespace ModbusServer
