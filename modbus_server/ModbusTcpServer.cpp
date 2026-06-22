#include "ModbusTcpServer.h"
#include "RegisterAddressMap.h"

// TODO: 실제 구현 필요
//
// [start()]
//   - QModbusTcpServer 인스턴스 생성
//   - setServerAddress(slaveId) 설정
//   - connectDevice() → 포트 바인딩
//   - dataWritten 시그널 → onDataWritten 슬롯 연결
//   - RegisterAddressMap::load() 후 syncDefaults() 호출
//
// [stop()]
//   - disconnectDevice() 후 m_server 해제
//
// [onDataWritten()]
//   - address → RegisterAddressMap::unifiedIdFromAddress() → unifiedId
//   - DeviceList에서 해당 unifiedId의 RegisterField 조회
//   - readOnly 체크 → 위반 시 Modbus Exception 응답 (QModbusTcpServer API 확인 필요)
//   - PollingManager::requestWrite() 호출로 실제 장비에 전파
//
// [syncReadData()]
//   - RegisterTable::unifiedRegister(id)로 현재 scaledValue 읽기
//   - QModbusTcpServer::setData()로 서버 내부 레지스터 갱신
//   - scaledValue → quint16 변환 시 scale 역산 필요 (rawRegisters 사용 권장)
//
// [통합 레지스터 타입 정책]
//   모든 통합 레지스터는 Holding Register(FC03)로 단일 노출.
//   비트형(rawCoils): false → 0x0000, true → 0x0001 로 변환 후 setData()
//   워드형(rawRegisters): 값 그대로 setData()
//   readOnly == true인 레지스터의 쓰기 요청 → Exception Code 0x01 응답
//
// [읽기 시점]
//   QModbusTcpServer는 내부에 레지스터 이미지를 유지하며 마스터 요청에 자동 응답한다.
//   RegisterTable의 최신 값을 반영하기 위해 다음 두 가지 방식 중 선택:
//     A) 폴링 완료 시마다 RegisterTable → QModbusTcpServer 동기화 (push)
//     B) QModbusTcpServer의 modbusServerRead 신호에서 직전 값 동기화 (pull on demand)
//   → A 방식 권장 (구현 단순, 항상 최신 값 유지)
//
// [포트 502 주의]
//   Linux에서 1024 미만 포트는 root 권한 필요.
//   배포 환경에서 cap_net_bind_service 부여 또는 iptables REDIRECT로 고포트 → 502 전환 고려.

namespace ModbusServer {

ModbusTcpServer::ModbusTcpServer(
    std::shared_ptr<DataCollection::Store::RegisterTable> registerTable,
    std::shared_ptr<DataCollection::Store::DeviceList>    deviceList,
    DataCollection::Polling::PollingManager               *pollingManager,
    DataCollection::Database::DeviceDatabase              *db,
    QObject *parent)
    : QObject(parent)
    , m_registerTable(std::move(registerTable))
    , m_deviceList(std::move(deviceList))
    , m_pollingManager(pollingManager)
    , m_addressMap(std::make_unique<RegisterAddressMap>(db))
{
}

ModbusTcpServer::~ModbusTcpServer()
{
    stop();
}

bool ModbusTcpServer::start(quint16 port, int slaveId, QString &error)
{
    Q_UNUSED(port)
    Q_UNUSED(slaveId)
    Q_UNUSED(error)
    // TODO
    return false;
}

void ModbusTcpServer::stop()
{
    // TODO
}

bool ModbusTcpServer::isRunning() const
{
    // TODO
    return false;
}

quint16 ModbusTcpServer::port() const    { return m_port; }
int     ModbusTcpServer::slaveId() const { return m_slaveId; }

int ModbusTcpServer::connectionCount() const
{
    // TODO
    return 0;
}

RegisterAddressMap *ModbusTcpServer::addressMap() const
{
    return m_addressMap.get();
}

void ModbusTcpServer::onDataWritten(QModbusDataUnit::RegisterType table, int address, int size)
{
    Q_UNUSED(table)
    Q_UNUSED(address)
    Q_UNUSED(size)
    // TODO
}

void ModbusTcpServer::syncReadData(int address, int size)
{
    Q_UNUSED(address)
    Q_UNUSED(size)
    // TODO
}

} // namespace ModbusServer
