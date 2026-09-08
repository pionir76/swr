# 파일 기반 트렌드 레코딩 — 파일 포맷 및 API 사양

> 작성일: 2026-08-20  
> 최종 수정: 2026-09-08  
> 상태: 확정 / 구현 완료 (SQLite 방식 완전 제거, 파일 기반 단일 운용)

---

## 1. 개요

바이너리 파일 기반 레코딩 방식을 단일 운용합니다. SQLite 기반 트렌드는 제거되었습니다.  
서버는 파일 I/O(append / seek)만 담당하며 집계·계산은 수행하지 않습니다.

| 항목 | 내용 |
|---|---|
| 파일 확장자 | `.tnd` |
| 파일 저장 경로 | `/trend_data/yymmdd_hhmmss.tnd` |
| 최대 용량 | 4 GB (초과 시 자동 기록 중지) |
| 자동 삭제 | 없음 — 사용자가 DELETE API로 직접 삭제 |
| 응답 형식 | 순수 바이너리 (`application/octet-stream`) |
| WebSocket | 사용하지 않음 — HTTP 폴링(1초, 32 bytes)으로 대체 |

---

## 2. 파일 레이아웃

```
[0x000 ~ 0x1FF]  메인 헤더        512 bytes
[0x200 ~ 0x5FF]  블록 0          1024 bytes
[0x600 ~ 0x9FF]  블록 1          1024 bytes
...

블록 오프셋 = 512 + block_index × 1024
```

---

## 3. 메인 헤더 (512 bytes)

### 3-1. 고정 영역 (0 ~ 63 bytes)

| 오프셋 | 크기 | 필드 | 내용 |
|---|---|---|---|
| 0 | 2 | ID | 매직 넘버 `"SW"` (0x53, 0x57) |
| 2 | 1 | MTYPE | SmartRoute = `0x30` |
| 3 | 1 | VER | 포맷 버전 |
| 4 | 1 | REV | 리비전 |
| 5 | 1 | CHCNT | 실제 채널 수 (1 ~ 16) |
| 6 | 6 | STIME | 녹화 시작 시각 (yymmddhhmmss, binary — not BCD) |
| 12 | 2 | FREQ | 샘플 간격 (초): 1 / 5 / 10 / 30 / 60 |
| 14 | 1 | ZCODE | 특수 코드 |
| 15 | 1 | FVER | 펌웨어 버전 |
| 16 | 48 | — | reserved |

### 3-2. 채널 영역 (64 ~ 511 bytes) — 채널 우선 배치

채널 0부터 순서대로, 채널당 28 bytes. 채널 N의 시작 오프셋:

```
채널 N 오프셋 = 64 + N × 28
```

| 필드 내 오프셋 | 크기 | 필드 | 내용 |
|---|---|---|---|
| +0 | 16 | CHTAG | 채널 태그명 (ASCII, null-padded, 최대 16자) |
| +16 | 2 | INRH | 상한 raw 값 (uint16) |
| +18 | 2 | INRL | 하한 raw 값 (uint16) |
| +20 | 1 | DOTPOS | 소수점 자릿수 (0=정수, 1→×0.1, 2→×0.01) |
| +21 | 1 | IS_SIGNED | 부호 여부 (0: unsigned, 1: signed) |
| +22 | 4 | UNIT | 단위 문자열 (ASCII, null-padded) |
| +26 | 2 | — | reserved |

> CHTAG를 16 bytes로 확장 (reserved 10 → 2 bytes로 축소), 채널 구조체 총 28 bytes 유지  
> uint16 필드(INRH, INRL)는 짝수 오프셋에 배치되어 비정렬 접근 없음

16채널 × 28 bytes = **448 bytes**  
고정 영역 64 bytes + 채널 영역 448 bytes = **512 bytes** ✓

**실수값 변환:**
```
unsigned: 실수값 = raw / 10^DOTPOS
signed:   실수값 = (raw > 32767 ? raw - 65536 : raw) / 10^DOTPOS
```

---

## 4. 데이터 블록 (1024 bytes, 반복)

### 4-1. 블록 헤더 (8 bytes)

| 오프셋 | 크기 | 필드 | 내용 |
|---|---|---|---|
| 0 | 4 | BLKINDEX | 블록 순번 (0-based, uint32) |
| 4 | 2 | DATACNT | 이 블록의 유효 레코드 수 (uint16) |
| 6 | 2 | — | reserved |

### 4-2. 데이터 영역 (1016 bytes)

- 레코드 1개 = 16채널 × 2 bytes = **32 bytes** (uint16 raw 값, 미사용 채널 = 0)
- 블록당 최대 레코드 수 = **31개** (31 × 32 = 992 bytes, 나머지 24 bytes reserved)

### 4-3. 타임스탬프 계산

블록 내에 타임스탬프를 별도 저장하지 않습니다.  
메인 헤더의 STIME, FREQ, 레코드 인덱스로 계산합니다.

```
record_index  = block_index × 31 + record_in_block
timestamp     = STIME + record_index × FREQ
```

**특정 시각 → 블록/레코드 역산:**
```
record_index    = (target_time - STIME) / FREQ
block_index     = record_index / 31
record_in_block = record_index % 31
block_offset    = 512 + block_index × 1024
record_offset   = block_offset + 8 + record_in_block × 32
```

> **⚠️ 구현 주의:** 마지막 블록은 31개 미만의 유효 레코드를 가질 수 있습니다.  
> 블록 헤더의 `DATACNT`를 반드시 확인하여 `record_in_block < DATACNT` 인 경우에만 레코드를 읽어야 합니다.  
> `DATACNT` 범위를 초과한 오프셋의 데이터는 미초기화 상태이므로 무시해야 합니다.

---

## 5. 레코드 주기 및 파일 크기 예상

| 주기 (FREQ) | 블록당 커버 시간 | 1일 크기 | 1달 크기 |
|---|---|---|---|
| 1초 | ~31초 | 2.8 MB | 83.6 MB |
| 5초 | ~155초 | 564 KB | 16.9 MB |
| 10초 | ~310초 | 279 KB | 8.4 MB |
| 30초 | ~930초 | 94 KB | 2.8 MB |
| 60초 | ~1860초 | 47 KB | 1.4 MB |

---

## 6. API 사양

| 메서드 | 엔드포인트 | 내용 |
|---|---|---|
| POST | `/api/tfile/start` | 녹화 시작 (새 파일 생성) |
| POST | `/api/tfile/stop` | 녹화 정지 |
| GET | `/api/tfile/status` | 현재 녹화 상태 조회 |
| GET | `/api/tfile/files` | 저장된 파일 목록 |
| DELETE | `/api/tfile/files/{filename}` | 파일 삭제 |
| GET | `/api/tfile/header?file=` | 메인 헤더 512 bytes 반환 |
| GET | `/api/tfile/latest?file=` | 최신 레코드 1개 (32 bytes) 반환 |
| GET | `/api/tfile/sample?file=&from=&to=&step=` | 구간 데이터 반환 |

### 6-1. `/api/tfile/sample` 상세

```
GET /api/tfile/sample?file={filename}&from={unix_sec}&to={unix_sec}&step={N}
```

| 파라미터 | 타입 | 내용 |
|---|---|---|
| file | string | 파일명 |
| from | uint32 | 조회 시작 (unix 초) |
| to | uint32 | 조회 종료 (unix 초) |
| step | uint16 | 레코드 건너뛰기 간격 (최솟값 1) |

**백엔드 처리:**
1. 헤더에서 STIME, FREQ 읽기
2. from/to → 레코드 인덱스 변환 (`record_index = (time - STIME) / FREQ`)
3. step 간격으로 레코드 seek → 읽기
4. 순수 바이너리 응답

**응답 형식:** 레코드 배열 (헤더 없음)
```
[record_0][record_1]...[record_N]
각 record = 16 × uint16 = 32 bytes
```

### 6-2. 데이터 조회 흐름

```
① GET /api/tfile/files
   → 파일 목록 (파일명, 크기, 녹화 시작/종료 시각)

② GET /api/tfile/header?file={filename}
   → 메인 헤더 512 bytes
   → CHCNT, STIME, FREQ, 채널 정보(CHTAG/INRH/INRL/DOTPOS/IS_SIGNED/UNIT) 파싱
   → 이후 sample/latest 바이너리 해석 기준 확보

③ GET /api/tfile/sample?...  또는  GET /api/tfile/latest?...
   → 순수 바이너리 레코드 수신 → 차트 렌더링
```

### 6-3. 실시간 모드 폴링 흐름

```
① 실시간 모드 진입
   GET /api/tfile/sample?file=xxx&from=now-1200&to=now&step=1
   → 초기 1200 레코드 수신 (~38 KB), 차트 초기 렌더링

② 1초마다
   GET /api/tfile/latest?file=xxx
   → 최신 레코드 32 bytes 수신
   → 차트 우측 append + 좌측 제거 (window shift)
```

---

## 7. 화면 모드

| 모드 | 전환 조건 | Pan/Zoom | 갱신 방식 |
|---|---|---|---|
| **실시간** | 기본 (녹화 중) | ❌ 불가 | 1초 폴링 (32 bytes) |
| **탐색** | 실시간 버튼 OFF | ✅ 가능 | on-demand |
| **파일 열기** | 저장 파일 선택 | ✅ 가능 | on-demand |

---

## 8. step 계산 (프론트엔드 담당)

백엔드는 step 계산 없이 요청받은 값대로 seek만 수행합니다.

```
available_records = (to - from) / FREQ
step = max(1, ceil(available_records / canvas_width_px))
```

**예시 (캔버스 1200px, FREQ=1초)**

| 배율 | 시간 범위 | 레코드 수 | step | 응답 크기 |
|---|---|---|---|---|
| 0.25x | 4800초 | 4800개 | 4 | ~38 KB |
| 0.5x | 2400초 | 2400개 | 2 | ~38 KB |
| **1:1** | **1200초** | **1200개** | **1** | **~38 KB** |
| 2x | 600초 | 600개 | 1 | ~19 KB |
| 4x | 300초 | 300개 | 1 | ~9.6 KB |

> step 최솟값이 1이므로 응답 크기는 항상 **캔버스 크기 × 32 bytes 이하**로 유지됩니다.

---

## 9. 부하 평가

| 동작 | 서버 처리 | 전송량 | 부하 |
|---|---|---|---|
| 레코딩 (1초 주기) | 파일 append | 32 bytes | ✅ 매우 낮음 |
| 실시간 폴링 (latest) | 파일 tail seek | 32 bytes/초 | ✅ 매우 낮음 |
| 탐색 모드 sample | 파일 seek + 복사 | ≤38 KB/요청 | ✅ 낮음 |
| 파일 목록 | 디렉터리 읽기 | 소량 | ✅ 낮음 |

---

## 10. API 응답 포맷 상세

### 10-0. `POST /api/tfile/start`

```json
{ "ok": true, "filename": "260821_053627.tnd" }
```

| 필드 | 타입 | 내용 |
|---|---|---|
| `ok` | bool | 녹화 시작 성공 여부 |
| `filename` | string | 생성된 파일명 |

> 실패 시 HTTP 4xx/5xx + `{ "error": "..." }` 반환

### 10-1. `GET /api/tfile/status`

녹화 상태와 용량 정보를 단일 응답으로 제공합니다.

```json
{
  "recording": true,
  "filename": "260803_120000.tnd",
  "elapsedSec": 3600,
  "fileSize": 10485760,
  "fileSizeLimit": 4294967296,
  "recordCount": 3600,
  "freq": 1
}
```

| 필드 | 타입 | 내용 |
|---|---|---|
| `recording` | bool | 현재 녹화 중 여부 |
| `filename` | string | 녹화 중인 파일명 (recording=false 시 빈 문자열) |
| `elapsedSec` | uint32 | 녹화 시작 이후 경과 초 |
| `fileSize` | uint64 | 현재 파일 크기 (bytes) |
| `fileSizeLimit` | uint64 | 최대 파일 크기 (4 GB = 4,294,967,296) |
| `recordCount` | uint32 | 누적 레코드 수 |
| `freq` | uint16 | 샘플 간격 (초) |

> 용량 초과 시 레코딩은 자동 중단됩니다 (`recording: false`).  
> 저장공간 사용률은 `GET /api/system/resources` (시스템 전체 저장공간)를 재사용합니다.

### 10-2. `GET /api/tfile/files`

```json
{
  "files": [
    {
      "filename": "260803_120000.tnd",
      "fileSize": 10485760,
      "startTime": 1754179200,
      "endTime":   1754265600,
      "recordCount": 86400,
      "freq": 1,
      "chCount": 8
    }
  ]
}
```

| 필드 | 타입 | 내용 |
|---|---|---|
| `filename` | string | 파일명 |
| `fileSize` | uint64 | 파일 크기 (bytes) |
| `startTime` | uint32 | 녹화 시작 시각 (unix 초) |
| `endTime` | uint32 | 마지막 레코드 시각 (unix 초) |
| `recordCount` | uint32 | 전체 레코드 수 |
| `freq` | uint16 | 샘플 간격 (초) |
| `chCount` | uint8 | 채널 수 (CHCNT) |

> `recordCount` 는 서버가 `(fileSize - 512) / 1024 × 31 + 마지막 블록 DATACNT` 로 계산하여 반환  
> 파일 목록 화면에서 헤더 별도 요청 없이 날짜 범위·채널 수를 표시할 수 있도록 포함
