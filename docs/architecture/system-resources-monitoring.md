# SmartRoute 시스템 리소스 모니터링

## 1. 목적

SmartRoute 웹 UI의 유지보수/대시보드 화면에서 CPU, 메모리, 디스크, 네트워크 등 시스템 리소스 사용량을 도넛 그래프로 시각화한다.

---

## 2. 구현 파일

| 파일 | 역할 |
|---|---|
| `utils/SystemMonitor.h` | 리소스 구조체 및 클래스 선언 |
| `utils/SystemMonitor.cpp` | 샘플링 및 파싱 구현 |
| `api/ApiServer.h/.cpp` | `GET /api/system/resources` 엔드포인트 |

---

## 3. 수집 항목 및 데이터 소스

| 항목 | Linux 소스 | 비고 |
|---|---|---|
| CPU 사용률 % | `/proc/stat` | 두 샘플 차분으로 계산 |
| 로드 에버리지 (1/5/15분) | `/proc/loadavg` | |
| CPU 온도 | `/sys/class/thermal/thermal_zone0/temp` | 밀리도 단위 → ÷1000 = °C |
| 메모리 사용률 % | `/proc/meminfo` | MemTotal - MemAvailable |
| 스왑 사용률 % | `/proc/meminfo` | SwapTotal - SwapFree |
| 디스크 사용률 % | `statvfs()` | `/`, `/var` 마운트 포인트 |
| 네트워크 rx/tx | `/proc/net/dev` | eth0, eth1 |
| 시스템 업타임 | `/proc/uptime` | 초 단위 |

---

## 4. 설계 — 서버 사이드 캐싱

프론트가 API를 요청할 때마다 실시간으로 측정하면 다음 문제가 발생한다.

- CPU 사용률은 두 시점의 차분이 필요하므로 요청당 최소 1초 대기 발생
- 동시 요청 시 샘플이 겹쳐 수치 왜곡 가능

해결 방법:

```
SystemMonitor (백그라운드 QTimer, 3초 주기)
    → /proc/stat 등 읽기 + 계산
    → 결과를 m_cache에 저장 (QMutex 보호)

GET /api/system/resources
    → m_cache 즉시 반환 (I/O 없음)
```

프론트 권장 폴링 주기: **5~10초** (도넛 그래프는 실시간성 불필요)

---

## 5. 클래스 구조

### 구조체

```cpp
struct DiskStat {
    QString mount;
    qint64  totalMb;
    qint64  usedMb;
    double  usagePercent;
};

struct NetStat {
    QString iface;
    qint64  rxBytes;
    qint64  txBytes;
};

struct SystemResources {
    double  cpuUsagePercent;
    double  loadAvg1, loadAvg5, loadAvg15;
    double  cpuTempCelsius;

    qint64  memTotalKb, memUsedKb;
    double  memUsagePercent;
    qint64  swapTotalKb, swapUsedKb;
    double  swapUsagePercent;

    QList<DiskStat> disks;
    QList<NetStat>  network;

    qint64    uptimeSeconds;
    QDateTime cachedAt;
};
```

### SystemMonitor 클래스

```cpp
class SystemMonitor : public QObject {
    explicit SystemMonitor(
        const QStringList &diskMounts,   // 감시할 마운트 포인트
        const QStringList &netIfaces,    // 감시할 네트워크 인터페이스
        int intervalSeconds = 3,
        QObject *parent = nullptr);

    SystemResources resources() const;   // 캐시된 값 즉시 반환
};
```

ApiServer 생성자에서 소유 객체로 생성 (parent = ApiServer):

```cpp
m_systemMonitor = new Util::SystemMonitor(
    {"/", "/var"},
    {"eth0", "eth1"},
    3, this);
```

---

## 6. API

### `GET /api/system/resources`

**권한:** 인증된 사용자 (로그인 필요)

**응답 예시:**

```json
{
  "cpu": {
    "usagePercent": 12.4,
    "loadAvg1": 0.32,
    "loadAvg5": 0.18,
    "loadAvg15": 0.10,
    "tempCelsius": 47.2
  },
  "memory": {
    "totalKb": 524288,
    "usedKb": 312400,
    "usagePercent": 59.6
  },
  "swap": {
    "totalKb": 0,
    "usedKb": 0,
    "usagePercent": 0.0
  },
  "disk": [
    {
      "mount": "/",
      "totalMb": 3800,
      "usedMb": 1240,
      "usagePercent": 32.6
    },
    {
      "mount": "/var",
      "totalMb": 1024,
      "usedMb": 430,
      "usagePercent": 42.0
    }
  ],
  "network": {
    "eth0": { "rxBytes": 12345678, "txBytes": 3456789 },
    "eth1": { "rxBytes": 0,        "txBytes": 0 }
  },
  "uptimeSeconds": 86400,
  "cachedAt": "2026-06-26T10:30:00"
}
```

---

## 7. CPU 사용률 계산

`/proc/stat` 첫 번째 라인 형식:

```
cpu  <user> <nice> <system> <idle> <iowait> <irq> <softirq> <steal> ...
```

계산식:

```
total = user + nice + system + idle + iowait + irq + softirq + steal
busy  = user + nice + system + irq + softirq + steal   (idle, iowait 제외)

CPU% = (busyCur - busyPrev) / (totalCur - totalPrev) × 100
```

- 첫 번째 샘플에서는 `prevCpu`만 저장, 사용률은 0으로 초기화
- 이후 3초마다 차분 계산 후 캐시 갱신

---

## 8. 디스크 마운트 포인트

현재 감시 대상:

| 마운트 | 용도 |
|---|---|
| `/` | 루트 파일시스템 (OS, 바이너리) |
| `/var` | DB 파일, 로그 DB (`/var/lib/swr/`, `/var/log/swr/`) |

추가가 필요한 경우 ApiServer 생성자의 `diskMounts` 목록에 추가하면 된다.

---

## 9. 네트워크 인터페이스

현재 감시 대상: `eth0` (현장), `eth1` (서비스)

`/proc/net/dev` 파싱:

```
 eth0: <rxBytes> <rxPkts> ... <txBytes> <txPkts> ...
```

- 컬럼 0 = rx bytes, 컬럼 8 = tx bytes (헤더 2줄 스킵)
- 값은 누적 카운터 (재부팅 시 초기화)
- 속도(bps) 계산이 필요하면 클라이언트 측에서 이전 값과 차분 후 폴링 주기로 나눔

---

## 10. 향후 개선 사항

- 네트워크 tx/rx 속도 (bps) 서버 사이드 계산 및 제공
- 복수 thermal zone 지원 (`thermal_zone0` ~ `thermal_zoneN`)
- 샘플링 주기 CMake 옵션으로 분리 (`SR_MONITOR_INTERVAL`)
- 프론트 WebSocket 또는 SSE로 push 방식 전환 (폴링 제거)
