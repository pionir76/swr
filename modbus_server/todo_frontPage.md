# Modbus TCP Server — 프론트 상태 페이지 구현 TODO

## 목적

외부 Modbus TCP 마스터의 접속 현황과 서버 통계를 프론트엔드에서 실시간으로 확인할 수 있는 상태 페이지 구현.

---

## 1. 백엔드 — 클라이언트 추적 구조 개선

### 1-1. `ClientInfo` 구조체 추가 (`ModbusTcpServer.h` 또는 별도 헤더)

```cpp
struct ClientInfo {
    QString   ip;
    quint16   port;
    QDateTime connectedAt;
    QDateTime lastActivityAt;
    int       totalRequests  = 0;  // FC 요청 수 (read + write)
    int       writeRequests  = 0;  // FC05/FC06/FC0F/FC10 요청 수
};
```

### 1-2. `ModbusServerImpl` 내부 저장소 변경

```cpp
// 변경 전
QSet<QTcpSocket *> m_clients;

// 변경 후
QMap<QTcpSocket *, ClientInfo> m_clients;
```

### 1-3. `acceptNewConnection()` 에서 ClientInfo 초기화

```cpp
bool acceptNewConnection(QTcpSocket *newClient) override {
    ClientInfo info;
    info.ip            = newClient->peerAddress().toString();
    info.port          = newClient->peerPort();
    info.connectedAt   = QDateTime::currentDateTime();
    info.lastActivityAt = info.connectedAt;
    m_clients.insert(newClient, info);
    emit clientConnected(info.ip, info.port);
    return true;
}
```

### 1-4. `onDataWritten()` 에서 활동 시각 및 카운터 갱신

- `onDataWritten()`이 발생할 때 어느 소켓이 요청했는지 특정이 어려움
- 해결책: `QModbusTcpServer::processRequest()` 오버라이드로 요청 소켓 특정 가능
  - `processRequest()` 는 각 클라이언트 요청마다 호출됨
  - `sender()` 또는 private 멤버 접근 방식 검토 필요

### 1-5. 서버 통계 (`ServerStats`) 추가

```cpp
struct ServerStats {
    QDateTime startedAt;          // 서버 시작 시각
    int       totalConnections = 0;  // 누적 접속 횟수 (해제 포함)
    int       totalWriteRequests = 0;
    int       errorCount = 0;
};
```

---

## 2. 백엔드 — API 엔드포인트 추가 (`ApiServer.cpp`)

### 2-1. `GET /api/modbus-server/status`

응답 JSON:

```json
{
  "running": true,
  "port": 502,
  "slaveId": 1,
  "startedAt": "2026-07-10T09:00:00",
  "uptimeSeconds": 3600,
  "currentConnections": 2,
  "totalConnections": 15,
  "totalWriteRequests": 42,
  "errorCount": 0,
  "clients": [
    {
      "ip": "192.168.0.10",
      "port": 54321,
      "connectedAt": "2026-07-10T09:30:00",
      "lastActivityAt": "2026-07-10T10:00:00",
      "totalRequests": 120,
      "writeRequests": 5
    }
  ]
}
```

### 2-2. `ModbusTcpServer` 에 API 연결을 위한 메서드 추가

```cpp
// ModbusTcpServer.h 에 추가
QList<ClientInfo> connectedClientInfos() const;
ServerStats       serverStats() const;
```

### 2-3. `ApiServer` 에 `ModbusTcpServer*` 참조 전달

- 현재 `ApiServer`는 `ModbusTcpServer`를 참조하지 않음
- `ApiServer` 생성자에 `ModbusTcpServer*` 파라미터 추가 (optional, nullptr 허용)
- 또는 `ApiServer::setModbusServer(ModbusTcpServer*)` setter 방식

---

## 3. 프론트엔드 — 상태 페이지 구성

### 3-1. 서버 기본 정보 카드

| 항목 | 값 |
|---|---|
| 서버 상태 | Running / Stopped (색상 표시) |
| 포트 | 502 |
| Slave ID | 1 |
| 가동 시간 | 1시간 23분 |
| 시작 시각 | 2026-07-10 09:00:00 |

### 3-2. 접속 현황 테이블 (실시간 갱신)

| IP | 포트 | 접속 시각 | 마지막 활동 | 요청 수 | 쓰기 수 |
|---|---|---|---|---|---|
| 192.168.0.10 | 54321 | 09:30:00 | 10:00:00 | 120 | 5 |

### 3-3. 누적 통계 카드

- 총 접속 횟수 (가동 이후 누적)
- 총 쓰기 요청 횟수
- 에러 횟수

### 3-4. 폴링 주기

- 상태 페이지 진입 시 `GET /api/modbus-server/status` 를 3~5초 간격으로 폴링
- 또는 WebSocket/SSE 방식으로 push 방식 전환 (추후 검토)

---

## 4. 구현 순서 (권장)

1. `ClientInfo`, `ServerStats` 구조체 정의
2. `ModbusServerImpl` 내부를 `QMap<QTcpSocket*, ClientInfo>` 로 전환
3. `processRequest()` 오버라이드로 요청 소켓 특정 방법 검토
4. `ModbusTcpServer` 공개 API 추가 (`connectedClientInfos()`, `serverStats()`)
5. `ApiServer` 에 `ModbusTcpServer*` 연결 + 엔드포인트 추가
6. 프론트 상태 페이지 구현

---

## 5. 미결 사항 / 검토 필요

- `processRequest()` 오버라이드 시 어느 클라이언트 소켓인지 특정하는 방법 확인
  - Qt SerialBus 내부 구현상 `sender()` 로 소켓 포인터 얻을 수 있는지 확인 필요
- `ModbusTcpServer` 통계를 메모리에만 보관할지, 재시작 후에도 유지할지 결정
  - 현재는 메모리 보관 (재시작 시 초기화) 방향으로 충분
- DB 컬럼 rename 미완 사항 (`address` → `local_address`, `unified_register_id` → `unified_address`)
  - 기존 DB와의 호환성 때문에 보류 중 — 스키마 마이그레이션 시 함께 처리
