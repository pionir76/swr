# SmartRoute 개발 계획 및 주요 개발 방향

## 1. 프로젝트 개요

SmartRoute는 NXP i.MX6ULL 기반 임베디드 통신 변환 디바이스입니다. Qt 기반 애플리케이션으로 동작하며, Modbus RTU/TCP를 통해 여러 범용 장비 데이터를 수집하고 내부 레지스터 테이블에 저장합니다.

웹 서버는 lighttpd가 정적 HTML/CSS/JS를 제공하고, 웹 UI에서 발생하는 API 요청은 Qt App 내부의 QHttpServer가 처리합니다. 모든 설정 및 장비 정보는 SQLite DB에 저장되며, Qt App이 SQLite DB를 직접 조회/갱신하여 API에 응답합니다.

또한 Modbus TCP 서버를 통해 외부 장치가 내부 레지스터 데이터를 읽어갈 수 있도록 합니다.

개발은 **1차 (60일)** 와 **2차 (60일)** 로 나누어 진행한다.

---

## 2. 개발에 필요한 핵심 항목

### 2.1 플랫폼 및 개발 환경

- NXP i.MX6ULL 임베디드 Linux 플랫폼
- Qt 프레임워크 (QCoreApplication 헤드리스, QSerialPort, QTcpSocket, QSqlDatabase, QHttpServer)
- 내부 웹서버 : lighttpd (정적 파일 서빙)
- API 서버 : QHttpServer (Qt App 내장)
- 웹 개발 언어 : HTML / CSS / JavaScript
- 데이터베이스 : SQLite (QSqlDatabase)
- 실제 컴파일은 Qt Creator 환경에서 진행하므로 본 환경에서는 컴파일 이슈에 대응하지 않는다.

### 2.2 통신 프로토콜

| 프로토콜 | 1차 | 2차 |
|---|---|---|
| Modbus RTU over RS485 | ✔ | |
| Modbus TCP over Ethernet | ✔ | |
| Modbus ASCII over RS485 | ✔ | |
| PCLink ASCII over RS485 | ✔ | |
| PCLink+SUM over TCP | | ✔ |

- 장비별 포트, IP, 보레이트, 타임아웃, 슬레이브 ID 등의 통신 설정
- Serial 장비는 단일 `SerialWorker` 스레드에서 순차 폴링 (RS485 버스 특성)
- TCP 장비는 장비별 독립 `TcpWorker` 스레드로 병렬 폴링

### 2.3 데이터 모델 및 저장 구조

**1차 DB 스키마 (필수)**

- 사용자 테이블 : 사용자 ID, 사용자명, 비밀번호 해시, 권한
- 장비 테이블 : 장비 ID, 장비명, 연결 방식(RTU/TCP), IP, 포트, 보레이트, 슬레이브 ID, 타임아웃, 폴링 주기, 재시도 횟수
- 레지스터 테이블 : 장비 ID, 레지스터 주소, 타입, 설명, 단위, 스케일, 최소/최대값, 읽기/쓰기 여부

**2차 DB 스키마 (추가)**

- 트렌드 테이블 : 레지스터 ID, 타임스탬프, 기록값
- HMI 레이아웃 테이블 : 사용자 정의 화면 매핑 정보

**메모리 내 실시간 레지스터 테이블 (RegisterTable) — 1차부터 유지**

- 수집된 최신 값, 스케일 적용값, 유효성 상태, 마지막 갱신 시각

**메모리 내 장비 목록 캐시 (DeviceList) — 1차부터 유지**

- DB에서 로드한 DeviceInfo를 메모리에 캐시, 런타임 폴링 상태 갱신
- 레지스터 쓰기 요청(WriteRequest) 큐 관리

### 2.4 웹 서버 및 UI

- lighttpd : 정적 HTML/CSS/JS 파일 서빙, `/api/*` 경로는 Qt App(QHttpServer)으로 프록시
- QHttpServer (Qt App 내장) : 웹 UI API 요청 처리

**1차 API**
- 로그인 / 로그아웃 / 세션 확인
- 장비 등록/조회/수정/삭제
- 레지스터 테이블 조회/등록/삭제
- 레지스터 값 쓰기
- 실시간 레지스터 값 조회
- 폴링 시작/중지/상태 확인
- 시스템 설정 조회/수정 (네트워크, RS485, 시스템)
- 재시작 명령

**2차 API**
- 트렌드 데이터 조회
- HMI 화면 레이아웃 저장/조회

**1차 웹 UI**
- 장비 등록 및 설정 페이지
- 실시간 모니터링 대시보드

**2차 웹 UI**
- 트렌드 목록 및 그래프 뷰
- 사용자 정의 HMI 화면 빌더

### 2.5 외부 인터페이스

- Modbus TCP 서버 구현 (1차)
- 외부 Modbus 클라이언트가 내부 RegisterTable 데이터를 읽을 수 있는 인터페이스
- 보안/네트워크 접근 제어 고려

### 2.6 테스트 및 검증

- 통신 안정성 테스트 (RS485, Ethernet)
- 데이터 정확성 검증
- 웹 UI 동작 및 사용자 흐름 테스트
- 트렌드 기록/조회 기능 검증 (2차)
- Modbus TCP 서버 응답 검증
- 메모리 / CPU 사용량 모니터링

### 2.7 운영 및 유지보수

- 로깅 및 에러 처리 전략
- 설정 백업/복원 방식 (SQLite DB 파일 백업)
- 펌웨어/소프트웨어 업데이트 방법
- 장애 복구 및 재시작 정책

---

## 3. 주요 개발 항목별 개발 방향

### 3.1 시작 흐름 (Startup Flow)

프로그램 시작 시 아래 순서로 동작한다.

1. `config.json` 로드 → `AppConfig` 파싱, `SystemConfig::init()` 등록 (없으면 `factoryReset`으로 기본값 생성), 로거 초기화
2. `DeviceDatabase` : SQLite DB를 열고 장비 목록, 각 장비별 레지스터 테이블, 폴링 설정을 조회한다.
3. `DeviceList` : DB에서 읽어온 DeviceInfo 목록으로 메모리 캐시를 초기화한다.
4. `PollingManager` : DeviceList에서 Serial/TCP 장비를 분리하고 `SerialWorker`(단일 스레드, 순차 폴링) / `TcpWorker`(장비별 독립 스레드)를 시작한다. 각 Worker는 `DataCollector` → `RegisterExecutor` → `IDeviceClient`를 통해 폴링을 수행하고 `RegisterTable`을 갱신한다.
5. `ApiServer` (QHttpServer) : 지정 포트(`SR_API_PORT`, 기본 8080)에서 HTTP 수신 대기를 시작한다. 웹 UI에서 장비/레지스터 조회·등록 요청이 오면 `DeviceDatabase`를 통해 SQLite DB를 조회/갱신하고 `DeviceList`도 동기화하여 JSON으로 응답한다. 실시간 값 요청은 `RegisterTable`에서 직접 읽어 응답한다.

### 3.2 데이터 수집 엔진

- Modbus RTU/TCP/ASCII, PCLink, PCLinkSum을 추상화하는 통신 계층(`IDeviceClient`)을 설계한다.
- RS485(Serial) 장비는 `SerialWorker` 단일 스레드에서 순차 폴링, TCP 장비는 `TcpWorker` 독립 스레드로 병렬 폴링한다.
- `DataCollector`가 `RegisterExecutor`를 통해 레지스터 읽기/쓰기를 수행하며, 바이트 오더 처리 및 최대 읽기 수량(32 words) 제한을 `RegisterExecutor`에서 관리한다.
- 통신 예외, 타임아웃, 연결 끊김에 대한 복구 로직을 각 Worker에서 관리한다.
- 수집된 데이터는 `RegisterTable.updateUnifiedRegister()`로 전달하며 스케일 적용, 범위 검사를 수행한다.

### 3.3 SQLite DB 연동

- `DeviceDatabase` 클래스 하나로 모든 DB 접근을 집중한다. (`QSqlDatabase` / `QSqlQuery`)
- 장비 목록 조회, 레지스터 테이블 조회, 폴링 설정 조회를 제공한다.
- 웹 API를 통한 장비 등록/수정/삭제, 레지스터 등록/수정/삭제 시 DB 갱신도 담당한다.
- DB 갱신 후 폴링 중인 장비 목록에 즉시 반영되도록 `CollectionCoordinator`에 재로드 인터페이스를 제공한다.

### 3.4 내부 레지스터 테이블 및 상태 관리

- 실시간 레지스터/코일 값을 저장하는 `RegisterTable`을 메모리에서 관리한다.
- 값 갱신 시 타임스탬프, 유효성 상태, 스케일 적용값, 범위 검사(outOfRange)를 함께 관리한다.
- `DeviceList`는 메모리 내 장비 목록 캐시로 폴링 상태(lastPollTimestamp, consecutiveErrors 등) 갱신 및 WriteRequest 큐를 관리한다.
- `RegisterTable`, `DeviceList` 모두 QMutex 기반 스레드 안전 접근을 보장한다.

### 3.5 웹 API 서버 (QHttpServer)

- `ApiServer`가 QHttpServer 라우팅 및 핸들러를 등록한다.
- lighttpd에서 `/api/*` 경로를 Qt App 포트(`SR_API_PORT`, 기본 8080)로 프록시하는 방식으로 연동한다.
- **1차 엔드포인트 (구현 완료)**
    - `POST /api/login` : 로그인 (사용자 인증, 세션 발급)
    - `POST /api/logout` : 로그아웃 (세션 무효화)
    - `GET /api/session` : 현재 세션 유효성 확인
    - `GET /api/polling/status` : 폴링 상태 조회
    - `POST /api/polling/start` : 폴링 시작
    - `POST /api/polling/stop` : 폴링 중지
    - `GET /api/devices` : 장비 목록 조회
    - `GET /api/devices/status` : 장비 폴링 상태 조회
    - `POST /api/devices` : 장비 등록
    - `PUT /api/devices/:id` : 장비 수정
    - `DELETE /api/devices/:id` : 장비 삭제
    - `GET /api/devices/:id/registers` : 장비별 레지스터 목록 조회
    - `POST /api/devices/:id/registers` : 레지스터 등록
    - `DELETE /api/devices/:id/registers` : 레지스터 삭제
    - `POST /api/devices/:id/registers/write` : 레지스터 값 쓰기
    - `GET /api/registers/realtime` : 실시간 레지스터 값 조회
    - `GET /api/users` : 사용자 목록 조회
    - `POST /api/users` : 사용자 추가
    - `DELETE /api/users/:username` : 사용자 삭제
    - `GET /api/config` : 시스템 설정 조회
    - `PUT /api/config/network` : 네트워크 설정 수정
    - `PUT /api/config/serial` : RS485 설정 수정
    - `PUT /api/config/system` : 시스템 설정 수정
    - `POST /api/restart` : 앱 재시작 명령
- **2차 엔드포인트**
    - `GET /api/trend/:id` : 트렌드 데이터 조회
    - `GET /api/hmi/layout` : HMI 화면 레이아웃 조회
    - `POST /api/hmi/layout` : HMI 화면 레이아웃 저장

### 3.6 Modbus TCP 서버 (1차)

- 내부 `RegisterTable` 값을 외부 클라이언트가 읽도록 Modbus TCP 서버 모듈을 구현한다.
- 필요한 레지스터 범위와 접근 정책을 정의한다.
- 내부 데이터 모델과 Modbus TCP 서버 간 매핑을 일치시킨다.
- 외부 요청에 대한 에러 응답 및 제한 처리를 추가한다.

### 3.7 기록 및 트렌드 데이터 관리 (2차)

- 사용자가 선택한 레지스터 항목을 SQLite 트렌드 테이블에 기록한다.
- 저장 주기, 보존 기간, 순환 저장 전략을 설계한다.
- 기록된 트렌드 데이터를 조회하는 API를 `ApiServer`에서 제공한다.

### 3.8 설정/관리 및 사용자 정의 화면 (2차)

- 사용자 정의 HMI 화면 빌더는 레지스터 데이터를 컨트롤/위젯에 매핑하는 구조로 설계한다.
- 저장된 화면 레이아웃과 매핑 정보는 재시작 후에도 유지되어야 한다.

---

## 4. 개발 일정

### 1차 개발 — 60일

**목표 : 핵심 수집 기능 + 기본 웹 관리 UI 완성**

| # | 항목 |
|---|---|
| 1 | SQLite DB 스키마 설계 (사용자/장비/레지스터 테이블) 및 `DeviceDatabase` 구현 |
| 2 | Modbus RTU / TCP 통신 모듈 및 폴링 엔진 (`CollectionCoordinator`, `PollingScheduler`) |
| 3 | 내부 레지스터 테이블 (`RegisterTable`) 및 실시간 수집 루프 |
| 4 | `QHttpServer` 기반 `ApiServer` 구현 및 lighttpd 프록시 연동 |
| 5 | 로그인/로그아웃/세션 API 및 Web UI 인증 처리 |
| 6 | 장비 등록/조회/수정/삭제 Web UI |
| 7 | 레지스터 등록/조회 Web UI |
| 8 | 실시간 모니터링 대시보드 |
| 9 | Modbus TCP 서버 (외부 클라이언트 읽기) |
| 10 | 통합 테스트 및 안정화 |

### 2차 개발 — 60일

**목표 : 트렌드/HMI/ASCII 확장 기능 완성**

| # | 항목 |
|---|---|
| 1 | Modbus ASCII 통신 모듈 추가 |
| 2 | SQLite 트렌드 테이블 설계 및 기록 엔진 |
| 3 | 트렌드 데이터 조회 API 및 그래프 Web UI |
| 4 | HMI 레이아웃 테이블 설계 |
| 5 | 사용자 정의 HMI 화면 빌더 Web UI |
| 6 | HMI 저장/로드 API |
| 7 | 통합 테스트 및 최종 안정화 |

---

## 5. 개발 원칙

- 모듈화: 통신, DB 접근, 메모리 상태, 웹 API, 외부 서버를 분리
- 명확한 인터페이스: 각 모듈 간 데이터 교환 인터페이스를 정의
- 오류 내성: 통신 장애 및 DB 오류에 대한 안전한 복구 처리
- 확장성: 장비 추가, 프로토콜 확장, 새로운 UI 항목 추가가 용이하도록 설계

## 6. 향후 고려 사항

- 보안: 웹 UI 접근 제어, Modbus TCP 접근 제한, API 인증
- 펌웨어 업데이트: OTA 또는 수동 업그레이드 방식
- 성능: 실시간 처리 지연 최소화, 메모리/CPU 최적화
- 확장: OPC UA, MQTT, 추가 프로토콜 또는 외부 시스템 연동
