# Modbus TCP Server

## 목표

SmartRoute가 수집한 레지스터 데이터를 외부 시스템(HMI, SCADA 등)에 **Modbus TCP 슬레이브**로 노출하는 것이 목표다.

외부 마스터는 SmartRoute에 접속하여:
- 내부 레지스터 테이블의 최신 폴링값을 **읽고**
- 쓰기 가능한 레지스터에 값을 써서 **실제 장비에 전파**할 수 있다.

---

## 전체 구조

```
외부 Modbus TCP 마스터 (HMI / SCADA)
            │
            ▼ TCP 502
    ModbusTcpServer          ← QObject, 외부 인터페이스
            │
            ▼
    ModbusServerImpl         ← QModbusTcpServer 서브클래스 (ModbusTcpServer.cpp 내부)
     ├─ readData() 오버라이드  → RegisterTable 직접 조회 (항상 최신값)
     └─ setData() 오버라이드  → readOnly 체크 후 허용 or Modbus Exception 응답
            │
            ├── RegisterTable    (폴링 결과 저장소, unifiedRegisterId 기준 인덱싱)
            └── DeviceList       (RegisterConfig 조회, 쓰기 큐)
```

### 왜 ModbusServerImpl이 필요한가

`QModbusTcpServer`는 자체 내부 레지스터 이미지를 유지하며, 마스터 읽기 요청에 그 이미지로 자동 응답한다.
별도 처리 없이 사용하면 내부 이미지와 `RegisterTable`의 최신 폴링값이 따로 놀게 된다.

Qt는 이 동작을 두 가상 함수로 커스터마이즈할 수 있도록 설계했다:

| 가상 함수 | 기본 동작 | 오버라이드 후 |
|-----------|-----------|---------------|
| `readData()` | 내부 이미지에서 값 반환 | `RegisterTable::state(addr)` 직접 조회 → 항상 최신 폴링값 |
| `setData()` | 내부 이미지에 값 저장 | readOnly 체크 → 차단 시 Modbus Exception, 허용 시 기본 동작 |

`ModbusServerImpl`은 이 두 함수를 오버라이드하기 위한 최소 서브클래스다.
외부에 노출할 필요가 없으므로 헤더 없이 `ModbusTcpServer.cpp` 안에만 정의한다.

---

## 주소 매핑 정책

**`modbusAddress == unifiedRegisterId`** — 별도 매핑 테이블 없이 직접 대응한다.

`unifiedRegisterId`는 시스템 전체에서 유일한 레지스터 식별자로:
- **자동 배정**: 1 ~ 4999 (등록 순서대로 DB가 배정)
- **수동 지정**: 5000 이상 (API에서 사용자가 직접 지정 가능)

마스터가 Modbus 주소 X를 요청하면 곧바로 `RegisterTable::state(X)`를 조회한다.

---

## 동작 흐름

### 읽기 (FC03 — Holding Register Read)

```
마스터 읽기 요청 (주소 X)
    → Qt가 readData() 호출
    → RegisterTable::state(X) 조회
    → 비트형(Coil/DiscreteInput) : rawCoils[0] → false=0x0000 / true=0x0001
    → 워드형(HoldingRegister 등) : rawRegisters[0] 그대로
    → 마스터에게 값 반환
```

별도 타이머나 주기적 동기화 없이, 마스터가 요청할 때마다 `RegisterTable`에서 직접 꺼내므로 항상 최신 폴링값이 응답된다.

### 쓰기 (FC06 / FC16 — Holding Register Write)

```
마스터 쓰기 요청 (주소 X, 값 V)
    → Qt가 setData() 호출
    → DeviceList::findByUnifiedId(X) 로 RegisterConfig 조회
         ├─ 레지스터 없음 or readOnly == true
         │       → return false → Qt가 Modbus Exception 자동 응답
         └─ writable
                 → QModbusTcpServer::setData() (내부 이미지 갱신)
                 → dataWritten 시그널 발생
                 → onDataWritten() 슬롯
                 → DeviceList::enqueueWrite(deviceId, WriteRequest)
                 → SerialWorker / TcpWorker가 실제 장비에 전파
```

---

## 레지스터 타입 변환 정책

내부 통합 레지스터의 모든 타입은 외부에 **Holding Register (FC03) 단일 타입**으로 노출된다.

| 내부 RegisterType | 값 변환 규칙 |
|-------------------|-------------|
| `Coil`, `DiscreteInput` | `rawCoils[0]` → `false=0x0000`, `true=0x0001` |
| `HoldingRegister`, `InputRegister` | `rawRegisters[0]` 그대로 |

쓰기 허용 여부는 원본 장비 타입이 아닌 `RegisterConfig.readOnly` 플래그로만 결정한다.

---

## 의존성

| 의존 대상 | 용도 |
|-----------|------|
| `Qt6::SerialBus` | `QModbusTcpServer` 제공 |
| `RegisterTable` | 읽기 요청 시 최신 폴링값 조회 |
| `DeviceList` | 쓰기 요청 시 RegisterConfig 조회 및 WriteRequest 큐잉 |

---

## 주의 사항

- Modbus 표준 포트 502는 Linux에서 root 권한 필요 → 배포 환경에서 `cap_net_bind_service` 부여 또는 iptables로 고포트 → 502 전환 고려
- `RegisterTable`은 폴링 스레드와 공유되므로 `state()` 같은 뮤텍스 보호 API를 통해서만 접근해야 한다
- `start()` 호출 시점의 `DeviceList` 기준으로 Holding Register 블록 범위를 결정한다. 이후 레지스터가 추가되면 서버를 재시작해야 새 주소가 적용된다
