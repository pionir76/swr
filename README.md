# SmartRoute

**임베디드 통신 변환 디바이스** — 다수의 산업용 장비 데이터를 Modbus RTU/TCP로 수집하고 웹 UI 및 Modbus TCP 서버로 제공합니다.

---

## 개요

SmartRoute는 현장의 다양한 범용 장비(PLC, 인버터, 센서 등)에 RS485 또는 Ethernet으로 연결하여 레지스터 데이터를 주기적으로 폴링하고, 이를 내부 레지스터 테이블에 저장합니다. 사용자는 웹 브라우저를 통해 실시간 데이터를 모니터링하고 장비 설정을 관리할 수 있으며, 외부 Modbus TCP 클라이언트가 내부 데이터를 직접 읽어갈 수도 있습니다.

```
현장 장비 (Modbus RTU/TCP, PCLink)
        │
        │  RS485 / Ethernet
        ▼
  ┌─────────────┐
  │  SmartRoute │  NXP i.MX6ULL · Qt6 · Linux
  │             │
  │ PollingMgr  │──▶ RegisterTable (실시간 캐시)
  │ DeviceDB    │──▶ SQLite
  │ ApiServer   │──▶ HTTP :8080 (REST API)
  │ ModbusTCP   │──▶ Modbus TCP Server :502
  └─────────────┘
        │
        │  lighttpd :80 (정적 파일 + API 프록시)
        ▼
   웹 브라우저 (대시보드 / 설정 / 유지보수)
```

---

## 하드웨어 사양

| 항목 | 내용 |
|---|---|
| CPU | NXP i.MX6ULL (ARM Cortex-A7) |
| 메모리 | 512 MB DRAM |
| 스토리지 | eMMC |
| OS | Embedded Linux |
| 통신 포트 | RS485 × 1, Ethernet × 2 (내부 스위칭 허브) |
| 기본 IP | eth0 `192.168.0.150` / eth1 `192.168.0.151` |

---

## 기술 스택

| 영역 | 기술 |
|---|---|
| 애플리케이션 | C++17, Qt6 (QCoreApplication, QHttpServer, QSerialPort, QSqlDatabase) |
| 웹 서버 | lighttpd (정적 파일), QHttpServer (REST API) |
| 데이터베이스 | SQLite (장치/레지스터/사용자/로그) |
| 빌드 | CMake 3.16+ |
| 웹 UI | HTML / CSS / JavaScript (Vanilla) |

---

## 지원 통신 프로토콜

| 프로토콜 | 인터페이스 |
|---|---|
| Modbus RTU | RS485 |
| Modbus ASCII | RS485 |
| Modbus TCP | Ethernet |
| PCLink ASCII | RS485 |
| PCLink+SUM | TCP |

---

## 주요 기능

### 데이터 수집
- 장비별 폴링 주기, 타임아웃, 재시도 횟수 설정
- RS485 장비: 단일 `SerialWorker` 스레드에서 순차 폴링 (버스 충돌 방지)
- TCP 장비: 장비별 독립 `TcpWorker` 스레드로 병렬 폴링
- 바이트 오더 처리 (ABCD / CDAB / BADC / DCBA)
- 수집 데이터 스케일 적용 및 범위 검사

### 웹 대시보드
- 실시간 레지스터 값 모니터링
- 장비별 폴링 상태 (정상 / 통신 오류 / 타임아웃)
- 시스템 요약 (장비 수, 레지스터 수, 사용자 수, 로그 수)
- 시스템 리소스 모니터링 (CPU / 메모리 / 디스크 / 네트워크)

### 장비 및 레지스터 관리
- 장비 등록 / 수정 / 삭제 (CRUD)
- 레지스터 등록 / 수정 / 삭제
- 통합 레지스터 주소 자동 매핑

### Modbus TCP 서버
- 외부 Modbus TCP 클라이언트가 내부 레지스터 데이터를 읽어가는 인터페이스
- 기본 포트 502

### 사용자 관리
- 역할 기반 접근 제어 (admin / manager / user)
- 세션 기반 인증, 로그인 실패 잠금, 자동 로그아웃
- admin 계정 삭제 불가 (id=0 고정)

### 백업 / 복원
- 전체 설정을 ZIP 파일로 다운로드 (장비, 레지스터, 사용자, 시스템 설정 포함)
- ZIP 업로드 → 무결성 검증(SHA-256) → 미리보기 → 선택적 복원
- 복원 항목 선택: 시스템 설정, 장비, 레지스터, 사용자, 네트워크 (기본 OFF)

### 공장 초기화
- 장비/레지스터/사용자 데이터 전체 삭제
- 시스템 설정 출고 기본값 복원 (네트워크 포함)
- 로그 전체 삭제
- admin 비밀번호 초기화

### 시스템 모니터링
- CPU 사용률, 온도, 로드 에버리지
- 메모리 / 스왑 사용량
- 디스크 사용률 (`/`, `/var`)
- 네트워크 rx/tx (eth0, eth1)
- 시스템 업타임
- 3초 주기 백그라운드 샘플링, API는 캐시 즉시 반환

---

## REST API 목록

| Method | Endpoint | 설명 |
|---|---|---|
| POST | `/api/login` | 로그인 |
| POST | `/api/logout` | 로그아웃 |
| GET | `/api/session` | 세션 확인 |
| GET | `/api/dashboard` | 대시보드 요약 |
| GET/POST | `/api/devices` | 장비 목록 / 등록 |
| GET/PUT/DELETE | `/api/devices/<id>` | 장비 조회 / 수정 / 삭제 |
| GET/POST | `/api/devices/<id>/registers` | 레지스터 목록 / 등록 |
| PUT/DELETE | `/api/devices/<id>/registers/<addr>` | 레지스터 수정 / 삭제 |
| GET | `/api/realtime` | 실시간 레지스터 값 |
| GET/POST | `/api/polling/status` | 폴링 상태 |
| POST | `/api/polling/start` | 폴링 시작 |
| POST | `/api/polling/stop` | 폴링 중지 |
| GET/PUT | `/api/config/system` | 시스템 설정 |
| GET/PUT | `/api/config/network` | 네트워크 설정 |
| GET/PUT | `/api/config/serial` | RS485 설정 |
| GET/PUT | `/api/config/modbus-server` | Modbus TCP 서버 설정 |
| GET | `/api/system/info` | 시스템 정보 + 요약 |
| GET | `/api/system/resources` | 리소스 모니터링 |
| POST | `/api/system/restart` | 애플리케이션 재시작 |
| GET/POST/DELETE | `/api/users` | 사용자 관리 |
| GET | `/api/logs` | 시스템 로그 조회 |
| GET | `/api/maintenance/backup` | 백업 ZIP 다운로드 |
| POST | `/api/maintenance/restore/validate` | 복원 파일 검증 |
| POST | `/api/maintenance/restore/apply` | 복원 적용 |
| POST | `/api/maintenance/factory-reset` | 공장 초기화 |

---

## 빌드 설정 (CMake)

```cmake
# 주요 빌드 옵션 (cmake -D<KEY>=<VALUE>)
SR_CONFIG_FILE       # 설정 파일 경로       (기본: /etc/swr/config.json)
SR_DB_FILE           # DB 파일 경로         (기본: /var/lib/swr/smartroute.db)
SR_LOG_FILE          # 로그 DB 경로         (기본: /var/log/swr/smartroute_log.db)
SR_API_PORT          # API 서버 포트        (기본: 8080)
SR_MAX_DEVICES       # 최대 장비 수         (기본: 20)
SR_MAX_LOG_LINES     # 최대 로그 보관 건수  (기본: 1000)
SR_SCHEMA_VERSION    # DB 스키마 버전       (백업/복원 호환성 검사 키)
```

---

## 파일 구조

```
swr/
├── main.cpp
├── CMakeLists.txt
├── api/                        # REST API 서버 (QHttpServer)
│   ├── ApiServer.h/.cpp
├── config/                     # 설정 로드/저장
│   ├── AppConfig.h/.cpp
│   └── SystemConfig.h/.cpp
├── data_collection/
│   ├── comm/                   # 통신 클라이언트 (Modbus, PCLink)
│   ├── database/               # SQLite DB 접근 (DeviceDatabase)
│   ├── model/                  # 데이터 모델 (DeviceInfo, RegisterField)
│   ├── polling/                # 폴링 엔진 (SerialWorker, TcpWorker)
│   ├── processor/              # DataCollector
│   └── store/                  # 메모리 캐시 (RegisterTable, DeviceList)
├── maintenance/                # 백업 / 복원
│   ├── BackupManager.h/.cpp
│   ├── RestoreManager.h/.cpp
│   └── BackupModels.h
├── modbus_server/              # Modbus TCP 서버
│   ├── ModbusTcpServer.h/.cpp
│   └── RegisterAddressMap.h/.cpp
├── utils/                      # 공통 유틸리티
│   ├── Logger.h/.cpp           # 비동기 SQLite 로거
│   ├── SystemMonitor.h/.cpp    # 시스템 리소스 모니터링
│   └── NetworkConfigurator.h/.cpp
└── webtest/                    # 웹 UI (HTML/CSS/JS)
    ├── index.html
    └── js/app.js
```

---

## 런타임 경로

| 경로 | 내용 |
|---|---|
| `/etc/swr/config.json` | 시스템 설정 파일 |
| `/var/lib/swr/smartroute.db` | 장비/레지스터/사용자 DB |
| `/var/log/swr/smartroute_log.db` | 시스템 로그 DB |
| `/var/www/html/` | 웹 UI 정적 파일 (lighttpd) |
| `/tmp/swr_restore/` | 복원 임시 세션 |

---

## 기본 계정

| 계정 | 비밀번호 | 역할 |
|---|---|---|
| `admin` | `1234` | 관리자 (삭제 불가) |

> 최초 접속 후 반드시 비밀번호를 변경하세요.

---

## 라이선스

Private — All rights reserved.
