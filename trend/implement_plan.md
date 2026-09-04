# Trend Recording — Implementation Plan

## 목적

사용자가 선택한 레지스터의 값 변화를 주기적으로 기록하고,  
웹 프론트엔드에서 시계열 그래프로 조회할 수 있도록 한다.

---

## 하드웨어 제약 조건

| 항목 | 사양 |
|------|------|
| CPU | i.MX6ULL (ARM Cortex-A7, 단일코어, 528 MHz) |
| 메모리 | 512 MB |
| 저장장치 | 8 GB eMMC (MLC 기준 P/E 10,000~30,000회) |
| OS | Yocto Linux |

### eMMC 수명 영향 분석

| 샘플 간격 | 레지스터 16개 기준 일일 쓰기량 |
|-----------|-------------------------------|
| 1초 | ~22 MB/day → **연간 8 GB 이상 — 위험** |
| 10초 | ~5.3 MB/day |
| 60초 | ~0.9 MB/day → **권장** |

→ 샘플 간격: 10 / 30 / 60초 중 선택 (UI Select Control, AppConfig 전역 설정)  
→ 메모리 버퍼링 후 일괄 쓰기(write batching) 필수

---

## 저장 방식

| 방식 | 장점 | 단점 | 채택 |
|------|------|------|------|
| SQLite (기존 인프라) | 이미 사용 중, 별도 의존성 없음 | 랜덤 I/O → WAL 모드 필요 | ✅ |
| RRDtool | 시계열 최적화, 자동 집계 | Yocto 레시피 추가, Qt 연동 복잡 | ✗ |
| 바이너리 파일 | I/O 최소 | 구현 복잡, 쿼리 불가 | ✗ |

- 전용 DB 파일: `/var/lib/swr/trend.db` (메인 DB와 I/O 분리)
- WAL 모드 활성화 (`PRAGMA journal_mode=WAL`)
- DB 최대 용량 제한: **3 GB** (초과 시 오래된 데이터부터 삭제)

---

## DB 스키마

-- ※ 채널 목록 및 샘플 간격은 AppConfig (config.json) 에서 관리
--   AppConfig::TrendConfig { sampleIntervalSec, channels: QList<TrendChannelConfig> }
--   trend.db 는 순수 데이터 테이블만 포함

-- 트렌드 원본 데이터 (Raw)
CREATE TABLE IF NOT EXISTS trend_data (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    reg_id  INTEGER NOT NULL,   -- unifiedRegisterId
    ts      INTEGER NOT NULL,   -- Unix timestamp (seconds)
    value   INTEGER NOT NULL    -- rawRegisters[0] 그대로 (quint16, 0~65535)
);
CREATE INDEX IF NOT EXISTS idx_trend_reg_ts ON trend_data (reg_id, ts);

-- 5분 집계 데이터
CREATE TABLE IF NOT EXISTS trend_data_5m (
    reg_id  INTEGER NOT NULL,
    ts      INTEGER NOT NULL,   -- 5분 버킷 시작 Unix timestamp
    avg_val INTEGER NOT NULL,   -- raw avg (소수점 반올림, quint16)
    min_val INTEGER NOT NULL,   -- raw min (quint16)
    max_val INTEGER NOT NULL,   -- raw max (quint16)
    PRIMARY KEY (reg_id, ts)
);

-- 10분 집계 데이터
CREATE TABLE IF NOT EXISTS trend_data_10m (
    reg_id  INTEGER NOT NULL,
    ts      INTEGER NOT NULL,   -- 10분 버킷 시작 Unix timestamp
    avg_val INTEGER NOT NULL,   -- raw avg (소수점 반올림, quint16)
    min_val INTEGER NOT NULL,   -- raw min (quint16)
    max_val INTEGER NOT NULL,   -- raw max (quint16)
    PRIMARY KEY (reg_id, ts)
);
```

---

## 데이터 보존 정책 (Retention)

| 테이블 | 보존 기간 | 비고 |
|--------|-----------|------|
| `trend_data` (Raw) | **1년** | 원본 샘플 그대로 유지 |
| `trend_data_5m` | **3년** | 5분 평균/최솟값/최댓값 |
| `trend_data_10m` | **5년** | 10분 평균/최솟값/최댓값 |

- **실시간 롤업** — 별도 스케줄링 없음:
  1. Raw INSERT 후 → 5분 버킷 완성 여부 확인 → `trend_data_5m` 즉시 생성
     - 버킷 시작 = `floor(ts / 300) * 300`
     - 버킷 종료 시각이 지난 시점에 avg/min/max 계산하여 INSERT
  2. 5m INSERT 후 → 2개 누적 확인 → `trend_data_10m` 즉시 생성
     - 버킷 시작 = `floor(ts / 600) * 600`
  3. 재부팅 안전: raw 데이터가 DB에 있으므로 미완성 버킷은 다음 샘플 도착 시 자동 완성

- **만료 데이터 삭제** — `QTimer(1h)` 단순 반복:
  ```sql
  DELETE FROM trend_data     WHERE ts < now - 365일
  DELETE FROM trend_data_5m  WHERE ts < now - 1095일
  DELETE FROM trend_data_10m WHERE ts < now - 1825일
  ```
  시스템 재부팅 후 최대 1시간 내 정리됨

- `VACUUM` — 월 1회 (eMMC 쓰기 부하 최소화)

용량 예측 (레지스터 16개 기준):

| 테이블 | 60초 간격 | 10초 간격 |
|--------|-----------|-----------|
| Raw 1년 | ~322 MB | ~1,927 MB |
| 5m 3년 | ~290 MB | ~290 MB |
| 10m 5년 | ~241 MB | ~241 MB |
| **합계** | **~853 MB** | **~2,458 MB ✓ (3 GB 이내)** |

---

## 아키텍처

```
PollingManager
      │ (기존, 변경 없음)
      ▼
RegisterTable  ◄────────────────────────────────┐
                                                 │ 주기적 읽기 (비침습적)
                                        TrendSampler  (신규 클래스)
                                          ├── QTimer (단일, 전역 간격)
                                          ├── 메모리 버퍼 (batch)
                                          │     └── N초마다 또는 M건 누적 시 flush
                                          └── TrendDatabase  (신규 클래스)
                                                └── trend.db (SQLite WAL)
                                                          │
                                          API: GET /api/trend/...
```

**설계 원칙**
- PollingManager / RegisterTable 기존 코드 수정 없음
- TrendSampler가 RegisterTable을 주기적으로 읽는 독립 타이머
- 배치 버퍼: 메모리에 최대 100건 또는 30초마다 일괄 INSERT

---

## 신규 클래스

### `trend/TrendDatabase` ✅ 구현 완료
- `open(path)` / `close()` / `isOpen()`
- `insertBatch(QList<TrendRawPoint>)` — 트랜잭션 INSERT 후 5m 롤업 자동 트리거
- `rollup5m(int regId, qint64 bucketTs)` — 버킷 완성 확인 → trend_data_5m INSERT → 10m 롤업 체인
- `rollup10m(int regId, qint64 bucketTs)` — 5m 2개 누적 시 → trend_data_10m INSERT
- `query(from, to, resolution, registers)` → `QMap<int, QList<TrendPoint>>`  
  (registers 비어 있으면 해당 기간 전체 채널 자동 반환, 채널당 ≤1,000 포인트 다운샘플링)
- `purgeExpired()` — 만료 데이터 삭제 (QTimer 1h마다 호출)
- 채널 설정/관리 없음 — AppConfig에서 담당

### `trend/TrendSampler`
- 생성자: `TrendSampler(RegisterTable*, TrendDatabase*, QObject*)`
- `applyConfig(const TrendConfig &)` — 채널 목록 + 샘플 간격 갱신, 채널 유무에 따라 자동 시작/정지
  - 채널 추가 → 자동 start / 채널 전체 제거 → 자동 stop (별도 start/stop 호출 불필요)
- 단일 QTimer로 전체 활성 채널을 동일 간격에 샘플링
- 배치 버퍼 관리 및 TrendDatabase flush (버퍼 ≥ 100건 또는 30초마다 강제 flush)

**샘플링 값 저장 사양:**
| 레지스터 타입 | 저장 값 |
|---|---|
| HoldingRegister / InputRegister | `rawRegisters[0]` (quint16, length 무관) |
| Coil / DiscreteInput | `rawCoils[0]` → 0 또는 1 (quint16) |
| 2레지스터(32비트) 채널 | **저장 불가** — UI 채널 선택 단계에서 차단 (length > 1 목록 비표시) |

---

## API

```
-- 트렌드 설정 조회 ✅ 구현 완료
GET  /api/trend/config
     응답: {
       "sampleIntervalSec": 10,
       "channels": [
         { "regId": 40003, "name": "메인룸 출력값", "unit": "%",
           "scale": 0.1, "isSigned": false, "minValue": 200, "maxValue": 800 },
         ...
       ]
     }

-- 트렌드 설정 변경 ✅ 구현 완료
PUT  /api/trend/config
     바디: {
       "sampleIntervalSec": 30,
       "channels": [
         { "regId": 40003, "name": "메인룸 출력값", "unit": "%",
           "scale": 0.1, "isSigned": false, "minValue": 200, "maxValue": 800 }
       ]
     }
     검증: sampleIntervalSec ∈ {10, 30, 60}, channels.length ≤ 16
     응답: (저장된 설정 전체 반환, GET 응답과 동일 구조)
     * 채널 목록 변경 시 기존 데이터 자동 삭제 후 저장 (별도 reset 호출 불필요)
     * 채널 설정 = 기록 시작 / 채널 전체 제거 = 기록 중단

-- 트렌드 기록 상태 조회 (미구현)
GET  /api/trend/status
     응답: { "recording": true, "channelCount": 5, "sampleIntervalSec": 10 }
     * recording = channels.size() > 0
     * 별도 start/stop 없음 — 채널 설정 여부로 결정

-- 트렌드 데이터 조회 단일 엔드포인트 ✅ 구현 완료
GET  /api/trend/data
     쿼리: ?from=<unix_ts>&to=<unix_ts>&resolution=raw|5m|10m[&registers=3,7,14]
     응답:
     {
       "from": 1720000000,
       "to":   1722678400,
       "resolution": "10m",
       "configuredChannels": [40003, 40004, 40006, 40009, ...],
       "meta": {
         "40003": {
           "name": "메인룸 출력값", "unit": "%",
           "scale": 0.1, "isSigned": false, "minValue": 200, "maxValue": 800,
           "avg": 492, "min": 201, "max": 798
         }
       },
       "channels": {
         "40003": [ { "ts": 1720000000, "v": 492 }, { "ts": 1720000600, "v": 521 }, ... ]
       }
     }
     * configuredChannels    — AppConfig 기준 현재 레코딩 설정된 채널 ID 목록
                               (프론트 슬롯 그리드 구성용 — GET /api/trend/config 별도 호출 불필요)
     * registers 생략 시 해당 기간에 기록된 모든 채널 반환
     * 채널당 최대 1,000 포인트 (서버에서 자동 다운샘플링)
     * meta[regId].name / unit / scale / isSigned — TrendChannelConfig에서 직접 반환
     * meta[regId].minValue / maxValue — Y축 표시 범위 (raw quint16 단위)
     * meta[regId].avg / min / max     — 조회 기간 전체 통계 (raw quint16 단위)
     * channels[regId]       — 시계열 포인트: ts + v(raw quint16 값)만 포함
     * v 값은 raw 정수 — 프론트에서 meta[regId].scale 곱하여 표시값 계산
       예: v=492, scale=0.1 → 표시값 49.2 (fixed point 개념)
     * configuredChannels에 있으나 channels에 없는 채널 → "데이터 없음" 카드로 표시
```

---

## 프론트엔드

- 그래프 라이브러리: **Chart.js** (번들 포함, CDN 없이 동작)
- 시간 범위 선택: `1h / 6h / 24h / 7d / 30d / 1y`
- 데이터 포인트: 서버에서 1,000개 제한 → 프론트 렌더링 부하 최소화

---

### Resolution 자동 선택 로직 (프론트 담당)

API의 `resolution` 파라미터는 프론트가 **시간 범위에 따라 자동 결정**한다.  
사용자는 raw/5m/10m 개념을 알 필요 없이 시간 범위만 선택한다.(알아서도 안됨.)

**선택 기준 (목표 포인트 수 기준 — B안 채택):**
```
target = 1,000 포인트 (서버 최대 반환 수)

(to - from) / 10   ≤ target  →  resolution = "raw"   (범위 ≤ ~2.7h)
(to - from) / 300  ≤ target  →  resolution = "5m"    (범위 ≤ ~8.3일)
그 외                         →  resolution = "10m"
```

**시간 범위별 실제 적용:**

| 시간 범위 선택 | 자동 선택 resolution | 반환 포인트 수 (16ch 기준) |
|---|---|---|
| 1h | raw | 360개 (다운샘플 없음) |
| 6h | raw | 2,160개 → 1,000으로 다운샘플 |
| 24h | 5m | 288개 (다운샘플 없음) |
| 7d | 5m | 2,016개 → 1,000으로 다운샘플 |
| 30d | 10m | 4,320개 → 1,000으로 다운샘플 |
| 1y | 10m | 52,560개 → 1,000으로 다운샘플 |

**단기 이벤트(스파이크) 주의사항:**
- 5m/10m 집계 데이터는 버킷 내 avg값만 표시되므로 10초 단위 순간 이벤트가 평균에 묻힐 수 있음
- meta의 `min/max`는 전체 조회 기간의 극값이므로 Y축 범위 설정에만 활용
- 단기 이벤트 확인이 필요한 경우 사용자가 시간 범위를 좁혀 raw로 재조회

**줌 인터랙션 (향후 확장):**
- 그래프 구간 드래그 줌 → `from/to` 재계산 → 동일 로직으로 resolution 재결정 → 재요청
- 별도 API 변경 없이 프론트 로직만으로 구현 가능

---

## 확정된 사양

| 항목 | 값 |
|------|----|
| 샘플 간격 설정 | AppConfig `trendSampleIntervalSec` — 10 / 30 / 60초 선택 (기본 10) |
| 최대 트렌드 채널 수 | 16개 |
| 레코딩 중 설정 변경 | 불가 (정지 후 변경) |
| Raw 보존 기간 | 1년 |
| 5분 집계 보존 기간 | 3년 |
| 10분 집계 보존 기간 | 5년 |
| trend.db 최대 용량 | 3 GB |
| API 응답 최대 포인트 수 | 채널당 1,000개 (자동 다운샘플링) |

---



---

# 프론트엔드 연동 가이드 (Frontend Integration Guide)

> 이 섹션은 프론트엔드 개발자와 공유하기 위한 문서입니다.

---

## 데이터 저장 구조 개요

트렌드 데이터는 해상도에 따라 3개의 독립 테이블에 분리 저장됩니다.  
각 테이블은 서로 독립적으로 만료되며, 상호 의존 관계가 없습니다.

| 테이블 | 해상도 | 보존 기간 | 저장 내용 |
|--------|--------|-----------|-----------|
| `trend_data` | 10초 (raw) | **1년** | 원본 수집값 |
| `trend_data_5m` | 5분 | **3년** | 5분 구간 avg / min / max |
| `trend_data_10m` | 10분 | **5년** | 10분 구간 avg / min / max |

- 총 DB 용량 한도: **3 GB**
- raw가 1년 후 삭제되어도 5m/10m 집계값은 독립적으로 유지됩니다.
- 각 테이블은 동일한 기간의 데이터를 보유하지 않습니다 — 오래된 데이터일수록 낮은 해상도만 조회 가능합니다.

---

## API 엔드포인트

```
GET /api/trend/data?from=<unix_ts>&to=<unix_ts>&resolution=raw|5m|10m
```

- `resolution` 파라미터는 **프론트엔드가 직접 결정**하여 전송합니다.
- 서버는 지정된 테이블에서 데이터를 조회하며, 해당 기간 데이터가 없으면 빈 응답을 반환합니다.
- 채널당 최대 **1,000 포인트** (서버에서 자동 다운샘플링).

---

## Resolution 자동 선택 규칙

프론트엔드는 `from` 기준의 **데이터 나이**와 **조회 범위 폭** 두 가지를 조합하여 resolution을 결정해야 합니다.

### 규칙 요약

```
1. 데이터 나이 우선 판단 (가용성)
   from 기준 나이 > 3년  →  "10m"  고정  (5m 테이블 만료)
   from 기준 나이 > 1년  →  "5m" 또는 "10m"  (raw 테이블 만료)

2. 나이 ≤ 1년이면 조회 범위 폭으로 결정 (성능)
   범위 ≤ 약 2.7시간  →  "raw"
   범위 ≤ 약 8.3일    →  "5m"
   범위 > 8.3일       →  "10m"
```

### JavaScript 구현 예시

```js
function selectResolution(from, to) {
    const now  = Math.floor(Date.now() / 1000);
    const ageYears  = (now - from) / (365 * 86400);
    const rangeDays = (to - from) / 86400;

    // 데이터 나이 우선
    if (ageYears > 3) return "10m";          // raw·5m 모두 만료
    if (ageYears > 1) {
        return rangeDays > 30 ? "10m" : "5m"; // raw만 만료
    }

    // 최근 1년 이내 — 범위 폭으로 결정
    const rangeSeconds = to - from;
    if (rangeSeconds / 10  <= 1000) return "raw";
    if (rangeSeconds / 300 <= 1000) return "5m";
    return "10m";
}
```

### 시간 범위 선택 버튼별 기본 동작 (데이터가 최근인 경우)

| 선택 범위 | 자동 resolution | 서버 반환 포인트 (추정) |
|-----------|-----------------|------------------------|
| 1h | raw | ~360개 |
| 6h | raw | ~2,160 → 1,000으로 다운샘플 |
| 24h | 5m | ~288개 |
| 7d | 5m | ~2,016 → 1,000으로 다운샘플 |
| 30d | 10m | ~4,320 → 1,000으로 다운샘플 |
| 1y | 10m | ~52,560 → 1,000으로 다운샘플 |

---

## 응답 데이터 소수점 표시

`v`는 raw 정수값입니다. `meta[regId].scale`을 곱한 뒤 소수점 자리수를 결정합니다.

```js
function getDecimals(scale) {
    if (scale >= 1) return 0;
    return Math.max(0, Math.round(-Math.log10(scale)));
    // scale=1.0 → 0자리, scale=0.1 → 1자리, scale=0.01 → 2자리
}

const scale = meta[regId].scale;
const displayVal = v * scale;
displayVal.toFixed(getDecimals(scale))
// 예: v=492, scale=0.1  → displayVal=49.2  → "49.2"
//     v=314, scale=0.01 → displayVal=3.14  → "3.14"
//     v=60,  scale=1.0  → displayVal=60    → "60"
```

---

## 주의사항

- **빈 channels 응답**: 요청 기간의 데이터가 없는 경우 해당 채널은 응답에 포함되지 않습니다.  
  `configuredChannels`에 있으나 `channels`에 없는 채널은 "데이터 없음"으로 표시하세요.
- **5m/10m의 v 값**: 해당 구간의 평균값(raw 정수 반올림)입니다. 순간 스파이크는 raw 조회로만 확인 가능합니다.
- **minValue / maxValue**: raw 단위의 Y축 범위입니다. 프론트 Y축 초기값으로 사용하고, scale을 곱하면 표시 단위 범위가 됩니다.
  예: minValue=200, scale=0.1 → Y축 하한 20.0

---

---

## 새로운 트렌드 기록시의 시나리오

사용자가 트렌드 채널 설정을 변경하면 기존 데이터는 모두 삭제됩니다.
데이터 손실이 발생하는 작업이므로 아래 흐름으로 처리합니다.

### 변경 조건 구분

| 변경 내용 | 데이터 삭제 | 처리 |
|-----------|------------|------|
| 채널 목록 변경 (regId 추가/제거) | **O** | 경고 모달 → 삭제 후 적용 |
| 샘플 간격만 변경 | X | 일반 confirm으로 처리 |

### UI 시나리오 (프론트엔드)

**1단계 — 변경 감지** (적용 버튼 클릭 시)
- 현재 선택된 채널과 기존 `cfg.channels`를 비교
- 채널 목록이 달라진 경우에만 경고 모달 진입

**2단계 — 경고 모달**
```
┌─────────────────────────────────────────────┐
│  ⚠️  트렌드 채널이 변경됩니다               │
│                                             │
│  채널 변경 시 기존 트렌드 데이터가          │
│  모두 삭제됩니다. 삭제 전 raw 데이터를      │
│  다운로드하여 보관할 수 있습니다.           │
│                                             │
│  [📥 raw 데이터 다운로드]                   │
│                                             │
│         [취소]  [데이터 삭제 후 적용]       │
└─────────────────────────────────────────────┘
```
- 다운로드는 선택 사항 — 강제하지 않음
- "데이터 삭제 후 적용" 버튼은 다운로드 완료 여부와 무관하게 항상 활성화

**3단계 — 적용 실행**
```
PUT /api/trend/config   ← 채널 변경 감지 + DB 리셋 + config 저장을 서버에서 원자적으로 처리
성공 메시지 표시 + 화면 갱신
```

### 데이터 백업(다운로드) 범위 지정

사용자가 다운로드 버튼을 누르면 프론트는 저장된 데이터의 실제 범위를 기준으로 날짜 피커를 제공합니다.

**범위 정보 취득 방법:**
트렌드 페이지 진입 시 호출하는 `GET /api/trend/config` 응답에 `dataRange` 필드가 포함됩니다.
별도 API 호출 없이 한 번의 요청으로 채널 설정과 데이터 범위를 동시에 취득합니다.

데이터 있을 때:+
{
  "sampleIntervalSec": 10,
  "channels": [ ... ],
  "dataRange": {
    "from": 1720000000,
    "to":   1753660800
  }
}
데이터 없을 때 (초기 상태):
{
  "sampleIntervalSec": 10,
  "channels": [ ... ],
  "dataRange": null
}

```
- `dataRange`가 `null`이면 저장된 데이터 없음 → 다운로드 버튼 비활성화
- `from` / `to` 값이 있으면 해당 범위 내에서만 날짜 피커 선택 허용

**다운로드 실행:**
```
GET /api/trend/export?from=<unix_ts>&to=<unix_ts>
    응답: CSV 파일 (Content-Disposition: attachment)
    컬럼: timestamp, datetime, reg_id, channel_name, value
    * resolution 고정 = raw (export는 항상 원본 데이터)
    * registers 파라미터 없음 — 전체 채널 포함
```

### API 구현 현황

```
-- GET /api/trend/config ✅ 구현 완료
    dataRange 필드 포함 — { from, to } 또는 null (데이터 없을 때)

-- GET /api/trend/export ✅ 구현 완료
    raw CSV 다운로드, from/to 범위 지정 필수

-- PUT /api/trend/config 채널 변경 감지 동작 ✅ 구현 완료
    채널 regId 목록이 변경됐을 때 → TrendDatabase::deleteAll() 호출 후 config 저장
    샘플 간격만 변경됐을 때 → deleteAll 없이 config만 저장
```

### 백엔드 처리 흐름 (`handlePutTrendConfig`)

```
요청 수신
  ├── 채널 변경 감지: 기존 regId 집합 vs 새 regId 집합 비교
  ├── 변경됨 → TrendDatabase::deleteAll()
  │     실패 시 → 500 반환 (config 저장 안 함)
  ├── config 저장
  └── 200 OK (저장된 config 반환)
```

---

## 구현 순서

1. ✅ `TrendDatabase` 클래스 — SQLite 스키마, 배치 INSERT, 롤업, 쿼리, deleteAll
1. ✅ `AppConfig::TrendConfig` — 채널 목록 + 샘플 간격 (config.json 저장)
1. ✅ `GET /api/trend/config`, `PUT /api/trend/config` — 설정 조회/변경 + 채널 변경 시 DB 리셋
1. ✅ `GET /api/trend/export` — raw CSV 다운로드
2. ✅ `TrendSampler` 클래스 — 타이머 기반 샘플링, 배치 버퍼 관리
3. `main.cpp` — TrendSampler 생성 및 RegisterTable / TrendDatabase 연결, 채널 설정 시 자동 시작
4. `ApiServer` — `GET /api/trend/status` 추가
5. 프론트엔드 — 트렌드 설정 화면 + Chart.js 그래프

---