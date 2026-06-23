# SmartRoute 데이터 수집 엔진 아키텍처

## 목표

- SQLite DB에서 대상 장비 목록, 연결 방법, 읽어올 레지스터 항목 정보를 직접 조회한다.
- Modbus RTU/TCP/ASCII, PCLink, PCLinkSum 통신을 추상화하여 대상 장비 설정에 따라 통신을 시작하고, 폴링 방식으로 데이터를 읽어온다.
- 수집된 데이터를 내부 레지스터 테이블에 전달하여 실시간 상태를 유지한다.
- 웹 UI에서 발생하는 API 요청을 QHttpServer로 수신하여 SQLite DB를 조회/갱신한 뒤 JSON으로 응답한다.

## 폴더 구조

```
swr/
  main.cpp
  CMakeLists.txt
  config/                         ← 앱 설정 로더
    AppConfig.h
    AppConfig.cpp
    SystemConfig.h
    SystemConfig.cpp
  utils/                          ← 공통 유틸리티
    Logger.h
    Logger.cpp
    NetworkConfigurator.h
    NetworkConfigurator.cpp
  api/                            ← 앱 인터페이스 계층 (Web UI ↔ Qt App)
    ApiServer.h
    ApiServer.cpp
  data_collection/                ← 데이터 수집 엔진 (장비 → 내부 저장소)
    database/
      DeviceDatabase.h
      DeviceDatabase.cpp
    model/
      DeviceModels.h              (DeviceInfo, DeviceConnection, RegisterField, PollingConfig, UserInfo, WriteRequest)
      UnifiedRegister.h
    comm/
      IDeviceClient.h             ← 공통 통신 인터페이스 (프로토콜 추상화)
      DeviceClientFactory.h
      DeviceClientFactory.cpp
      RegisterExecutor.h
      RegisterExecutor.cpp
      modbus/
        ModbusRTUClient.h
        ModbusRTUClient.cpp
        ModbusTCPClient.h
        ModbusTCPClient.cpp
        ModbusASCIIClient.h
        ModbusASCIIClient.cpp
      pclink/
        PcLinkClient.h            ← Samwontech PCLink ASCII over RS485
        PcLinkClient.cpp
        PcLinkSumTCPClient.h      ← Samwontech PCLink+SUM over TCP (구현 예정)
        PcLinkSumTCPClient.cpp
    polling/
      PollingManager.h
      PollingManager.cpp
      SerialWorker.h
      SerialWorker.cpp
      TcpWorker.h
      TcpWorker.cpp
    processor/
      DataCollector.h
      DataCollector.cpp
    store/
      RegisterTable.h
      RegisterTable.cpp
      DeviceList.h
      DeviceList.cpp
```

## 구성 요소별 역할

### 0) `config/`  _(앱 설정 계층)_

- `config.json` 파일(경로: `SR_CONFIG_FILE`)을 파싱하여 `AppConfig` 구조체를 반환한다.
- 네트워크 인터페이스(eth0/eth1), RS485 설정, 시스템 설정(hostname, NTP)을 담는다.
- `SystemConfig` 싱글턴을 통해 앱 전역에서 설정값을 조회한다.
- `factoryReset()` 으로 config 파일이 없을 때 기본값 파일을 생성한다.

### 1) `api/`  _(앱 인터페이스 계층 — `data_collection/` 외부)_

- QHttpServer 기반 REST API 서버
- lighttpd가 전달하는 Web UI의 API 요청을 수신하여 처리
- `data_collection/` 내부 모듈(`DeviceDatabase`, `RegisterTable`, `DeviceList`, `PollingManager`)을 조합하여 JSON 응답 반환
- 네임스페이스: `Api`

주요 파일

- `ApiServer` : QHttpServer 라우팅 및 핸들러 등록

**구현된 엔드포인트**

| 그룹 | 메서드 | 경로 |
|---|---|---|
| 인증 | POST | `/api/login` |
| 인증 | POST | `/api/logout` |
| 인증 | GET | `/api/session` |
| 폴링 제어 | GET | `/api/polling/status` |
| 폴링 제어 | POST | `/api/polling/start` |
| 폴링 제어 | POST | `/api/polling/stop` |
| 장비 | GET | `/api/devices` |
| 장비 | GET | `/api/devices/status` |
| 장비 | POST | `/api/devices` |
| 장비 | PUT | `/api/devices/:id` |
| 장비 | DELETE | `/api/devices/:id` |
| 레지스터 | GET | `/api/devices/:id/registers` |
| 레지스터 | POST | `/api/devices/:id/registers` |
| 레지스터 | PUT | `/api/devices/:id/registers` |
| 레지스터 | DELETE | `/api/devices/:id/registers` |
| 레지스터 | POST | `/api/devices/:id/registers/write` |
| 실시간 값 | GET | `/api/registers/realtime` |
| 로그 | GET | `/api/logs` |
| 사용자 | GET | `/api/users` |
| 사용자 | POST | `/api/users` |
| 사용자 | DELETE | `/api/users/:username` |
| 시스템 설정 | GET | `/api/config` |
| 시스템 설정 | POST | `/api/config/reset` |
| 시스템 설정 | PUT | `/api/config/network` |
| 시스템 설정 | PUT | `/api/config/serial` |
| 시스템 설정 | PUT | `/api/config/system` |
| 시스템 설정 | PUT | `/api/config/modbus-server` |
| 시스템 설정 | POST | `/api/restart` |

### 2) `database/`

- SQLite DB에 직접 접근하여 장비 정보와 레지스터 항목을 조회하는 모듈
- Qt의 `QSqlDatabase` / `QSqlQuery`를 사용하여 구현
- 웹 API를 통한 장비 등록/수정/삭제 요청 시 DB 갱신도 담당
- 사용자 인증(`validateUser`) 및 사용자 CRUD 담당

주요 파일

- `DeviceDatabase` : 장비 목록, 레지스터 테이블, 사용자 정보, 폴링 설정을 조회/갱신하는 단일 DB 접근 클래스

### 3) `model/`

- DB에서 읽어온 장비 정보를 담는 도메인 모델
- 장비 연결 정보, 레지스터 정의, 폴링 주기/타임아웃 정보 등을 포함

주요 모델

- `DeviceInfo` : 장비 식별 정보, 장비명, 연결 정보, 레지스터 목록, 폴링 설정, 런타임 상태
- `DeviceConnection` : Serial / TCP 연결 방식, 프로토콜(ModbusRtu/ModbusTcp/ModbusAscii/PcLink/PcLinkSum), IP, 슬레이브 ID, 바이트 오더, 타임아웃
- `RegisterField` : 레지스터 주소, 타입(Coil/DiscreteInput/HoldingRegister/InputRegister/WordRegister/BitRegister), 스케일, 단위, 바이트 오더, 비트 레이블
- `PollingConfig` : 폴링 주기(ms), 재시도 횟수
- `UnifiedRegister` : 수집된 실시간 값, 스케일 적용 결과, 유효성 상태, 마지막 갱신 시각
- `UserInfo` : 사용자 ID/명/비밀번호 해시·솔트/권한(User/Manager/Admin)
- `WriteRequest` : 레지스터 쓰기 요청 정보 (RegisterField + rawValues/coilValues)

### 4) `comm/`

- Modbus RTU/TCP/ASCII, PCLink, PCLinkSum 통신을 추상화하여 공통 인터페이스를 제공
- 연결 설정에 따라 적절한 클라이언트를 생성

주요 파일

- `IDeviceClient` : 공통 인터페이스 (`connect()`, `disconnect()`, `readWords()`, `readBits()`, `writeWords()`, `writeBits()`)
- `modbus/ModbusRTUClient` : RS485 기반 Modbus RTU 구현 (CRC16, QSerialPort)
- `modbus/ModbusTCPClient` : Ethernet 기반 Modbus TCP 구현 (MBAP Header, QTcpSocket)
- `modbus/ModbusASCIIClient` : RS485 기반 Modbus ASCII 구현 (LRC, QSerialPort)
- `pclink/PcLinkClient` : Samwontech PCLink ASCII over RS485 구현
- `pclink/PcLinkSumTCPClient` : Samwontech PCLink+SUM over TCP (구현 예정)
- `DeviceClientFactory` : 장비 DeviceConnection 설정을 바탕으로 적합한 IDeviceClient 생성
- `RegisterExecutor` : RegisterField 기반 읽기/쓰기 실행, 바이트 오더 처리, 최대 읽기 수량 제한(64 words/coils), 배치 읽기(`readBatch`) 지원

### 5) `polling/`

- 폴링 스케줄링 엔진
- 연결 타입(Serial/TCP)별로 별도 워커 스레드를 분리하여 병렬 폴링

주요 파일

- `PollingManager` : 장비 목록에서 Serial/TCP를 분리, `SerialWorker`와 `TcpWorker`를 생성·관리
- `SerialWorker` : QThread 기반, 모든 Serial 장비를 순차 폴링 (RS485 단일 버스 특성상 순차 처리)
- `TcpWorker` : QThread 기반, 장비 1개당 독립 스레드로 TCP 폴링

### 6) `processor/`

- 실제 통신 수행과 결과 처리 흐름
- SerialWorker/TcpWorker가 내부적으로 DataCollector를 생성하여 사용

주요 파일

- `DataCollector` : DeviceInfo를 받아 `RegisterExecutor`를 통해 레지스터 값을 읽어오는 핵심 엔진, WriteRequest 큐도 처리(`flushWrites`), 연속 주소 필드를 배치로 묶어 단일 요청으로 처리(`collectAllFields` → `buildBatches`)

### 7) `store/`

- 수집된 데이터를 메모리에 보관하고 실시간 레지스터 테이블을 관리
- QMutex로 스레드 안전 보장
- 웹 API, Modbus TCP 서버 등 다른 모듈과 데이터 공유

주요 파일

- `RegisterTable` : 실시간 레지스터/코일 상태 저장소, UnifiedRegister 관리 (스케일 적용, 범위 검사 포함)
- `DeviceList` : 메모리 내 장비 목록 캐시, 런타임 상태 갱신, WriteRequest 큐 관리

### 8) `utils/`

- 공통 유틸리티
- 로깅 (파일 + QDebug 출력, 최대 라인 수 순환), 네트워크 설정 (ip 명령 래퍼)

## 프로그램 시작 순서 (Startup Flow)

```
[1] main.cpp
    config.json 로드 → AppConfig 파싱 (없으면 factoryReset으로 기본값 생성)
    SystemConfig::init(config) — 전역 설정 등록
    → Logger 초기화 (SR_LOG_FILE, 최대 SR_MAX_LOG_LINES 줄)
    → 네트워크 설정 적용 (현재 주석 처리, 필요 시 활성화)

[2] DeviceDatabase
    SQLite DB 오픈 (SR_DB_FILE)
    장비 목록 (DeviceInfo + DeviceConnection + RegisterField + PollingConfig) 조회

[3] DeviceList
    DeviceDatabase에서 읽어온 DeviceInfo 목록으로 메모리 캐시 초기화

[4] PollingManager
    DeviceList에서 Serial / TCP 장비 분리
    Serial 장비 → SerialWorker 1개 스레드 (순차 폴링)
    TCP 장비 → TcpWorker N개 스레드 (장비별 독립 폴링)
    각 Worker는 내부에서 DataCollector를 생성하여 RegisterExecutor로 통신 수행
    수집 결과 → RegisterTable.updateUnifiedRegister() 갱신

[5] ApiServer (QHttpServer)
    지정 포트(SR_API_PORT, 기본 8080)에서 HTTP 수신 대기
    Web UI에서 장비 조회/등록/수정/삭제 요청 → DeviceDatabase 조회/갱신 + DeviceList 동기화 → JSON 응답
    Web UI에서 실시간 레지스터 값 요청 → RegisterTable 조회 → JSON 응답
    폴링 제어 요청 → PollingManager.start() / stop()
    레지스터 쓰기 요청 → DeviceList.enqueueWrite() → Worker 다음 사이클에서 flushWrites() 처리

[6] 폴링 루프 (Worker Thread 반복)
    SerialWorker / TcpWorker
    → DataCollector.initialize() → DeviceClientFactory → IDeviceClient 생성
    → DataCollector.collectAllFields() → buildBatches() → RegisterExecutor.readBatch() → IDeviceClient.readWords()/readBits()
    → RegisterTable.updateUnifiedRegister() 갱신
    → DataCollector.flushWrites() → WriteRequest 큐 처리
    → DeviceList.updateStatus() (폴링 성공/실패 상태 갱신)
```

## 데이터 흐름

1. 시작 시 `DeviceDatabase`가 SQLite DB를 직접 조회하여 `DeviceInfo`(연결 정보 + 레지스터 목록 + 폴링 설정)를 반환한다.
2. `DeviceList`가 목록을 메모리에 캐시하고, `PollingManager`가 Serial/TCP 분류 후 Worker 스레드를 시작한다.
3. 각 Worker는 폴링 주기마다 `DataCollector.collectAllFields()`를 호출한다. 내부에서 연속 주소 필드를 배치로 묶어 `RegisterExecutor.readBatch()` → `IDeviceClient`로 최소 횟수의 통신을 수행한다.
4. 읽어온 데이터는 `RegisterTable.updateUnifiedRegister()`로 전달되어 스케일 적용, 범위 검사 후 실시간 상태가 갱신된다.
5. 웹 UI에서 API 요청이 오면 `ApiServer`(QHttpServer)가 수신하여 `DeviceDatabase` 또는 `RegisterTable`을 조회하고 JSON으로 응답한다.
6. 장비 등록/수정/삭제 시 `DeviceDatabase`와 `DeviceList` 양쪽을 동기화(`syncAddDevice` 등)한다.

## 지원 프로토콜

| 프로토콜 | 연결 방식 | 구현 상태 |
|---|---|---|
| Modbus RTU | RS485 (Serial) | 완료 |
| Modbus ASCII | RS485 (Serial) | 완료 |
| Modbus TCP | Ethernet (TCP) | 완료 |
| PCLink ASCII | RS485 (Serial) | 완료 |
| PCLink+SUM TCP | Ethernet (TCP) | 구현 예정 |
