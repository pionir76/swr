# 로깅 설계 및 구현

## 목표

시스템 이벤트 및 장비 통신 상태 변화를 비동기로 DB에 기록하고 API로 제공한다.  
매 폴링마다 기록하는 방식 대신, **상태 전환 시점(정상↔불능)에만 경보를 기록**한다.

---

## 정책

| 항목 | 값 |
|------|-----|
| 최대 보관 건수 | 1000건 (`SR_MAX_LOG_LINES`, CMakeLists.txt에서 변경 가능) |
| 삭제 방식 | 1000건 초과 시 오래된 항목부터 자동 삭제 (`trimIfNeeded`) |
| 기록 방식 | 비동기 큐 → 전용 Writer 스레드 → 500ms 배치 flush |
| 경보 조건 | 장비 통신 상태 전환 시에만 기록 (매 폴링 아님) |

---

## 아키텍처

```
[시스템 어디서나]
  Util::Logger::info(...)    ──┐
  Util::Logger::warning(...) ──┼──→ QQueue (QMutex) ──→ Writer Thread ──→ logs.db
  Util::Logger::error(...)   ──┘         (push)           (500ms drain)
```

### 기존 방식과 비교

| 항목 | 기존 (폴링 로그) | 현재 (Logger) |
|------|------------------|---------------|
| 기록 시점 | 매 폴링 사이클 | 상태 전환 + 시스템 이벤트 |
| 스레드 안전성 | PollLogQueue (별도 클래스) | Logger 내부 QMutex |
| Writer | 별도 LogWriterThread | Logger 자체 QThread |
| 위치 | PollingManager 소속 | 시스템 전역 static |
| DB 테이블 | `device_poll_log` | `logs` |

---

## Logger 구조 (`utils/Logger.h/.cpp`)

```cpp
class Logger : public QThread {
public:
    static bool initialize(const QString &dbPath,
                           bool printToDebug = false,
                           int maxLines = 1000);
    static void shutdown();

    static void info(const QString &message);
    static void warning(const QString &message);
    static void error(const QString &message);

    static QList<LogEntry> fetch(int limit,
                                 const QString &level,
                                 const QString &from,
                                 const QString &to,
                                 QString &error);
};
```

- `initialize()` → `main.cpp`에서 최초 1회 호출, 내부 writer 스레드 시작
- `shutdown()` → `main.cpp`의 `app.exec()` 반환 후 호출, 잔여 큐 flush 후 종료
- `info/warning/error` → 어느 스레드에서나 호출 가능 (큐에 push하고 즉시 반환)
- `fetch()` → 호출 스레드별 읽기 전용 커넥션으로 DB 직접 조회

### Writer 스레드 동작

```
run() 진입
  └─ QSQLITE 커넥션 open (커넥션명: "logger_writer_{threadId}")
  └─ WAL 모드 활성화 (동시 읽기/쓰기 허용)
  └─ logs 테이블 CREATE IF NOT EXISTS
  └─ while (m_running):
       msleep(500)
       drain() → 큐에서 전체 pop → 단일 트랜잭션 INSERT → trimIfNeeded()
  └─ drain()  ← 종료 전 잔여 항목 플러시
  └─ 커넥션 close
```

---

## DB 스키마

```sql
-- logs 테이블 (Logger writer 스레드가 생성)
CREATE TABLE IF NOT EXISTS logs (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT    NOT NULL,   -- "yyyy-MM-dd HH:mm:ss"
    level     TEXT    NOT NULL,   -- "INFO" | "WARN" | "ERROR"
    message   TEXT    NOT NULL
);
```

DB 파일 경로: `SR_LOG_FILE` (기본값: `/var/log/swr/smartroute_log.db`)  
장비 DB(`SR_DB_FILE`)와 **별도 파일**로 관리된다.

---

## 경보 기록 — 장비 상태 전환 감지

`SerialWorker` 및 `TcpWorker`에서 폴링 완료 후 이전 상태와 비교한다.

```cpp
// 상태 전환 감지
if (prevState != status.state) {
    if (status.state == State::Error) {
        Util::Logger::warning(
            QStringLiteral("[경보] %1 통신 불능: %2").arg(device.name, lastError));
    } else if (status.state == State::Ok && prevState == State::Error) {
        Util::Logger::info(
            QStringLiteral("[복구] %1 통신 정상화").arg(device.name));
    }
    prevState = status.state;
}
```

| 전환 | 레벨 | 메시지 예시 |
|------|------|------------|
| 정상 → 불능 | WARN | `[경보] 냉동기1 통신 불능: CRC Error` |
| 불능 → 정상 | INFO | `[복구] 냉동기1 통신 정상화` |

- **최초 기동 시** (`Unknown → Ok`) 경보 없음 (정상 진입이므로)
- **불능 지속 중** 추가 경보 없음 (전환 시점에만 1회 기록)

---

## API

### `GET /api/logs`

**쿼리 파라미터**

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `limit` | int | 1000 | 페이지당 반환 건수 |
| `offset` | int | 0 | 건너뛸 항목 수 (페이지네이션) |
| `level` | string | (전체) | `INFO` \| `WARN` \| `ERROR` |
| `from` | string | (없음) | 시작 시간 `yyyy-MM-dd HH:mm:ss` |
| `to` | string | (없음) | 종료 시간 `yyyy-MM-dd HH:mm:ss` |

**페이지네이션 예시** (페이지당 20건)

```
1페이지: GET /api/logs?limit=20&offset=0
2페이지: GET /api/logs?limit=20&offset=20
3페이지: GET /api/logs?limit=20&offset=40
```

**응답**

```json
{
  "logs": [
    {
      "id": 205,
      "timestamp": "2026-06-25T10:24:31",
      "level": "WARN",
      "message": "[경보] 냉동기1 통신 불능: CRC Error"
    },
    {
      "id": 204,
      "timestamp": "2026-06-25T10:22:10",
      "level": "INFO",
      "message": "Polling started. Total devices: 3"
    }
  ],
  "count": 2,
  "total": 347,
  "limit": 20,
  "offset": 0,
  "level": "ALL",
  "from": "",
  "to": ""
}
```

- `count` — 이번 응답에 포함된 항목 수
- `total` — 필터 조건에 해당하는 전체 건수 (프론트에서 총 페이지 수 계산 용도)
- 결과는 `id DESC` 정렬 (최신 순)

---

## 대시보드 통합 API (`GET /api/dashboard`)

`logs` 테이블에서 레벨별로 분리해 두 섹션에 제공한다.

| 섹션 | 필드명 | 레벨 | 건수 |
|------|--------|------|------|
| 경보 패널 | `alerts` | WARN + ERROR | 최신 20건 |
| 로그 패널 | `logs` | INFO only | 최신 20건 |

---

## 변경 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `utils/Logger.h` | async QThread 기반으로 재작성, static initialize/shutdown 추가 |
| `utils/Logger.cpp` | 큐+Writer 스레드 구현, fetch용 별도 읽기 커넥션 |
| `data_collection/polling/SerialWorker.h/.cpp` | PollLogQueue 제거, 상태 전환 감지 → Logger 호출 |
| `data_collection/polling/TcpWorker.h/.cpp` | 동일 |
| `data_collection/polling/PollingManager.h/.cpp` | dbPath/logQueue/logWriter 멤버 제거, 생성자 단순화 |
| `data_collection/database/DeviceDatabase.h/.cpp` | `device_poll_log` 테이블/인덱스/fetchPollLog 제거 |
| `data_collection/model/DeviceModels.h` | `PollLogEntry`, `kPollLogMaxEntries` 제거 |
| `api/ApiServer.h/.cpp` | poll-log 라우트/핸들러 제거, dashboard alerts/logs 분리 |
| `main.cpp` | PollingManager 생성자 변경, Logger::shutdown() 추가 |
| `CMakeLists.txt` | PollLogQueue.h, LogWriterThread 파일 제거 |

### 삭제된 파일

| 파일 | 이유 |
|------|------|
| `data_collection/polling/PollLogQueue.h` | Logger 내부로 흡수 |
| `data_collection/polling/LogWriterThread.h/.cpp` | Logger로 통합 |
