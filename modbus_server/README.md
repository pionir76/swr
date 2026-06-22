# Modbus TCP Server

SmartRoute를 Modbus TCP 슬레이브(서버)로 동작시키는 모듈.
외부 Modbus TCP 마스터(HMI, SCADA 등)가 SmartRoute에 접속하여 통합 레지스터 테이블의 값을 읽거나 실제 장비에 쓰기를 전파할 수 있다.

---

## 구성 클래스

| 클래스 | 파일 | 역할 |
|--------|------|------|
| `ModbusTcpServer` | `ModbusTcpServer.h/.cpp` | Modbus TCP 서버 메인. 읽기/쓰기 요청 처리 |
| `RegisterAddressMap` | `RegisterAddressMap.h/.cpp` | Modbus 주소 ↔ UnifiedRegisterId 매핑 관리 |

---

## 통합 레지스터 타입 정책

내부 통합 레지스터 테이블의 모든 항목은 외부에 **Holding Register (FC03) 단일 타입**으로 노출된다.
원본 장비의 레지스터 타입(Coil, DiscreteInput, InputRegister 등)은 외부 인터페이스에서 무시된다.

| 내부 RegisterType | 외부 노출 타입 | 값 변환 규칙 |
|-------------------|----------------|--------------|
| Coil / DiscreteInput / BitRegister | Holding Register (FC03) | `false → 0x0000`, `true → 0x0001` |
| HoldingRegister / InputRegister / WordRegister | Holding Register (FC03) | rawRegisters 값 그대로 |

쓰기 가능 여부는 `RegisterField.readOnly` 플래그로만 결정한다.
원본 장비 타입이 InputRegister(원래 RO)여도, 사용자가 readOnly로 설정한 레지스터여도
쓰기 요청 시 동일하게 **Modbus Exception Code 0x01** 로 거부한다.

---

## 동작 흐름

```
외부 Modbus TCP 마스터
        │
        ▼
ModbusTcpServer (QModbusTcpServer 기반)
        │
        ├── 읽기 요청 (FC03 — Holding Register 전용)
        │       └── RegisterAddressMap → unifiedRegisterId
        │               └── RegisterTable::unifiedRegister(id)
        │                       ├── 비트형: rawCoils[0] → 0x0000 / 0x0001
        │                       └── 워드형: rawRegisters 그대로
        │                               → Holding Register 응답
        │
        └── 쓰기 요청 (FC06/FC16 — Holding Register 전용)
                └── RegisterAddressMap → unifiedRegisterId → RegisterField
                        ├── readOnly == true → Exception Code 0x01 응답
                        └── PollingManager::requestWrite() → 실제 장비 전파
```

---

## 주소 매핑 정책

### 기본 (자동)
- `modbusAddress == unifiedRegisterId`
- RegisterTable에 등록된 레지스터는 별도 설정 없이 즉시 접근 가능

### 사용자 지정 (웹 UI)
- 사용자가 원하는 Modbus 주소를 수동으로 지정 가능
- 저장 전 반드시 **중복 주소 사용 여부 확인** (`RegisterAddressMap::isAddressInUse()`)
- 설정은 DB의 `modbus_address_map` 테이블에 영구 저장

### DB 스키마 (modbus_address_map 테이블)
```sql
CREATE TABLE IF NOT EXISTS modbus_address_map (
    unified_id      INTEGER PRIMARY KEY,  -- UnifiedRegister ID
    modbus_address  INTEGER NOT NULL UNIQUE, -- Modbus 레지스터 주소
    is_custom       INTEGER NOT NULL DEFAULT 0  -- 0: 자동(기본), 1: 사용자 지정
);
```

---

## 쓰기 전파 정책

- FC06/FC16 (Holding Register 쓰기): `PollingManager`를 통해 실제 장비로 전파
- FC05/FC15 (Coil 쓰기): 동일하게 실제 장비로 전파
- 쓰기 대상 장비가 readOnly 레지스터이면 Exception Code 01로 응답
- 폴링 중 쓰기 충돌: 쓰기 완료 후 다음 폴링 사이클에서 값 갱신

---

## REST API (웹 UI 연동)

주소 매핑 설정을 위해 ApiServer에 아래 엔드포인트가 추가되어야 함:

| Method | Path | 설명 |
|--------|------|------|
| GET | `/api/modbus-server/status` | 서버 상태 (포트, 슬레이브 ID, 연결 수) |
| GET | `/api/modbus-server/config` | 서버 설정 (포트, 슬레이브 ID 조회) |
| PUT | `/api/modbus-server/config` | 서버 설정 변경 (포트, 슬레이브 ID) |
| GET | `/api/modbus-server/address-map` | 전체 주소 매핑 목록 조회 |
| PUT | `/api/modbus-server/address-map/:unifiedId` | 특정 레지스터의 Modbus 주소 지정 (중복 체크 포함) |
| DELETE | `/api/modbus-server/address-map/:unifiedId` | 지정 주소 제거 (기본값으로 복귀) |

---

## 설정 값 (CMake / config.json)

| 항목 | 기본값 | 설명 |
|------|--------|------|
| `SR_MODBUS_SERVER_PORT` | `502` | Modbus TCP 리스닝 포트 |
| `SR_MODBUS_SERVER_SLAVE_ID` | `1` | Modbus 슬레이브 ID |
| `SR_MODBUS_SERVER_ENABLED` | `0` | 서버 활성화 여부 (0=비활성) |

---

## 의존성

- `Qt6::SerialBus` — `QModbusTcpServer` 제공 (CMakeLists.txt에 추가 필요)
- `DataCollection::Store::RegisterTable` — 실시간 레지스터 값 읽기
- `DataCollection::Store::DeviceList` — RegisterField 조회 (쓰기 전파 시 장비/레지스터 특정)
- `DataCollection::Polling::PollingManager` — 실제 장비 쓰기 전파
- `DataCollection::Database::DeviceDatabase` — 주소 매핑 영구 저장

---

## 주의 사항

- `QModbusTcpServer`는 `Qt6::SerialBus` 모듈 필요 (크로스컴파일 시 SDK에 포함 여부 확인)
- Modbus 표준 포트 502는 root 권한 필요 → 배포 환경에서 포트 포워딩 또는 cap_net_bind_service 설정 고려
- RegisterTable은 폴링 스레드와 공유되므로 읽기 시 반드시 `RegisterTable`의 뮤텍스 보호 API 사용 (직접 내부 접근 금지)
