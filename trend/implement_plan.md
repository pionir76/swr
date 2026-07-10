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

| 샘플 간격 | 레지스터 31개 기준 일일 쓰기량 |
|-----------|-------------------------------|
| 1초 | ~43 MB/day → **연간 15 GB 이상 — 위험** |
| 10초 | ~4.3 MB/day |
| 60초 | ~0.7 MB/day → **권장** |

→ 최소 샘플 간격 10초, 기본값 60초  
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
- DB 최대 용량 제한: **500 MB** (초과 시 오래된 데이터부터 삭제)

---

## DB 스키마

```sql
-- 어떤 레지스터를 기록할지 설정
CREATE TABLE IF NOT EXISTS trend_config (
    unified_register_id  INTEGER PRIMARY KEY,
    enabled              INTEGER NOT NULL DEFAULT 0,
    sample_interval_sec  INTEGER NOT NULL DEFAULT 60  -- 최소 10
);

-- 트렌드 원본 데이터 (Raw)
CREATE TABLE IF NOT EXISTS trend_data (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    reg_id  INTEGER NOT NULL,   -- unifiedRegisterId
    ts      INTEGER NOT NULL,   -- Unix timestamp (seconds)
    value   REAL    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_trend_reg_ts ON trend_data (reg_id, ts);

-- 5분 집계 데이터
CREATE TABLE IF NOT EXISTS trend_data_5m (
    reg_id  INTEGER NOT NULL,
    ts      INTEGER NOT NULL,   -- 5분 버킷 시작 Unix timestamp
    avg_val REAL    NOT NULL,
    min_val REAL    NOT NULL,
    max_val REAL    NOT NULL,
    PRIMARY KEY (reg_id, ts)
);

-- 1시간 집계 데이터
CREATE TABLE IF NOT EXISTS trend_data_1h (
    reg_id  INTEGER NOT NULL,
    ts      INTEGER NOT NULL,   -- 1시간 버킷 시작 Unix timestamp
    avg_val REAL    NOT NULL,
    min_val REAL    NOT NULL,
    max_val REAL    NOT NULL,
    PRIMARY KEY (reg_id, ts)
);
```

---

## 데이터 보존 정책 (Retention)

| 테이블 | 보존 기간 | 비고 |
|--------|-----------|------|
| `trend_data` (Raw) | **7일** | 원본 샘플 그대로 유지 |
| `trend_data_5m` | **30일** | 5분 평균/최솟값/최댓값 |
| `trend_data_1h` | **1년** | 1시간 평균/최솟값/최댓값 |

- 매일 1회 Daily Maintenance Job 실행:
  1. Raw → 5m 집계 생성
  2. 5m → 1h 집계 생성
  3. 만료 데이터 삭제
  4. `VACUUM` (단, 월 1회로 제한 — eMMC 쓰기 부하)

용량 예측 (레지스터 20개 기준):

| 테이블 | 예상 크기 |
|--------|-----------|
| Raw 7일 (60초 간격) | ~3 MB |
| 5m 30일 | ~5 MB |
| 1h 1년 | ~4 MB |
| **합계** | **~12 MB** |

---

## 아키텍처

```
PollingManager
      │ (기존, 변경 없음)
      ▼
RegisterTable  ◄────────────────────────────────┐
                                                 │ 주기적 읽기 (비침습적)
                                        TrendSampler  (신규 클래스)
                                          ├── QTimer (레지스터별 간격)
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

### `trend/TrendDatabase`
- `open(path)` / `close()`
- `insertBatch(QList<TrendPoint>)`
- `query(regId, from, to, resolution)` → `QList<TrendPoint>`
- `runMaintenance()` — 집계 및 만료 삭제

### `trend/TrendSampler`
- 생성자: `TrendSampler(RegisterTable*, TrendDatabase*, QObject*)`
- `applyConfig(QList<TrendConfig>)` — 활성 레지스터 및 간격 갱신
- 내부 QTimer로 각 레지스터를 설정 간격마다 샘플링
- 배치 버퍼 관리 및 TrendDatabase flush

---

## API

```
GET  /api/trend/config
     응답: 트렌드 활성화된 레지스터 목록 + 간격 설정

PUT  /api/trend/config/{unifiedRegisterId}
     바디: { "enabled": true, "sampleIntervalSec": 60 }
     응답: 200 OK

GET  /api/trend/data/{unifiedRegisterId}
     쿼리: ?from=<unix_ts>&to=<unix_ts>&resolution=raw|5m|1h
     응답:
     {
       "registerId": 14,
       "resolution": "5m",
       "data": [
         { "ts": 1720000000, "avg": 150.0, "min": 148.0, "max": 152.0 },
         ...
       ]
     }
     * 응답 포인트 수 최대 1,000개 (서버에서 자동 다운샘플링)
```

---

## 프론트엔드

- 그래프 라이브러리: **Chart.js** (번들 포함, CDN 없이 동작)
- 시간 범위 선택: `1h / 6h / 24h / 7d / 30d / 1y`
- 동시 표시 레지스터: 최대 **5개** (오버레이)
- 데이터 포인트: 서버에서 1,000개 제한 → 프론트 렌더링 부하 최소화

---

## 확정된 사양

| 항목 | 값 |
|------|----|
| 최소 샘플 간격 | 10초 |
| 기본 샘플 간격 | 60초 |
| 최대 트렌드 레지스터 수 | 20개 |
| Raw 보존 기간 | 7일 |
| 5분 집계 보존 기간 | 30일 |
| 1시간 집계 보존 기간 | 1년 |
| trend.db 최대 용량 | 500 MB |
| API 응답 최대 포인트 수 | 1,000개 |

---

## 구현 순서

1. `TrendDatabase` 클래스 — SQLite 스키마, 배치 INSERT, 쿼리
2. `TrendSampler` 클래스 — 타이머 기반 샘플링, 버퍼 관리
3. `main.cpp` — TrendSampler 생성 및 RegisterTable 연결
4. `ApiServer` — 트렌드 API 엔드포인트 추가
5. 프론트엔드 — 트렌드 설정 화면 + Chart.js 그래프
6. Daily Maintenance Job 연결
