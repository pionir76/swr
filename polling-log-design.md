# 폴링 로그 설계 및 구현

## 목표

장비별 Modbus 폴링 결과(성공/실패, 소요시간, 메시지)를 DB에 기록하고 API로 제공한다.

---

## 정책

| 항목 | 값 |
|------|-----|
| 기록 시점 | 매 폴링 사이클 완료 시 |
| 장비당 최대 보관 건수 | 100건 (101번째 INSERT 시 가장 오래된 것 자동 삭제) |
| 삭제 방식 | INSERT 직후 `NOT IN (SELECT ... LIMIT 100)` 조건 DELETE |

---

## 동시성 문제와 해결 방안

TcpWorker는 장비 1개당 독립 스레드로 동작하므로 여러 스레드가 동시에 DB에 쓰려 한다.  
Qt의 `QSqlDatabase` 커넥션은 생성한 스레드에서만 사용 가능하므로 단일 커넥션을 공유할 수 없다.

**채택한 방안: 로그 전용 쓰기 큐 (방안 B)**

```
SerialWorker ──push()──┐
TcpWorker(1) ──push()──┤
TcpWorker(2) ──push()──┼──→ PollLogQueue ──→ LogWriterThread ──→ device.db
TcpWorker(N) ──push()──┘   (QMutex+QQueue)    (전용 커넥션)
```

- Worker는 큐에 `PollLogEntry`를 push하고 즉시 반환 (DB I/O 블로킹 없음)
- `LogWriterThread`만 DB 커넥션을 소유하므로 Qt 스레드 규칙 준수
- 0.5초마다 큐를 소비해 단일 트랜잭션으로 일괄 INSERT → DB 쓰기 효율 최대화

---

## 데이터 모델

### `PollLogEntry` (`data_collection/model/DeviceModels.h`)

```cpp
struct PollLogEntry {
    qint64  id            = -1;
    int     deviceId      = -1;
    QString deviceName;
    QString timestamp;        // "yyyy-MM-dd HH:mm:ss"
    bool    success       = false;
    int     durationMs    = -1;
    int     registerCount = 0;
    QString message;          // "Read N registers OK" | 에러 문자열
};
```

---

## DB 스키마

```sql
CREATE TABLE IF NOT EXISTS device_poll_log (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id      INTEGER NOT NULL,
    device_name    TEXT    NOT NULL,
    timestamp      TEXT    NOT NULL,
    success        INTEGER NOT NULL DEFAULT 0,
    duration_ms    INTEGER NOT NULL DEFAULT -1,
    register_count INTEGER NOT NULL DEFAULT 0,
    message        TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_poll_log_device
ON device_poll_log(device_id, id);
```

인덱스 `(device_id, id)` — 장비별 조회와 오래된 항목 DELETE 모두 O(log n) 보장.

---

## 구성 요소

### 1. `PollLogQueue` (`data_collection/polling/PollLogQueue.h`)

헤더 온리 클래스. Worker 스레드들이 push, LogWriterThread가 popAll 호출.

```
push()   : QMutexLocker → QQueue::enqueue   (비블로킹)
popAll() : QMutexLocker → 전체 drain → QList 반환
```

### 2. `LogWriterThread` (`data_collection/polling/LogWriterThread.h/.cpp`)

- 생성자: `dbPath`, `PollLogQueue*` 수신
- `run()`: 자체 스레드에서 전용 커넥션 open → 루프 → close
- 커넥션 이름: `"log_writer_{threadId}"` (고유 보장)
- 루프: `msleep(500)` → `popAll()` → 비어있으면 continue → `writeEntries()`
- 종료(`stop()`): `m_running=false` → `wait()` → run() 내 잔여 큐 플러시 후 반환

#### `writeEntries()` 흐름

```
db.transaction()
for each entry:
    INSERT INTO device_poll_log (...)
    DELETE FROM device_poll_log
        WHERE device_id = :did
          AND id NOT IN (SELECT id ... ORDER BY id DESC LIMIT 100)
db.commit()
```

트랜잭션으로 묶으므로 다수의 항목이 큐에 쌓여 있어도 1번의 커밋으로 처리.

### 3. `PollingManager` (변경)

- 생성자에 `const QString &dbPath` 추가
- `start()`: `PollLogQueue` → `LogWriterThread` 순서로 생성 후 Worker 생성 시 큐 포인터 전달
- `stop()`: Worker 전부 종료 → **그 후** LogWriterThread 종료 (잔여 push 보장)

```
stop 순서:
  1. SerialWorker::stop()
  2. TcpWorker::stop() × N
  3. LogWriterThread::stop()  ← 마지막 (잔여 로그 플러시)
```

### 4. Worker 변경 (`SerialWorker`, `TcpWorker`)

생성자에 `PollLogQueue*` 추가. 폴링 사이클 완료(`updateStatus` 직후) 로그 push:

```cpp
if (m_logQueue) {
    Model::PollLogEntry entry;
    entry.deviceId      = device.id;
    entry.deviceName    = device.name;
    entry.timestamp     = QDateTime::fromMSecsSinceEpoch(pollStart)
                              .toString("yyyy-MM-dd HH:mm:ss");
    entry.success       = allOk;
    entry.durationMs    = status.lastPollDurationMs;
    entry.registerCount = device.registers.size();
    entry.message       = allOk
        ? QStringLiteral("Read %1 registers OK").arg(registerCount)
        : lastError;
    m_logQueue->push(entry);
}
```

---

## API

### `GET /api/devices/poll-log`

**쿼리 파라미터**

| 파라미터 | 타입 | 기본값 | 설명 |
|----------|------|--------|------|
| `deviceId` | int | -1 (전체) | 특정 장비만 조회 |
| `limit` | int | 200 | 최대 반환 건수 |

**응답 예시**

```json
[
  {
    "id": 1042,
    "deviceId": 3,
    "deviceName": "냉동기1",
    "timestamp": "2026-06-23 10:24:31",
    "success": true,
    "durationMs": 42,
    "registerCount": 24,
    "message": "Read 24 registers OK"
  },
  {
    "id": 1039,
    "deviceId": 5,
    "deviceName": "스포란제어기",
    "timestamp": "2026-06-23 10:24:28",
    "success": false,
    "durationMs": -1,
    "registerCount": 8,
    "message": "CRC Error"
  }
]
```

결과는 `id DESC` 정렬 (최신 순).

---

## 변경 파일 목록

| 파일 | 변경 내용 |
|------|-----------|
| `data_collection/model/DeviceModels.h` | `PollLogEntry` 구조체 추가 |
| `data_collection/database/DeviceDatabase.h` | `fetchPollLog()` 선언 |
| `data_collection/database/DeviceDatabase.cpp` | 테이블+인덱스 스키마, `fetchPollLog()` 구현 |
| `data_collection/polling/PollLogQueue.h` | 신규 — 스레드 안전 큐 |
| `data_collection/polling/LogWriterThread.h` | 신규 — 전용 Writer 스레드 헤더 |
| `data_collection/polling/LogWriterThread.cpp` | 신규 — Writer 스레드 구현 |
| `data_collection/polling/PollingManager.h` | `dbPath` 파라미터, Queue/Writer 멤버 |
| `data_collection/polling/PollingManager.cpp` | Queue/Writer 생성·종료, Worker에 큐 전달 |
| `data_collection/polling/SerialWorker.h` | `PollLogQueue*` 멤버 추가 |
| `data_collection/polling/SerialWorker.cpp` | 생성자 + 폴링 완료 후 push |
| `data_collection/polling/TcpWorker.h` | `PollLogQueue*` 멤버 추가 |
| `data_collection/polling/TcpWorker.cpp` | 생성자 + 폴링 완료 후 push |
| `api/ApiServer.h` | `handleGetDevicePollLog()` 선언 |
| `api/ApiServer.cpp` | 라우트 등록 + 핸들러 구현 |
| `main.cpp` | `PollingManager`에 `dbFilePath` 전달 |
