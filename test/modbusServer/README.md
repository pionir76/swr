# Modbus TCP Server 테스트

SmartRoute 디바이스의 Modbus TCP Server 동작을 자동으로 검증하는 테스트.

## 테스트 내용

| 단계 | 내용 |
|------|------|
| 1 | SWR REST API 로그인 → Bearer 토큰 획득 |
| 2 | API로 전체 디바이스/레지스터 목록 수집 (unifiedRegisterId, readOnly) |
| 3 | Modbus TCP 연결 확인 |
| 4 | FC3 Read Holding Registers — 전 레지스터 읽기 |
| 5 | FC6 Write Single Register — RW 레지스터 쓰기 후 복원 |

### 쓰기 검증 방식

이 디바이스는 **통신 라우터** 구조로 동작한다.

```
FC6 쓰기 수신
  → ModbusTcpServer.onDataWritten()
  → writeRequested 시그널
  → PollingManager
  → 실제 필드 장치 (RS-485 / Modbus TCP)
```

Modbus 서버의 `readData()`는 내부 버퍼가 아닌 `RegisterTable`(폴링 결과값)을 반환한다.  
따라서 **쓰기 직후 read-back은 이전 폴링값을 반환** — 이것은 버그가 아니라 정상 동작이다.  
FC6 응답에 Modbus Exception이 없으면 **명령이 필드 장치로 전달된 것으로 판정**한다.

## 실행 방법

### 의존성 설치 (최초 1회)

```bash
pip install pymodbus requests
```

### 기본 실행 (기본값: 192.168.0.150)

```bash
python3 test_modbus_server.py
```

### 옵션 지정

```bash
python3 test_modbus_server.py \
  --host 192.168.0.150 \
  --api-port 8080 \
  --mb-port 502 \
  --slave 1 \
  --user admin \
  --password 1234
```

### 옵션 목록

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--host` | `192.168.0.150` | 디바이스 IP |
| `--api-port` | `8080` | SWR REST API 포트 |
| `--mb-port` | `502` | Modbus TCP 포트 |
| `--slave` | `1` | Modbus 슬레이브 ID |
| `--user` | `admin` | API 로그인 사용자 |
| `--password` | `1234` | API 로그인 비밀번호 |

## 정상 출력 예시

```
[ 1 ] API 로그인
  ✓ 로그인 성공  user=admin  token=87a6dd8d...

[ 2 ] 레지스터 목록 수집 (API)
  · 디바이스 수: 4
  ✓ 총 레지스터 31개 수집
  ...

[ 3 ] Modbus TCP 연결
  ✓ 연결 성공: 192.168.0.150:502  slaveId=1

[ 4 ] 읽기 테스트 (FC3 Read Holding Registers)
  ✓ uid=   3  TP2KMF / reg-3  → 0  (0x0000)
  ...
  읽기 결과: 31 성공 / 0 실패

[ 5 ] 쓰기 테스트 (FC6 Write Single Register) — RW 레지스터만
  ✓ uid=   4  TP2KMF / reg-4  현재값=1  전달값=2  → 명령 전달됨
  ...
  쓰기 결과: 10 명령 전달됨 / 0 실패

[ 결과 요약 ]
  총 41 통과 / 0 실패
  ✓ 모든 테스트 통과
```

## 관련 소스

| 파일 | 설명 |
|------|------|
| `modbus_server/ModbusTcpServer.h/.cpp` | Modbus TCP Server 구현 |
| `data_collection/store/RegisterTable.h` | 폴링 결과 저장소 |
| `api/ApiServer.cpp` | REST API (`/api/devices`, `/api/devices/{id}/registers`) |
| `main.cpp` | ModbusTcpServer 생성 및 시작 (`config.modbusServer.enabled` 조건) |
