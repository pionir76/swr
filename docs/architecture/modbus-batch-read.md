# Modbus 연속 레지스터 배치 읽기 설계

## 목표

폴링 사이클마다 `RegisterField` 1개당 Modbus 요청 1건을 발생시키는 현재 구조를 개선하여,  
**주소가 연속된 동일 타입 필드를 하나의 요청으로 묶어** 통신 횟수를 최소화한다.

RS-485 반이중 통신(turn-around time 비용)과 Modbus TCP 모두 동일한 코드 경로로 혜택을 받는다.

---

## 제약 조건

| 항목 | 값 / 정책 |
|------|-----------|
| 배치 최대 크기 | `MAX_READ_QUANTITY = 64` — 배치 분할 기준 전용 (개별 필드 길이 제한 아님) |
| 갭 허용 | **없음** — 주소가 엄격히 연속(strictly adjacent)인 필드만 묶음 |
| 타입 혼합 | **불가** — FC가 다르므로 동일 RegisterType끼리만 묶음 |
| 바이트 오더 | 배치 응답 분배 후 **필드별**로 개별 적용 |
| 에러 처리 | 배치 1건 실패 → 해당 배치 내 모든 필드를 error 처리 |
| 쓰기 | 변경 없음 (기존 `writeField` 유지) |

### 연속 판정 기준

```
field[n].address + field[n].length == field[n+1].address
  AND field[n].type == field[n+1].type
  AND currentBatch.totalLength + field[n+1].length <= MAX_READ_QUANTITY
```

---

## 핵심 자료 구조

### `RegisterBatch` (DataCollector 내부 전용)

```cpp
// DataCollector.cpp 내부에서만 사용 (헤더 불필요)
struct FieldSlice {
    int fieldIndex;   // m_device.registers 내 원본 인덱스
    int offset;       // 배치 응답 배열 내 시작 위치
    int length;       // field.length
};

struct RegisterBatch {
    int startAddress;
    int totalLength;
    Model::RegisterType type;    // 배치 내 모든 필드의 공통 타입
    QList<FieldSlice> slices;
};
```

### `FieldResult` (DataCollector 공개 타입)

```cpp
// DataCollector.h
struct FieldResult {
    QVector<quint16> registerValues;
    QVector<bool>    coilValues;
    bool             ok    = false;
    QString          error;
};
```

---

## 배치 그룹핑 알고리즘

`DataCollector::buildBatches()` — 생성자에서 1회 호출되어 `m_batches`에 캐시됨.

```
1. m_device.registers 인덱스 배열 생성 후 (type, address) 기준으로 정렬
   → 비연속 주소라도 정렬 후 연속이면 같은 배치로 묶임
2. 정렬된 순서로 순회하며 현재 필드가 현재 배치에 추가 가능한지 판단:
     - 첫 번째 필드이거나 currentBatch가 없으면 → 새 배치 시작
     - field.type != currentBatch.type → 새 배치 시작
     - field.address != currentBatch.startAddress + currentBatch.totalLength → 새 배치 시작
     - currentBatch.totalLength + field.length > MAX_READ_QUANTITY → 새 배치 시작
3. 추가 가능하면: slice.offset = currentBatch.totalLength, currentBatch.totalLength += field.length
4. 전체 순회 후 마지막 배치 확정 → QList<RegisterBatch> 반환
```

결과 예시 (HoldingRegister, MAX=64):

```
필드 A: addr=100 len=2  ┐
필드 B: addr=102 len=4  ├── Batch 1: start=100, total=6
필드 C: addr=106 len=1  ┘
필드 D: addr=110 len=2  ── Batch 2: start=110, total=2  (갭: 107~109)
필드 E: addr=112 len=1  ── Batch 3: start=112, total=1  (타입이 다를 경우 분리)
```

---

## 변경 파일 및 내용

### 1. `comm/RegisterExecutor.h`

- `MAX_READ_QUANTITY = 64` — **배치 분할 기준 전용**. 개별 필드 길이 제한이 아님
- `readField()`의 `field.length > MAX_READ_QUANTITY` 검사 **제거** (개별 필드 길이는 디바이스 설정에 따름)
- `writeField()`의 동일 검사도 **제거**
- `readBatch()` 메서드 추가:

```cpp
bool readBatch(int startAddress,
               int totalLength,
               Model::RegisterType type,
               QVector<quint16> &registerValues,
               QVector<bool>    &coilValues,
               QString          &error);
```

### 2. `comm/RegisterExecutor.cpp`

`readBatch()` 구현:
- word 타입(HoldingRegister / InputRegister / WordRegister): `m_client->readWords(startAddress, totalLength, out, error)`
- bit 타입(Coil / DiscreteInput / BitRegister): `m_client->readBits(startAddress, totalLength, out, error)`
- `applyByteOrder`는 **호출하지 않음** — 분배 후 DataCollector에서 필드별 적용

### 3. `processor/DataCollector.h`

- `FieldResult` 구조체 추가 (public)
- `collectAllFields()` 메서드 추가:

```cpp
QList<FieldResult> collectAllFields();
```

- 기존 `collectField()` 유지 (단일 필드 읽기, writeField 검증 등 내부 재사용 가능성)

### 4. `processor/DataCollector.cpp`

`collectAllFields()` 흐름:

```
1. buildBatches() 호출 → QList<RegisterBatch> batches
2. QList<FieldResult> results(m_device.registers.size()) 초기화
3. for each batch:
     a. m_executor->readBatch(batch.startAddress, batch.totalLength, batch.type, rawWords, rawBits, error)
     b. 실패 시: batch.slices 내 모든 fieldIndex에 대해 results[idx].ok=false, results[idx].error=error
     c. 성공 시: for each slice:
          - rawWords.mid(slice.offset, slice.length) 슬라이싱
          - applyByteOrder(slice, effectiveByteOrder(field)) 적용
          - results[slice.fieldIndex].registerValues = 적용 결과
          - results[slice.fieldIndex].ok = true
4. return results
```

### 5. `polling/SerialWorker.cpp`

for-each-field 루프를 `collectAllFields()` 기반으로 교체:

```cpp
// 변경 전
for (const RegisterField &field : m_devices[i].registers) {
    QVector<quint16> regValues; QVector<bool> coilValues; QString error;
    const bool ok = collectors[i]->collectField(field, regValues, coilValues, error);
    m_table->updateUnifiedRegister(m_devices[i].id, field, regValues, coilValues, ok, error, ...);
}

// 변경 후
const QList<FieldResult> results = collectors[i]->collectAllFields();
for (int fi = 0; fi < m_devices[i].registers.size(); ++fi) {
    const auto &r = results.at(fi);
    m_registerTable->updateState(m_devices[i].registers[fi],
                                 r.registerValues, r.coilValues, r.ok, r.error, ...);
    if (!r.ok) { allOk = false; lastError = r.error; }
}
```

### 6. `polling/TcpWorker.cpp`

SerialWorker와 동일한 패턴으로 교체.

---

## 데이터 흐름 (변경 후)

```
Worker (Serial/TCP)
  └─ DataCollector::collectAllFields()
       ├─ buildBatches()          ← 필드 리스트 → 배치 그룹 계산
       └─ for each batch:
            RegisterExecutor::readBatch()
              └─ IDeviceClient::readWords() or readBits()
            → raw 배열을 slice별로 분배 + byteOrder 적용
       → QList<FieldResult>  (원본 필드 순서 유지)
  └─ RegisterTable::updateUnifiedRegister() (필드별)
```

---

## 변경 없는 항목

| 항목 | 이유 |
|------|------|
| `IDeviceClient` 인터페이스 | `readWords(addr, count)` 이미 range 지원 |
| `writeField` / `flushWrites` | 쓰기 배치는 이번 범위 외 |
| `RegisterField` 모델 | 필드 정의 변경 없음 |
| `RegisterTable` | 입력 인터페이스 동일 |
| PcLink 클라이언트 | `IDeviceClient::readWords` 동일 시그니처 사용 |

---

## 기대 효과

장비당 레지스터 10개가 모두 HoldingRegister 연속 주소인 경우:

| 항목 | 변경 전 | 변경 후 |
|------|---------|---------|
| Modbus 요청 수 | 10회 | 1회 |
| RS-485 turn-around | 10회 | 1회 |
| 폴링 소요 시간 | 10 × (RTT + TA) | 1 × (RTT + TA) |
