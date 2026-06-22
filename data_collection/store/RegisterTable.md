# First Level Heading

Paragraph.

## Second Level Heading

Paragraph.

- bullet
+ other bullet
* another bullet
    * child bullet

1. ordered
2. next ordered

### Third Level Heading

Some *italic* and **bold** text and `inline code`.

An empty line starts a new paragraph.

Use two spaces at the end  
to force a line break.

A horizontal ruler follows:

---

Add links inline like [this link to the Qt homepage](https://www.qt.io),
or with a reference like [this other link to the Qt homepage][1].

    Add code blocks with
    four spaces at the front.

> A blockquote
> starts with >
>
> and has the same paragraph rules as normal text.

First Level Heading in Alternate Style
======================================# RegisterTable 클래스 설명

## 개요

Modbus 장치에서 수집한 레지스터/코일 값을 **쓰레드 안전하게 저장하고 조회**하는 인메모리 저장소입니다.  
세 가지 저장 레이어를 갖습니다.

---

## 내부 저장소 구조

```
m_unifiedRegisters: QHash<unifiedId, UnifiedRegister>
m_fieldToUnifiedId: QHash<"deviceId:address:type", unifiedId>
```

| 저장소 | 역할 |
|--------|------|
| `m_unifiedRegisters` | 스케일 변환·범위검사까지 완료된 통합 레지스터 (rawRegisters/rawCoils 포함) |
| `m_fieldToUnifiedId` | `(deviceId, address, type)` → 정수 ID 매핑 캐시 |

> 원시값(raw)은 별도 테이블 없이 `UnifiedRegister.rawRegisters` / `UnifiedRegister.rawCoils` 에 직접 저장된다.

---

## 핵심 흐름: `updateUnifiedRegister`

```
폴링 결과 도착
    │
    ▼
resolveUnifiedId()  ← (deviceId + address + type) 조합으로 정수 ID 발급/조회
    │
    ▼
entry 필드 채우기 (name, unit, scale, minValue, maxValue ...)
    │
    ├─ success == true
    │       ▼
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
        rawRegisters/rawCoils 초기화, scaledValue=0, isValid=false
    │
    ▼
m_unifiedRegisters[unifiedId] = entry  (저장 완료)
```

---

## `updateUnifiedRegister` 주요 필드 매핑

| `UnifiedRegister` 필드 | 소스 |
|---|---|
| `name` | `field.displayName` (없으면 `field.tagName`) |
| `deviceId` | `int deviceId` 파라미터 (DeviceInfo.id) |
| `deviceAddress` | `field.address` |

---

## `resolveUnifiedId` — ID 발급 로직

`RegisterField.unifiedRegisterId`가 설정되어 있으면 그 값을 직접 사용하고,  
없으면 `m_nextUnifiedId`를 자동 증가시켜 새 ID를 발급합니다.  
같은 `(deviceId, address, type)` 조합은 항상 동일 ID를 반환합니다.

---

## `computeScaledValue` — 원시값 → 물리값 변환

### 레지스터(quint16) 경우

레지스터가 2개 이상이면 16비트씩 왼쪽으로 시프트하며 합쳐 32/64비트 정수로 만든 뒤 `scale`을 곱합니다.  
1개면 그냥 해당 값 × `scale`.

```
예) rawRegisters = [0x0001, 0x86A0]
combined = (0x0001 << 16) | 0x86A0 = 100000
scaledValue = 100000 × 0.01 = 1000.0
```

### 코일(bool) 경우

- 1개: `true` → `1.0 × scale`, `false` → `0.0`
- 여러 개: `true` 개수 × `scale`

---

## 쓰레드 안전성

모든 public 메서드에서 `QMutexLocker locker(&m_mutex)` 로 진입 즉시 락을 잡습니다.  
`m_mutex`가 `mutable`로 선언되어 있어 `const` 조회 메서드도 락을 사용할 수 있습니다.

---

## 요약

> **"Modbus 폴링 원시값을 받아서 → 통합 ID를 부여하고 → 스케일·범위를 적용해 → 쓰레드 안전하게 저장하는 인메모리 캐시 테이블"**


Paragraph.

Second Level Heading in Alternate Style
---------------------------------------

Paragraph.

[1]: https://www.qt.io
