# RegisterTable 클래스 설명

## 개요

폴링 워커에서 수집한 레지스터/코일 값을 **스레드 안전하게 저장하고 조회**하는 인메모리 저장소입니다.
Modbus RTU/TCP/ASCII, PCLink 등 프로토콜에 무관하게 동일한 저장 구조를 사용합니다.

---

## 내부 저장소 구조

```
m_states: QHash<unifiedRegisterId, RegisterState>
```

| 저장소 | 역할 |
|--------|------|
| `m_states` | 스케일 변환·범위검사까지 완료된 레지스터 상태 (rawRegisters/rawCoils 포함) |

> `unifiedRegisterId`는 DB INSERT 시 `DeviceDatabase::insertRegister`에서 확정된다. 런타임 ID 발급 로직 없음.
> 원시값(raw)은 별도 테이블 없이 `RegisterState.rawRegisters` / `RegisterState.rawCoils` 에 직접 저장된다.

---

## 핵심 흐름: `updateState`

```
폴링 결과 도착
    │
    ▼
config.unifiedRegisterId  ← DB에서 로드 시 이미 확정된 값 (< 0이면 즉시 리턴)
    │
    ▼
entry.config = config  ← RegisterConfig 전체 갱신 (초기화 및 폴링 재시작 후 최신화)
    │
    ├─ success == true
    │       ▼
    │   lastUpdated = 현재 시각
    │   rawRegisters / rawCoils 저장
    │       ▼
    │   computeScaledValue()  ← 원시값 × scale → 물리값
    │       ▼
    │   outOfRange 판정 (scaledValue < min || > max)
    │       ▼
    │   clampValue()          ← 범위 초과 시 min/max 로 잘라냄
    │
    └─ success == false
            ▼
        isValid=false, errorMessage 저장
        lastUpdated/rawRegisters/rawCoils/scaledValue/outOfRange 는 직전 값 유지
        → lastUpdated 미갱신으로 elapsed 축적 → quality가 Normal → Bad로 전이
    │
    ▼
m_states[unifiedId] = entry  (저장 완료)
```

---

## `updateState` 주요 필드 매핑

| `RegisterState` 필드 | 소스 |
|---|---|
| `config` | `RegisterConfig` 파라미터 전체 |
| `config.deviceId` | `RegisterConfig.deviceId` |
| `config.address` | `RegisterConfig.address` |
| `pollingIntervalMs` | `int pollingIntervalMs` 파라미터 |

---

## `unifiedRegisterId` — ID 관리

`unifiedRegisterId`는 DB INSERT 시 `DeviceDatabase::insertRegister`에서 확정됩니다.

### 범위 정책 (`DeviceModels.h`)

| 범위 | 용도 | 상수 |
|---|---|---|
| 1 ~ 4999 | 자동 할당 전용 | `kAutoUnifiedIdMax = 4999` |
| 5000 ~ 65535 | 수동 지정 전용 | `kManualUnifiedIdMin = 5000` |

- **자동 할당** (`unifiedRegisterId` 미지정 시): `WHERE unified_register_id < 5000` 범위 내 `MAX + 1` 부여. 범위 소진 시 에러 반환.
- **수동 지정** (`unifiedRegisterId` 직접 전달 시): `0 ≤ id < 5000` 이면 API에서 400 BadRequest 반환.
- DB에 `UNIQUE INDEX (unified_register_id WHERE >= 0)` 로 중복 방지.

> 이 정책의 목적: 사용자가 수동으로 높은 번지(예: 5555)를 지정해도 자동 할당 번지가 영향받지 않음.

### Modbus Server 주소 표기

`unifiedRegisterId`가 Modbus PDU 주소로 직접 사용됩니다.

```
전통 5자리 표기 = 40000 + unifiedRegisterId
예) unifiedRegisterId=1 → 40001,  unifiedRegisterId=5000 → 45000
```

`RegisterTable`은 ID를 발급하지 않으며, `config.unifiedRegisterId` 값을 그대로 사용합니다.

---

## `computeScaledValue` — 원시값 → 물리값 변환

`config.type` 기준 `switch`로 코일/레지스터 경로를 명시적으로 분기합니다.

### 레지스터 계열 (`HoldingRegister`, `InputRegister`, `WordRegister`)

레지스터가 2개 이상이면 16비트씩 왼쪽으로 시프트하며 합쳐 32/64비트 정수로 만든 뒤 `scale`을 곱합니다.
1개면 해당 값 × `scale`. `isSigned` 시 부호 확장 처리합니다.

```
예) rawRegisters = [0x0001, 0x86A0]
combined = (0x0001 << 16) | 0x86A0 = 100000
scaledValue = 100000 × 0.01 = 1000.0
```

### 코일 계열 (`Coil`, `DiscreteInput`, `BitRegister`)

- 1개: `true` → `1.0 × scale`, `false` → `0.0`
- 여러 개: `true` 개수 × `scale`

---

## 스레드 안전성

모든 public 메서드에서 `QMutexLocker locker(&m_mutex)` 로 진입 즉시 락을 잡습니다.
`m_mutex`가 `mutable`로 선언되어 있어 `const` 조회 메서드도 락을 사용할 수 있습니다.

---

## Modbus Server 연동

`RegisterTable`은 외부 Modbus 마스터에게 데이터를 제공하는 **단일 소스**입니다.

### 노출 정책

| 항목 | 정책 |
|---|---|
| 레지스터 타입 | 원본 타입 무관하게 **Holding Register(FC03) 단일 타입**으로 고정 노출 |
| 비트형 변환 | `rawCoils[0]` → `false=0x0000`, `true=0x0001` (16비트 워드) |
| 쓰기 가능 여부 | `RegisterConfig.readOnly` 플래그로 결정 (readOnly=true → Exception Code 0x01) |
| 주소 체계 | `unifiedRegisterId` == Modbus 주소 (기본값), 웹 UI에서 수동 변경 가능 |

### 데이터 흐름

```
[외부 Modbus 마스터]
    │ FC03 Read / FC06·FC16 Write
    ▼
ModbusTcpServer
    ├─ 읽기: RegisterAddressMap → unifiedId → RegisterTable::state(id) → 응답
    └─ 쓰기: RegisterAddressMap → RegisterConfig → PollingManager::enqueueWrite → 실제 장비 전파
```

### 주소 매핑 (`RegisterAddressMap`)

- 기본: `modbusAddress == unifiedRegisterId` (별도 설정 불필요)
- 수동 지정: DB `modbus_address_map` 테이블에 영구 저장
- 중복 방지: `isAddressInUse()` 확인 후 `setCustomAddress()` 호출

---

## 요약

> **"폴링 원시값을 받아서 → unifiedRegisterId 키로 저장하고 → 스케일·범위를 적용해 → 스레드 안전하게 캐싱하는 인메모리 상태 테이블 — 외부 Modbus 마스터에게 Holding Register로 단일 노출"**
