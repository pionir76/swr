# SmartRoute REST API 명세

> 최종 수정: 2026-10-14 (pollMode/effectiveIntervalMs 추가, HMI 태그 API 추가)  
> Base URL: `http://<device-ip>:8080`  
> 응답 형식: `application/json` (바이너리 응답 제외)

---

## 공통

### 인증

`Authorization: Bearer <token>` 헤더를 모든 보호된 엔드포인트에 포함해야 합니다.  
토큰은 `POST /api/login` 성공 시 발급됩니다.

### 권한 등급

| 등급 | 대상 |
|---|---|
| `Any` | 로그인한 모든 사용자 |
| `ManagerOrAbove` | Manager, Admin |
| `AdminOnly` | Admin 전용 |
| 없음 | 인증 불필요 |

### 공통 에러 응답

```json
{ "error": "에러 메시지" }
```

| 상태 코드 | 의미 |
|---|---|
| `400 Bad Request` | 잘못된 요청 파라미터 |
| `401 Unauthorized` | 미인증 / 세션 만료 |
| `403 Forbidden` | 권한 부족 / 보호된 계정 |
| `404 Not Found` | 리소스 없음 |
| `409 Conflict` | 상태 충돌 (폴링 중 수정, 이미 녹화 중 등) |
| `422 Unprocessable Entity` | 복원 파일 무결성 검증 실패 |
| `500 Internal Server Error` | 서버 내부 오류 |

---

## 1. 인증 / 세션

### POST /api/login
인증 불필요.

**Request Body**
```json
{ "username": "admin", "password": "1234" }
```

**Response 200**
```json
{ "token": "uuid-token-string" }
```

**에러 응답**

| 상태 | reason 필드 | 설명 |
|---|---|---|
| 401 | — | 비밀번호 불일치 |
| 403 | `"disabled"` | 계정 비활성화 |
| 403 | `"locked"` | 계정 잠금 (최대 시도 초과) |

---

### POST /api/logout
인증 불필요 (만료 토큰으로도 로그아웃 가능).

**Response 200** (body 없음)

---

### GET /api/session
권한: `Any`

**Response 200**
```json
{ "valid": true }
```

---

## 2. 대시보드

### GET /api/dashboard
권한: `Any`

**Response 200**
```json
{
  "polling": { "running": true },
  "kpi": {
    "totalDevices": 3,
    "okDevices": 2,
    "errorDevices": 1,
    "unknownDevices": 0
  },
  "devices": [
    {
      "id": 1,
      "name": "PLC-1",
      "deviceCode": "DEV01",
      "connType": "serial",
      "protocol": "modbus_rtu",
      "state": "ok",
      "lastPollDurationMs": 45,
      "consecutiveErrors": 0,
      "lastError": ""
    }
  ],
  "realtimeRegisters": [ /* 최대 20개, GET /api/registers/realtime 항목과 동일 */ ],
  "alerts": [
    { "id": 10, "timestamp": "2026-10-13T12:00:00", "level": "WARN", "message": "..." }
  ],
  "logs": [
    { "id": 9, "timestamp": "2026-10-13T12:00:00", "level": "INFO", "message": "..." }
  ]
}
```

> `state`: `"ok"` | `"error"` | `"unknown"`

---

## 3. 폴링 제어

### GET /api/polling/status
권한: `Any`

**Response 200**
```json
{ "running": true }
```

---

### POST /api/polling/start
권한: `Any`

**Response 200**
```json
{ "running": true }
```

**에러** 409: 이미 실행 중

---

### POST /api/polling/stop
권한: `Any`

**Response 200**
```json
{ "running": false }
```

---

## 4. 장비

### GET /api/devices
권한: `Any`

**Response 200**
```json
{
  "devices": [
    {
      "id": 1,
      "deviceCode": "DEV01",
      "name": "PLC-1",
      "displayName": "PLC 1번",
      "connType": "serial",
      "protocol": "modbus_rtu",
      "ipAddress": "",
      "tcpPort": 502,
      "slaveId": 1,
      "timeoutMs": 1000,
      "intervalMs": 1000,
      "retryCount": 3,
      "pollMode": "auto",
      "byteOrder": "big"
    }
  ]
}
```

> `connType`: `"serial"` | `"tcp"`  
> `protocol`: `"modbus_rtu"` | `"modbus_tcp"` | `"modbus_ascii"` | `"pclink"` | `"pclink_sum"`  
> `byteOrder`: `"big"` | `"little"` | `"default"`  
> `pollMode`: `"auto"` | `"manual"` (기본값 `"auto"`)

---

### GET /api/devices/status
권한: `Any`

**Response 200**
```json
{
  "devices": [
    {
      "deviceId": 1,
      "deviceCode": "DEV01",
      "displayName": "PLC 1번",
      "pollMode": "auto",
      "state": "ok",
      "lastPollTimestamp": "2026-10-13T12:00:00",
      "lastPollDurationMs": 45,
      "effectiveIntervalMs": 1200,
      "consecutiveErrors": 0,
      "lastError": ""
    }
  ]
}
```

---

### POST /api/devices
권한: `ManagerOrAbove` | 폴링 중 409

**Request Body**
```json
{
  "deviceCode": "DEV01",
  "name": "PLC-1",
  "displayName": "PLC 1번",
  "connType": "serial",
  "protocol": "modbus_rtu",
  "ipAddress": "",
  "tcpPort": 502,
  "slaveId": 1,
  "timeoutMs": 1000,
  "intervalMs": 1000,
  "retryCount": 3,
  "pollMode": "auto",
  "byteOrder": "big"
}
```

> 필수: `name`, `byteOrder`(`"big"` 또는 `"little"` 필수, `"default"` 불가)  
> `protocol` 생략 시: `connType=serial` → `modbus_rtu`, `connType=tcp` → `modbus_tcp`  
> `pollMode` 생략 시 `"auto"`. `"manual"` 선택 시 `intervalMs` 사용.

**Response 201**
```json
{ "name": "PLC-1", "pollMode": "auto" }
```

---

### PUT /api/devices/{id}
권한: `ManagerOrAbove` | 폴링 중 409

**Request Body** (변경할 필드만 포함)
```json
{
  "name": "PLC-1-updated",
  "slaveId": 2,
  "pollMode": "manual",
  "intervalMs": 2000
}
```

**Response 200**
```json
{ "ok": true, "pollMode": "manual" }
```
`pollMode:"manual"` 설정 시 비현실적인 `intervalMs`(실측 소요시간보다 짧음)를 넣으면 저장은 허용하되 `"warning"` 필드 포함:
```json
{ "ok": true, "pollMode": "manual", "warning": "설정 주기(500ms)가 실측 소요시간(800ms)보다 짧습니다" }
```

**에러** 404: 장비 없음

---

### DELETE /api/devices/{id}
권한: `ManagerOrAbove` | 폴링 중 409

**Response 200** (body 없음)

---

## 5. 레지스터

### GET /api/devices/{deviceId}/registers
권한: `Any`

**Response 200**
```json
{
  "registers": [
    {
      "id": 1,
      "tagName": "TEMP_01",
      "displayName": "온도 1번",
      "localAddress": 100,
      "type": "holding_register",
      "readOnly": false,
      "length": 1,
      "unit": "℃",
      "scale": 0.1,
      "isSigned": true,
      "bitLabels": "",
      "minValue": -50.0,
      "maxValue": 150.0,
      "byteOrder": "default",
      "unifiedAddress": 1001
    }
  ]
}
```

> `type`: `"coil"` | `"discrete_input"` | `"input_register"` | `"holding_register"`  
> `byteOrder`: `"default"` | `"big"` | `"little"`

---

### POST /api/devices/{deviceId}/registers
권한: `ManagerOrAbove` | 폴링 중 409

**Request Body**
```json
{
  "tagName": "TEMP_01",
  "displayName": "온도 1번",
  "localAddress": 100,
  "type": "holding_register",
  "readOnly": false,
  "length": 1,
  "unit": "℃",
  "scale": 0.1,
  "isSigned": true,
  "bitLabels": "",
  "minValue": -50.0,
  "maxValue": 150.0,
  "byteOrder": "default",
  "unifiedAddress": 1001
}
```

> 필수: `tagName`, `type`  
> `unifiedAddress` 생략 시 자동 할당 (0–4999). 수동 지정 시 `>= 5000` 이어야 함.

**Response 201**
```json
{
  "tagName": "TEMP_01",
  "localAddress": 100,
  "type": "holding_register",
  "unifiedAddress": 1001
}
```

**에러** 409: `unifiedAddress` 중복

---

### PUT /api/registers/{registerId}
권한: `ManagerOrAbove` | 폴링 중 409

**Request Body** (변경할 필드만 포함, `tagName` 필수)
```json
{
  "tagName": "TEMP_01",
  "scale": 0.01,
  "unit": "°C"
}
```

**Response 200** (body 없음)  
**에러** 404: 레지스터 없음 | 409: `unifiedAddress` 중복

---

### DELETE /api/registers/{registerId}
권한: `ManagerOrAbove` | 폴링 중 409

**Response 200** (body 없음)

---

### GET /api/registers/unified-id/check?id={unifiedAddress}
권한: `Any`

**Query Parameter**: `id` (정수, ≥ 0)

**Response 200**
```json
{ "available": true }
```

---

### POST /api/registers/{registerId}/write
권한: `ManagerOrAbove`

**Request Body**
```json
{ "rawValues": [1234] }
```

> `rawValues`: `quint16` 배열. Coil 타입은 0/1만 유효.  
> 쓰기는 큐에 등록 후 다음 폴링 사이클에서 실행됩니다.

**Response 202**
```json
{ "ok": true }
```

**에러** 400: readOnly 레지스터 | 404: 레지스터 없음

---

### GET /api/registers/realtime
권한: `Any`

**Response 200**
```json
{
  "registers": [
    {
      "id": 1001,
      "registerId": 1,
      "tagName": "TEMP_01",
      "displayName": "온도 1번",
      "deviceId": 1,
      "localAddress": 100,
      "sourceType": "holding_register",
      "unit": "℃",
      "scaledValue": 25.3,
      "minValue": -50.0,
      "maxValue": 150.0,
      "scale": 0.1,
      "rawWord": 253,
      "bitLabels": "",
      "readOnly": false,
      "isValid": true,
      "outOfRange": false,
      "quality": "good",
      "lastUpdated": "2026-10-13T12:00:00Z",
      "errorMessage": ""
    }
  ]
}
```

> `quality`: `"good"` | `"normal"` | `"bad"`

---

## 6. 로그

### GET /api/logs
권한: `Any`

**Query Parameters** (모두 선택)

| 파라미터 | 타입 | 기본값 | 설명 |
|---|---|---|---|
| `limit` | int | 설정값 | 최대 조회 건수 |
| `offset` | int | 0 | 시작 오프셋 |
| `level` | string | ALL | `INFO` \| `WARN` \| `ERROR` |
| `from` | string | — | 시작일 `YYYY-MM-DD` |
| `to` | string | — | 종료일 `YYYY-MM-DD` |

**Response 200**
```json
{
  "logs": [
    { "id": 1, "timestamp": "2026-10-13T12:00:00", "level": "INFO", "message": "..." }
  ],
  "count": 20,
  "total": 150,
  "limit": 20,
  "offset": 0,
  "level": "ALL",
  "from": "",
  "to": ""
}
```

---

## 7. 사용자 관리

### GET /api/users
권한: `Any`

**Response 200**
```json
{
  "users": [
    {
      "id": 0,
      "username": "admin",
      "displayName": "관리자",
      "description": "",
      "role": "admin",
      "status": "active",
      "failedLoginCount": 0,
      "lastLoginAt": "2026-10-13T12:00:00",
      "lastLoginIp": "192.168.0.10"
    }
  ]
}
```

> `role`: `"admin"` | `"manager"` | `"user"`  
> `status`: `"active"` | `"locked"` | `"disabled"`

---

### POST /api/users
권한: `ManagerOrAbove`

**Request Body**
```json
{
  "username": "user1",
  "displayName": "사용자1",
  "description": "현장 운영자",
  "password": "pass1234",
  "role": "user"
}
```

> 필수: `username`, `password`, `role`  
> `password` 최소 길이: 보안 정책의 `minPasswordLength` (기본 8자)

**Response 201**
```json
{ "username": "user1", "role": "user" }
```

**에러** 409: 사용자명 중복

---

### PUT /api/users/{username}
권한: `ManagerOrAbove` | id=0(admin) 수정 불가(403)

**Request Body** (변경할 필드만 포함)
```json
{
  "displayName": "새 이름",
  "description": "메모",
  "role": "manager"
}
```

**Response 200**
```json
{
  "username": "user1",
  "displayName": "새 이름",
  "description": "메모",
  "role": "manager"
}
```

---

### DELETE /api/users/{username}
권한: `ManagerOrAbove` | id=0(admin) 삭제 불가(403)

**Response 200** (body 없음)  
**에러** 404: 사용자 없음

---

### PUT /api/users/{username}/password
권한: `Any`

**동작 규칙**
- 본인(`isSelf=true`) + 비관리자: `currentPassword` 필수, 일치 검증 후 변경
- Admin 계정으로 타인 변경: `currentPassword` 불필요
- 타인 + 비관리자: 403

**Request Body**
```json
{
  "currentPassword": "old1234",
  "newPassword": "new1234"
}
```

> `newPassword` 최소 길이: 보안 정책의 `minPasswordLength`

**Response 200**
```json
{ "username": "user1" }
```

---

### PUT /api/users/{username}/status
권한: `ManagerOrAbove` | id=0(admin) 변경 불가(403)

**Request Body**
```json
{ "status": "active" }
```

> `status`: `"active"` | `"locked"` | `"disabled"`  
> `active` 설정 시 `failedLoginCount` 자동 0 초기화

**Response 200**
```json
{ "username": "user1", "status": "active" }
```

---

### GET /api/users/login-history
권한: `ManagerOrAbove`

**Query Parameters** (선택)

| 파라미터 | 타입 | 기본값 | 설명 |
|---|---|---|---|
| `limit` | int | 100 | 최대 조회 건수 |
| `username` | string | — | 특정 사용자 필터 |

**Response 200**
```json
{
  "history": [
    {
      "id": 1,
      "timestamp": "2026-10-13T12:00:00",
      "username": "admin",
      "action": "login",
      "result": "success",
      "ip": "192.168.0.10"
    }
  ],
  "count": 1
}
```

> `action`: `"login"` | `"logout"`  
> `result`: `"success"` | `"invalid_password"` | `"account_disabled"` | `"account_locked"`

---

### DELETE /api/users/login-history
권한: `ManagerOrAbove`

**Query Parameters** (선택)

| 파라미터 | 설명 |
|---|---|
| `username` | 특정 사용자 이력만 삭제. 생략 시 전체 삭제 |

**Response 200** (body 없음)

---

### GET /api/users/security-policy
권한: `ManagerOrAbove`

**Response 200**
```json
{
  "maxFailedAttempts": 5,
  "sessionTimeoutMinutes": 30,
  "minPasswordLength": 8,
  "autoLogout": true
}
```

---

### PUT /api/users/security-policy
권한: `ManagerOrAbove`

**Request Body** (변경할 필드만 포함)
```json
{
  "maxFailedAttempts": 5,
  "sessionTimeoutMinutes": 30,
  "minPasswordLength": 8,
  "autoLogout": true
}
```

> 유효 범위: `maxFailedAttempts` 1–20 | `sessionTimeoutMinutes` 1–1440 | `minPasswordLength` 4–32

**Response 200** — 저장된 값 전체 반환 (GET과 동일)

---

## 8. 시스템 설정

### GET /api/config
권한: `Any`

**Response 200**
```json
{
  "network": {
    "interfaces": [
      {
        "name": "eth0",
        "role": "primary",
        "enabled": true,
        "mode": "static",
        "ipAddress": "192.168.0.150",
        "netmask": "255.255.255.0",
        "gateway": "192.168.0.1",
        "dns": "8.8.8.8"
      }
    ]
  },
  "serial": {
    "device": "/dev/ttymxc1",
    "baudRate": 9600,
    "dataBits": 8,
    "parity": "none",
    "stopBits": 1
  },
  "system": {
    "hostname": "smartroute",
    "ntpServer": "pool.ntp.org"
  },
  "modbusServer": {
    "enabled": false,
    "port": 502,
    "slaveId": 1
  },
  "trend": {
    "sampleIntervalSec": 10,
    "channels": [
      {
        "regId": 1001,
        "name": "온도1",
        "unit": "℃",
        "scale": 0.1,
        "isSigned": true,
        "minValue": 0,
        "maxValue": 65535
      }
    ]
  }
}
```

---

### POST /api/config/reset
권한: `ManagerOrAbove`

설정 파일을 출고 기본값으로 초기화합니다. DB(장비/레지스터/사용자)는 변경하지 않습니다.

**Response 200**
```json
{
  "network": { /* 초기화된 네트워크 설정 */ },
  "serial":  { /* 초기화된 시리얼 설정 */ },
  "system":  { /* 초기화된 시스템 설정 */ },
  "modbusServer": { /* 초기화된 Modbus 서버 설정 */ },
  "restartRequired": true
}
```

---

### PUT /api/config/network
권한: `ManagerOrAbove`

**Request Body**
```json
{
  "interfaces": [
    {
      "name": "eth0",
      "role": "primary",
      "enabled": true,
      "mode": "static",
      "ipAddress": "192.168.0.150",
      "netmask": "255.255.255.0",
      "gateway": "192.168.0.1",
      "dns": "8.8.8.8"
    }
  ]
}
```

**Response 200**
```json
{ "restartRequired": true }
```

---

### PUT /api/config/serial
권한: `ManagerOrAbove`

**Request Body** (변경할 필드만 포함)
```json
{
  "baudRate": 19200,
  "dataBits": 8,
  "parity": "none",
  "stopBits": 1
}
```

> `parity`: `"none"` | `"even"` | `"odd"`

**Response 200**
```json
{ "restartRequired": true }
```

---

### PUT /api/config/system
권한: `ManagerOrAbove`

**Request Body** (변경할 필드만 포함)
```json
{
  "hostname": "smartroute",
  "ntpServer": "pool.ntp.org"
}
```

**Response 200**
```json
{ "restartRequired": true }
```

---

### PUT /api/config/modbus-server
권한: `ManagerOrAbove`

**Request Body** (변경할 필드만 포함)
```json
{
  "enabled": true,
  "port": 502,
  "slaveId": 1
}
```

> `port`: 1–65535 | `slaveId`: 1–247

**Response 200**
```json
{ "restartRequired": true }
```

---

## 9. 시스템 정보 / 제어

### GET /api/system/info
권한: `Any`

**Response 200**
```json
{
  "info": {
    "ver": "1",
    "rev": "0",
    "zcode": "0",
    "schemaVersion": 1,
    "lastUpdateDate": "2026-10-13"
  },
  "disk": {
    "total": 7516192768,
    "used": 1234567890,
    "available": 6000000000,
    "usedPercent": 16.5
  },
  "summary": {
    "deviceCount": 3,
    "registerCount": 24,
    "userCount": 2,
    "logCount": 150,
    "pollingActive": true
  },
  "ntp": {
    "server": "pool.ntp.org",
    "synced": true,
    "maxErrorMs": 12
  }
}
```

---

### GET /api/system/resources
권한: `Any`

3초 주기로 백그라운드에서 샘플링한 캐시 값을 즉시 반환합니다.

**Response 200**
```json
{
  "cpu": {
    "usagePercent": 15.3,
    "loadAvg1": 0.12,
    "loadAvg5": 0.08,
    "loadAvg15": 0.05,
    "tempCelsius": 52.0
  },
  "memory": {
    "totalKb": 524288,
    "usedKb": 102400,
    "usagePercent": 19.5
  },
  "swap": {
    "totalKb": 0,
    "usedKb": 0,
    "usagePercent": 0.0
  },
  "disk": [
    { "mount": "/",    "totalMb": 7168, "usedMb": 1178, "usagePercent": 16.4 },
    { "mount": "/var", "totalMb": 1024, "usedMb": 50,   "usagePercent": 4.9  }
  ],
  "network": {
    "eth0": { "rxBytes": 1234567, "txBytes": 890123 },
    "eth1": { "rxBytes": 0,       "txBytes": 0       }
  },
  "uptimeSeconds": 86400,
  "cachedAt": "2026-10-13T12:00:00"
}
```

---

### POST /api/system/restart
권한: `ManagerOrAbove`

300ms 후 프로세스를 종료합니다. systemd `Restart=always` 설정으로 자동 재시작됩니다.

**Response 200**
```json
{ "restarting": true }
```

---

## 10. 시스템 시간

### GET /api/system/time
권한: `Any`

**Response 200**
```json
{
  "ntpEnabled": true,
  "synced": true,
  "timezone": "Asia/Seoul",
  "localTime": "2026-10-13T21:00:00+09:00",
  "utcTime":   "2026-10-13T12:00:00Z"
}
```

---

### PUT /api/system/ntp
권한: `Any`

**Request Body**
```json
{ "enabled": true }
```

**Response 200**
```json
{ "ok": true, "ntpEnabled": true }
```

---

### PUT /api/system/timezone
권한: `Any`

NTP 활성 여부와 무관하게 적용됩니다.  
⚠️ 타임존 변경 후 기존 .tnd 파일의 STIME 기반 타임스탬프 계산이 어긋날 수 있습니다.

**Request Body**
```json
{ "timezone": "Asia/Seoul" }
```

> IANA 타임존 식별자 (예: `"UTC"`, `"Asia/Seoul"`, `"America/New_York"`)  
> `;`, `&`, `..` 포함 시 400

**Response 200**
```json
{ "ok": true, "timezone": "Asia/Seoul" }
```

**에러** 400: 유효하지 않은 타임존 식별자

---

### PUT /api/system/time
권한: `Any`  
⚠️ NTP가 활성화된 상태에서는 **409** 반환. 먼저 `PUT /api/system/ntp { "enabled": false }` 를 호출해야 합니다.

**Request Body**
```json
{ "datetime": "2026-10-13T21:00:00" }
```

> ISO 8601 형식. 시스템 시각 변경 후 외부 RTC(`hwclock --systohc`)에도 반영됩니다.

**Response 200**
```json
{ "ok": true, "localTime": "2026-10-13T21:00:00" }
```

**에러** 409: NTP 활성 상태

---

## 11. 언어 설정

### GET /api/system/language
권한: `없음 (인증 불필요)`

**응답** 200:
```json
{ "language": "ko" }
```

### PUT /api/system/language
권한: `ManagerOrAbove`

**요청 바디:**
```json
{ "language": "ko" }
```

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `language` | string | ✅ | 언어 코드. 허용값: `"ko"` \| `"en"` \| `"ja"` \| `"zh"` |

**응답** 200:
```json
{ "ok": true, "language": "ko" }
```

**에러** 400: `language` 필드 누락 또는 허용되지 않는 값

---

## 12. 트렌드 설정 (채널 구성)

### GET /api/trend/config
권한: `Any`

**Response 200**
```json
{
  "sampleIntervalSec": 10,
  "channels": [
    {
      "regId": 1001,
      "name": "온도1",
      "tag": "TEMP_01",
      "unit": "℃",
      "scale": 0.1,
      "isSigned": true,
      "minValue": 0,
      "maxValue": 65535
    }
  ]
}
```

---

### PUT /api/trend/config
권한: `ManagerOrAbove`

**Request Body** (변경할 필드만 포함)
```json
{
  "sampleIntervalSec": 10,
  "channels": [
    {
      "regId": 1001,
      "name": "온도1",
      "tag": "TEMP_01",
      "unit": "℃",
      "scale": 0.1,
      "isSigned": true,
      "minValue": 0,
      "maxValue": 65535
    }
  ]
}
```

> `sampleIntervalSec`: `1` | `5` | `10` | `30` | `60`  
> `channels` 최대 16개. `tag` 생략 시 `"CH_01"` 형식으로 자동 부여 (최대 16자)

**Response 200** — 저장된 설정 전체 반환 (GET과 동일)

---

## 13. 트렌드 파일 (레코딩)

### POST /api/tfile/start
권한: `Any`

현재 트렌드 설정(`GET /api/trend/config`)을 기반으로 새 `.tnd` 파일 레코딩을 시작합니다.

**Response 200**
```json
{ "ok": true, "filename": "261013_210000.tnd" }
```

**에러**  
- 400: 트렌드 채널 미설정  
- 409: 이미 녹화 중

---

### POST /api/tfile/stop
권한: `Any`

**Response 200**
```json
{ "ok": true }
```

---

### GET /api/tfile/status
권한: `Any`

**Response 200**
```json
{
  "recording": true,
  "filename": "261013_210000.tnd",
  "elapsedSec": 3600,
  "fileSize": 10485760,
  "fileSizeLimit": 4294967296,
  "recordCount": 360,
  "freq": 10
}
```

> 녹화 중이 아닐 때: `recording=false`, `filename`은 마지막 파일명, `elapsedSec=0`

---

### GET /api/tfile/files
권한: `Any`

**Response 200**
```json
{
  "files": [
    {
      "filename": "261013_210000.tnd",
      "fileSize": 10485760,
      "startTime": 1760450400,
      "endTime":   1760454000,
      "recordCount": 360,
      "freq": 10,
      "chCount": 4
    }
  ]
}
```

> `startTime`, `endTime`: Unix 초 (UTC)

---

### DELETE /api/tfile/files/{filename}
권한: `Any`

**에러**  
- 400: 잘못된 파일명 (경로 포함, `..` 포함)  
- 404: 파일 없음  
- 409: 현재 녹화 중인 파일 삭제 시도

**Response 200**
```json
{ "ok": true }
```

---

### GET /api/tfile/header?file={filename}
권한: `Any`

Content-Type: `application/octet-stream`  
**Response**: `.tnd` 파일 메인 헤더 512 bytes (binary)

> 파싱 방법은 `TrendHandler/` 폴더의 헤더 포맷 또는 `api-spec.md` 내 tfile 섹션 참조

---

### GET /api/tfile/latest?file={filename}
권한: `Any`

Content-Type: `application/octet-stream`  
**Response**: 마지막 유효 레코드 32 bytes (16채널 × uint16, binary)

> 녹화 중인 파일에도 요청 가능. 실시간 1초 폴링에 사용.

---

### GET /api/tfile/sample?file={filename}&from={unix}&to={unix}&step={N}
권한: `Any`

| 파라미터 | 타입 | 필수 | 설명 |
|---|---|---|---|
| `file` | string | ✓ | 파일명 |
| `from` | uint32 | ✓ | 조회 시작 Unix 초 (UTC) |
| `to` | uint32 | ✓ | 조회 종료 Unix 초 (UTC) |
| `step` | uint16 | — | 레코드 건너뛰기 간격 (기본 1) |

> 서버는 `(to-from)/step > 50,000` 이면 `step`을 자동 상향 조정합니다.  
> `step` 계산 권장: `step = max(1, ceil((to-from)/FREQ / canvas_width_px))`

Content-Type: `application/octet-stream`  
**Response**: 레코드 배열 (헤더 없음), 각 레코드 = 16 × uint16 = 32 bytes

---

## 14. HMI 태그

레지스터 `tagName`과 별도의 독립된 네임스페이스. 레지스터를 "소스"로 느슨하게 참조하며, 레지스터 이름이 바뀌어도 HMI 화면은 영향받지 않음.

### 데이터 모델

```json
{
  "id": "TK1_LEVEL",
  "name": "1번 탱크 수위",
  "source": { "registerId": 123 },
  "unit": "%",
  "min": 0.0,
  "max": 100.0,
  "decimals": 1,
  "access": "r",
  "createdAt": "2026-10-14T10:00:00",
  "updatedAt": "2026-10-14T10:00:00"
}
```

- `unit / min / max / decimals / access` 생략(null) 시 소스 레지스터 값에서 상속
- `decimals` 생략 시 `scale`에서 자동 추론 (`scale=0.1 → 1`, `scale=0.01 → 2`, …)
- `access`: `"r"` | `"rw"` — 레지스터 `readOnly` 상속 또는 오버라이드
- `id` 허용 문자: 영숫자 + `_` + `-`, 최대 64자

### GET /api/hmi/tags
권한: `Any`

**응답** 200:
```json
{ "tags": [ { ...HmiTag... } ] }
```

### POST /api/hmi/tags
권한: `ManagerOrAbove`

**요청 바디:**
```json
{
  "id": "TK1_LEVEL",
  "name": "1번 탱크 수위",
  "source": { "registerId": 123 },
  "unit": "%",
  "min": 0.0,
  "max": 100.0,
  "decimals": 1,
  "access": "r"
}
```
`id / name / source.registerId` 필수, 나머지 선택

**응답** 201: 생성된 HmiTag 객체  
**에러** 400: 유효하지 않은 id 형식 / registerId 없음 / access 값 오류  
**에러** 409: id 중복

### GET /api/hmi/tags/{id}
권한: `Any`

**응답** 200: HmiTag 객체  
**에러** 404

### PUT /api/hmi/tags/{id}
권한: `ManagerOrAbove`

요청 바디: POST와 동일 (`id`는 URL에서 받음)

**응답** 200: 수정된 HmiTag 객체  
**에러** 404

### DELETE /api/hmi/tags/{id}
권한: `ManagerOrAbove`

**응답** 200: `{ "ok": true }`  
**에러** 404

### GET /api/hmi/values?tags=A,B
권한: `Any`

`tags` 쿼리 파라미터 생략 시 전체 태그 반환

**응답** 200:
```json
{
  "ts": "2026-10-14T02:26:05Z",
  "values": {
    "TK1_LEVEL": {
      "v": 56.9,
      "q": "good",
      "unit": "%",
      "decimals": 1,
      "access": "r"
    }
  }
}
```

| `q` 값 | 의미 |
|--------|------|
| `"good"` | 마지막 폴링 경과 < 1.5× interval |
| `"normal"` | 1.5× ~ 3× interval (응답 지연) |
| `"bad"` | 3× 초과 또는 응답 없음 |

`v`는 `bad` 상태이거나 아직 값이 없으면 `null`

### POST /api/hmi/write
권한: `ManagerOrAbove`

**요청 바디:**
```json
{ "tag": "LEVEL_SP", "v": 75.0 }
```

coil 타입 레지스터는 `v`에 JSON boolean(`true`/`false`) 또는 숫자(`0`/`1`) 전달 가능.  
서버가 엔지니어링 단위 `v`를 `raw = round(v / scale)`로 변환 후 기록.

**응답** 202: `{ "ok": true, "tag": "LEVEL_SP" }`  
**에러** 404: 태그 없음  
**에러** 405: `access: "r"` 태그 쓰기 시도

---

## 15. 유지보수

### GET /api/maintenance/backup
권한: `AdminOnly`

Content-Type: `application/zip`  
Content-Disposition: `attachment; filename="swr_backup_YYYYMMdd_HHmmss.zip"`

**Response**: 설정 백업 ZIP 파일 (binary)

> 포함 내용: 장비, 레지스터, 사용자, 시스템 설정 (config.json)

---

### POST /api/maintenance/restore/validate
권한: `AdminOnly`

**Request Body**: ZIP 파일 binary (`Content-Type: application/octet-stream`)

**Response 200**
```json
{
  "restoreId": "uuid-string",
  "backupInfo": {
    "product": "SmartRoute",
    "createdAt": "2026-10-13T12:00:00",
    "hostname": "smartroute",
    "version": "1",
    "revision": "0",
    "zcode": "0",
    "schemaVersion": 1
  },
  "config":    { "available": true, "count": 1,  "warning": "" },
  "devices":   { "available": true, "count": 3,  "warning": "" },
  "registers": { "available": true, "count": 24, "warning": "" },
  "users":     { "available": true, "count": 2,  "warning": "" },
  "hmi":       { "available": false,"count": 0,  "warning": "" },
  "warnings":  []
}
```

**에러** 422: 파일 무결성 검증 실패

---

### POST /api/maintenance/restore/apply
권한: `AdminOnly` | 폴링 중 409

**Request Body**
```json
{
  "restoreId": "validate에서 받은 uuid",
  "options": {
    "config":    true,
    "network":   false,
    "devices":   true,
    "registers": true,
    "users":     true,
    "hmi":       false
  }
}
```

> `network` 기본값 `false` (IP 변경으로 접속 불능 방지)

**Response 200**
```json
{ "ok": true, "restartRequired": true }
```

---

### POST /api/maintenance/factory-reset
권한: `AdminOnly` | 폴링 중 409

DB(장비/레지스터/사용자) 전체 삭제, 설정 파일 초기화, 로그 전체 삭제, admin 비밀번호 초기화를 수행합니다.

**Response 200**
```json
{ "ok": true, "restartRequired": true }
```
