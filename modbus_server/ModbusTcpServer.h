#pragma once

// ModbusTcpServer
//
// SmartRoute를 Modbus TCP 슬레이브(서버)로 동작시키는 메인 클래스.
// Qt6의 QModbusTcpServer(Qt6::SerialBus)를 내부에서 사용한다.
//
// [통합 레지스터 타입 정책]
//   내부 통합 레지스터 테이블(RegisterTable)의 모든 항목은 외부에
//   Holding Register(FC03)로 단일 타입으로 노출된다.
//   원본 장비 레지스터 타입(Coil / DiscreteInput / InputRegister 등)은
//   외부 인터페이스에서 무시된다.
//
//   비트형(Coil / DiscreteInput / BitRegister) 값 변환 규칙:
//     false → 0x0000,  true → 0x0001  (16비트 워드로 표현)
//
//   쓰기 가능 여부는 RegisterConfig.readOnly 플래그로 결정한다.
//   원본 장비의 InputRegister(FC04, 원래 RO)든 사용자가 readOnly 설정한
//   레지스터든 동일하게 쓰기 요청 시 Modbus Exception Code 0x01로 거부한다.
//
// [읽기 경로]
//   외부 마스터 FC03 요청
//     → RegisterAddressMap으로 unifiedId 조회
//     → RegisterTable::unifiedRegister(id)에서 현재 값 가져옴
//     → 비트형이면 rawCoils[0] → 0/1 변환, 워드형이면 rawRegisters 그대로
//     → Holding Register 응답 반환
//
// [쓰기 경로]
//   외부 마스터 FC06/FC16 요청 (Holding Register 쓰기만 허용)
//     → RegisterAddressMap으로 unifiedId → RegisterConfig 조회
//     → RegisterConfig.readOnly == true → Exception Code 0x01 응답
//     → PollingManager::requestWrite()로 실제 장비에 전파
//
// [쓰레드 모델]
//   QModbusTcpServer는 Qt 이벤트 루프에서 동작.
//   RegisterTable 접근은 RegisterTable 내부 뮤텍스로 보호됨.
//   RegisterAddressMap 접근은 RegisterAddressMap 내부 뮤텍스로 보호됨.

#include <QObject>
#include <memory>
#include <QModbusServer>

class QModbusTcpServer;

namespace DataCollection::Store   { class RegisterTable; class DeviceList; }
namespace DataCollection::Polling { class PollingManager; }
namespace DataCollection::Database { class DeviceDatabase; }

namespace ModbusServer {

class RegisterAddressMap;

class ModbusTcpServer : public QObject
{
    Q_OBJECT

public:
    explicit ModbusTcpServer(
        std::shared_ptr<DataCollection::Store::RegisterTable> registerTable,
        std::shared_ptr<DataCollection::Store::DeviceList>    deviceList,
        DataCollection::Polling::PollingManager               *pollingManager,
        DataCollection::Database::DeviceDatabase              *db,
        QObject *parent = nullptr);

    ~ModbusTcpServer() override;

    // 서버 시작. port: Modbus TCP 포트(기본 502), slaveId: 슬레이브 ID(기본 1).
    bool start(quint16 port, int slaveId, QString &error);

    // 서버 중지.
    void stop();

    bool isRunning() const;

    quint16 port() const;
    int     slaveId() const;

    // 현재 연결된 외부 마스터 수. (QModbusTcpServer 제공 정보)
    int connectionCount() const;

    // 주소 매핑 접근 (ApiServer에서 웹 UI 설정 처리 시 사용).
    RegisterAddressMap *addressMap() const;

signals:
    // 외부 마스터가 쓰기를 요청하여 장비로 전파를 시도했을 때 발생.
    void writeRequested(int unifiedId, double value);

    // 서버 에러 발생 시.
    void serverError(const QString &message);

private slots:
    // QModbusTcpServer의 dataWritten 시그널에 연결.
    // 쓰기 요청을 PollingManager로 전파한다.
    void onDataWritten(QModbusDataUnit::RegisterType table, int address, int size);

private:
    // 읽기 요청 시 RegisterTable의 현재 값으로 QModbusTcpServer의 데이터를 동기화한다.
    void syncReadData(int address, int size);

    std::shared_ptr<DataCollection::Store::RegisterTable> m_registerTable;
    std::shared_ptr<DataCollection::Store::DeviceList>    m_deviceList;
    DataCollection::Polling::PollingManager               *m_pollingManager;

    QModbusTcpServer              *m_server = nullptr;
    std::unique_ptr<RegisterAddressMap> m_addressMap;

    quint16 m_port    = 502;
    int     m_slaveId = 1;
};

} // namespace ModbusServer
