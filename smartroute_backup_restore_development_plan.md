# SmartRoute 백업/복원 개발 플랜

## 1. 목적

이 문서는 SmartRoute(SWR) 장비의 설정 백업 및 복원 기능 개발 방향을 정의한다.

목표는 하나의 SWR 장비에서 구성한 설정을 백업 파일로 다운로드하고, 다른 SWR 장비에 업로드하여 동일한 운영 설정을 안정적으로 적용할 수 있도록 하는 것이다.

백업/복원 기능은 단순 파일 복사가 아니라 다음 조건을 만족해야 한다.

- 장비 목록, 레지스터 목록, 시스템 설정을 안전하게 이관한다.
- 네트워크 설정과 사용자 계정처럼 장비 고유성이 강한 항목은 기본 복원 대상에서 제외한다.
- 복원 전 파일 검증과 미리보기 단계를 제공한다.
- DB 적용은 transaction 기반으로 처리하고 실패 시 rollback한다.
- 복원 완료 후 재시작 필요 여부를 웹 화면에 명확히 표시한다.

---

## 2. 전체 사용 시나리오

### 2.1 백업 생성

운영자는 SWR-A 장비의 웹 유지보수 화면에서 백업 파일을 다운로드한다.

```text
SWR-A 웹 접속
    ↓
유지보수 화면
    ↓
전체 파라메터 백업 Download
    ↓
swr_backup_20260624_102431.zip 다운로드
```

### 2.2 다른 장비에 복원

운영자는 SWR-B 장비의 웹 유지보수 화면에서 SWR-A에서 받은 백업 파일을 업로드한다.

```text
SWR-B 웹 접속
    ↓
유지보수 화면
    ↓
백업 파일 Upload
    ↓
파일 검증
    ↓
복원 항목 미리보기
    ↓
복원 적용
    ↓
재시작 필요 알림 표시
```

---

## 3. 백업 파일 형식

백업 파일은 단일 JSON 파일보다 ZIP 패키지 형식을 권장한다.

파일명 예시:

```text
swr_backup_20260624_102431.zip
```

ZIP 내부 구성:

```text
swr_backup_20260624_102431.zip
├── manifest.json
├── config.json
├── devices.json
├── registers.json
├── users.json
├── hmi.json
└── checksum.sha256
```

현재 개발 범위에서는 **HMI 설정**은 현재는 빈 데이터로 저장한다. (사양 및 구현이 미완료 이므로)

---

## 4. 백업 포함 항목

### 4.1 포함 대상

| 파일 | 내용 |
|---|---|
| `manifest.json` | 백업 파일 메타 정보 |
| `config.json` | 시스템 설정 정보 |
| `devices.json` | 장비 목록 |
| `registers.json` | 레지스터 목록 |
| `users.json` | 사용자 목록 (admin 제외) |
| `hmi.json` | HMI 디자인 스케치 정보 (현재 빈 데이터) |
| `checksum.sha256` | 백업 파일 무결성 검증 정보 |

### 4.2 제외 대상

다음 항목은 초기 버전의 백업/복원 대상에서 제외한다.

| 항목 | 제외 이유 |
|---|---|
| 런타임 상태 | 다른 장비에 복원할 의미 없음 |
| 현재 측정값 | 실시간 데이터이므로 백업 대상 아님 |
| 폴링 성공/실패 카운트 | 장비별 실행 상태이므로 백업 대상 아님 |
| 로그 파일 | 설정 이관과 무관 |
| 세션 정보 | 보안상 복원 대상 아님 |
| 인증 토큰 | 보안상 복원 대상 아님 |

---

## 5. 주의해서 처리할 항목

다른 SWR 장비에 복원할 때 위험할 수 있는 항목은 기본 복원 대상에서 제외한다.

### 5.1 네트워크 설정

네트워크 설정은 백업 파일에 포함할 수는 있지만, 복원 시 기본값은 OFF로 둔다.

이유:

- IP 주소 충돌 가능
- 현재 접속 중인 웹 연결이 끊길 수 있음
- 장비별 eth0/eth1 역할이 다를 수 있음
- 현장 네트워크 구성에 따라 재설정이 필요할 수 있음

권장 정책:

```text
네트워크 설정은 백업에는 포함 가능
복원 화면에서는 기본 선택 해제
사용자가 명시적으로 선택한 경우에만 복원
```

복원 화면 경고 문구:

```text
네트워크 설정을 복원하면 현재 접속이 끊기거나 IP 충돌이 발생할 수 있습니다.
```

### 5.2 사용자 계정

사용자 계정 정책:

- admin 계정은 백업/복원 대상에서 제외한다.
- admin 계정은 삭제할 수 없으며 항상 id=0으로 고정한다.
- admin 이외의 사용자 계정은 백업 포함 가능하지만 초기 버전에서는 복원 미지원이다.

이유:

- 현재 장비의 관리자 계정이 덮어씌워질 수 있음
- 로그인 불가 상태가 발생할 수 있음

---

## 6. manifest.json 구조

`manifest.json`은 백업 파일의 메타 정보를 가진다.

예시:

```json
{
  "product": "SmartRoute",
  "backupVersion": 1,
  "createdAt": "2026-06-24T10:24:31",
  "sourceDevice": {
    "hostname": "swr-field-01",
    "version": "1",
    "revision": "0",
    "zcode": "0",
    "schemaVersion": 3
  },
  "contents": {
    "config": true,
    "devices": true,
    "registers": true,
    "network": true,
    "users": false,
    "hmi": true
  }
}
```

필수 필드:

| 필드 | 설명 |
|---|---|
| `product` | 제품명 |
| `backupVersion` | 백업 파일 포맷 버전 |
| `createdAt` | 백업 생성 시각 |
| `hostname` | 백업 원본 장비 이름 |
| `version` | 백업 원본 장비 버전 |
| `revision` | 백업 원본 장비 리비전 |
| `zcode` | 백업 원본 장비의 특주 코드 |
| `schemaVersion` | DB 또는 설정 스키마 버전 (백업 파일의 호환성 검토 키) |
| `contents` | 백업 파일에 포함된 항목 목록 |

---

## 7. config.json 구조

`config.json`은 시스템 설정 정보를 가진다.

초기 복원 대상:

- 시스템 기본 설정
- RS485 설정
- Modbus TCP Server 설정
- NTP 설정

주의 대상:

- 네트워크 설정은 포함 가능하지만 기본 복원 대상에서는 제외한다.

예시:

```json
{
  "system": {
    "hostname": "swr-field-01",
    "ntpServer": "pool.ntp.org"
  },
  "serial": {
    "device": "/dev/ttymxc1",
    "baudRate": 9600,
    "dataBits": 8,
    "parity": "none",
    "stopBits": 1
  },
  "modbusServer": {
    "enabled": true,
    "port": 502,
    "slaveId": 1
  },
  "network": {
    "included": true,
    "restoreDefault": false
  }
}
```

---

## 8. devices.json 구조

`devices.json`은 등록된 장비 목록을 가진다.

예시:

```json
{
  "devices": [
    {
      "id": 1,
      "name": "냉동기1",
      "connectionType": "rtu",
      "protocol": "modbus_rtu",
      "serialDevice": "/dev/ttymxc1",
      "ipAddress": "",
      "tcpPort": 0,
      "slaveId": 1,
      "timeoutMs": 1000,
      "intervalMs": 1000,
      "retryCount": 3,
      "byteOrder": "ABCD",
      "enabled": true
    }
  ]
}
```

주의:

- 장비 ID는 백업 원본 기준의 ID이다.
- 복원 시 전체 교체 방식에서는 그대로 사용할 수 있다.
- 병합 방식에서는 ID 충돌 처리가 필요하므로 초기 버전에서는 지원하지 않는다.

---

## 9. registers.json 구조

`registers.json`은 각 장비의 레지스터 정의를 가진다.

통합 레지스터 주소(UnifiedRegister.deviceAddress) 포함 여부:

- **포함하지 않는다.**
- 통합 레지스터 주소는 장비 레지스터 정의(RegisterField)로부터 런타임에 자동 생성되는 파생값이다.
- 복원 시 장비/레지스터를 재적용하면 통합 레지스터 매핑은 자동으로 재구성된다.

예시:

```json
{
  "registers": [
    {
      "id": 1,
      "deviceId": 1,
      "tagName": "CH1_PV",
      "displayName": "냉동기1 현재온도",
      "address": 40001,
      "registerType": "holding",
      "dataType": "float",
      "length": 2,
      "unit": "°C",
      "scale": 0.1,
      "readOnly": true,
      "enabled": true
    }
  ]
}
```

---

## 10. users.json 구조

`users.json`은 제품에 등록된 사용자 정보이다. admin 계정은 제외한다.

필수 필드:

| 필드 | 타입 | 설명 |
|---|---|---|
| `username` | string | 로그인 ID |
| `displayName` | string | 화면 표시 이름 |
| `description` | string | 계정 설명 |
| `passwordHash` | string | SHA-256 해시된 비밀번호 |
| `role` | string | 권한 (`"admin"` \| `"manager"` \| `"user"`) |
| `status` | string | 계정 상태 (`"active"` \| `"disabled"` \| `"locked"`) |

제외 필드 (런타임/감사 데이터):

| 필드 | 제외 이유 |
|---|---|
| `failedLoginCount` | 런타임 상태, 복원 대상 아님 |
| `lastLoginAt` | 감사 데이터, 복원 대상 아님 |
| `lastLoginIp` | 감사 데이터, 복원 대상 아님 |
| `createdAt` | 복원 시 재생성됨 |
| `updatedAt` | 복원 시 재생성됨 |

예시:

```json
{
  "users": [
    {
      "username": "operator1",
      "displayName": "현장운전원1",
      "description": "1공장 운전원",
      "passwordHash": "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3",
      "role": "user",
      "status": "active"
    },
    {
      "username": "manager1",
      "displayName": "관리자1",
      "description": "설비 담당 관리자",
      "passwordHash": "b3a8e0e1f9ab1bfe3a36f231f676f78bb28a2d0b7a6b0e5b3a5c3c9c2a3e5f7",
      "role": "manager",
      "status": "active"
    }
  ]
}
```

초기 버전에서 사용자 계정 복원은 미지원이다. 백업에는 포함하되 복원 적용 시 무시한다.

---

## 11. checksum.sha256

백업 파일 무결성 검증을 위해 각 JSON 파일에 대한 SHA-256 체크섬을 기록한다.

예시:

```text
a7b9f2...  manifest.json
c4d1aa...  config.json
3f88bc...  devices.json
91aa32...  registers.json
b2cc41...  users.json
00e1f5...  hmi.json
```

복원 시 서버는 checksum을 다시 계산하여 파일 변조 여부를 확인한다.

정책:

```text
checksum 불일치 시 복원 차단
필수 파일 누락 시 복원 차단
알 수 없는 파일이 있어도 무시 가능
```

---

## 12. 백업 다운로드 API

```http
GET /api/maintenance/backup
```

응답:

```http
Content-Type: application/zip
Content-Disposition: attachment; filename="swr_backup_20260624_102431.zip"
```

서버 동작:

```text
1. 현재 설정 정보 조회
2. manifest.json 생성
3. config.json 생성
4. devices.json 생성
5. registers.json 생성
6. users.json 생성
7. hmi.json 생성 (빈 데이터)
8. checksum.sha256 생성
9. ZIP 패키지 생성
10. 파일 다운로드 응답 반환
```

---

## 13. 복원 Upload 절차

복원은 즉시 적용하지 않는다.

반드시 다음 단계를 거친다.

```text
1. 백업 파일 선택
2. 서버 업로드
3. 파일 형식 검증
4. checksum 검증
5. manifest 정보 검증
6. 호환성 검사
7. 복원 가능 항목 미리보기
8. 사용자가 복원 항목 선택
9. 복원 적용
10. 재시작 필요 알림 표시
```

---

## 14. 복원 검증 API

```http
POST /api/maintenance/restore/validate
Content-Type: multipart/form-data
```

요청:

```text
file: swr_backup_20260624_102431.zip
```

응답 예시:

```json
{
  "ok": true,
  "restoreId": "tmp_restore_20260624_102431",
  "backupInfo": {
    "createdAt": "2026-06-24T10:24:31",
    "sourceHostname": "swr-field-01",
    "version": "1",
    "revision": "0",
    "zcode": "0",
    "schemaVersion": 3
  },
  "items": {
    "config": {
      "available": true
    },
    "devices": {
      "available": true,
      "count": 24
    },
    "registers": {
      "available": true,
      "count": 1284
    },
    "users": {
      "available": true,
      "count": 3
    },
    "hmi": {
      "available": true,
      "count": 0
    },
    "network": {
      "available": true,
      "restoreDefault": false,
      "warning": "네트워크 설정 복원 시 IP 충돌이 발생할 수 있습니다."
    }
  },
  "warnings": [
    "백업 파일의 Hostname이 현재 장비와 다릅니다.",
    "네트워크 설정은 기본 복원 대상에서 제외됩니다."
  ]
}
```

---

## 15. 복원 적용 API

```http
POST /api/maintenance/restore/apply
Content-Type: application/json
```

요청 예시:

```json
{
  "restoreId": "tmp_restore_20260624_102431",
  "options": {
    "config": true,
    "devices": true,
    "registers": true,
    "hmi": false,
    "users": false,
    "network": false
  }
}
```

응답 예시:

```json
{
  "ok": true,
  "message": "복원이 완료되었습니다.",
  "restartRequired": true
}
```

---

## 16. 복원 적용 서버 처리 흐름

```text
1. restoreId 유효성 확인
2. 임시 업로드 파일 존재 확인
3. manifest 재검증
4. checksum 재검증
5. 폴링 동작 중이면 복원 차단
6. DB transaction 시작
7. 기존 장비/레지스터 데이터 삭제
8. 백업 장비/레지스터 데이터 삽입
9. 선택된 config 항목 적용
10. transaction commit
11. 임시 파일 삭제
12. restartRequired = true 반환
```

실패 시:

```text
1. transaction rollback
2. 오류 로그 기록
3. 현재 설정 유지
4. 임시 파일 삭제
5. 프론트에 실패 사유 반환
```

---

## 17. 복원 중 폴링 처리

복원 중에는 장비/레지스터 정보가 변경되므로 폴링 엔진과 충돌할 수 있다.

초기 버전 추천 정책:

```text
폴링 중이면 복원 적용 차단
사용자에게 폴링 정지 후 다시 시도 안내
```

추후 개선:

```text
복원 적용 시 자동으로 폴링 정지
복원 완료 후 설정 재로드
필요 시 서비스 재시작
```

---

## 18. 재시작 정책

복원 후에는 설정 reload만으로 충분하지 않을 수 있다.

초기 버전에서는 복원 완료 후 다음 응답을 반환한다.

```json
{
  "restartRequired": true
}
```

웹 화면에서는 다음 메시지를 표시한다.

```text
복원 적용이 완료되었습니다.
변경 사항을 완전히 적용하려면 시스템 재시작이 필요합니다.
```

주의:

- `/api/system/restart`는 Qt 애플리케이션 재시작이다.
- 애플리케이션 재시작이면 웹에 "애플리케이션 재시작"으로 표시한다.
- OS 재부팅이 필요한 경우(네트워크 설정 변경 등)에는 "시스템 재부팅"으로 표시한다.

---

## 19. 임시 파일 관리

업로드된 복원 파일은 서버의 임시 경로에 저장하고 사용 후 반드시 삭제한다.

정책:

```text
임시 저장 경로: /tmp/swr_restore/{restoreId}/
validate 완료 후 임시 파일 유지 (apply 대기)
apply 완료 또는 실패 후 임시 파일 즉시 삭제
서버 재시작 시 /tmp/swr_restore/ 하위 전체 정리
validate 후 일정 시간(예: 30분) 미apply 시 자동 삭제
```

보안 주의:

```text
ZIP path traversal 방지 필수
  - ZIP 내 파일 경로에 ../ 포함 여부 검사
  - 지정된 임시 경로 외부로 파일이 생성되지 않도록 검증
```

---

## 20. 프론트 화면 구성

유지보수 화면의 복원 영역은 다음 흐름으로 구성한다.

```text
전체 파라메터 복원 Upload

[ 파일 선택 또는 드래그 & 드롭 ]

업로드 후 미리보기:
- 백업 생성일
- 원본 장비명
- 펌웨어 버전
- 스키마 버전

복원 항목 선택:
[✓] 시스템 설정
[✓] 장비 목록
[✓] 레지스터 목록
[ ] 네트워크 설정
[ ] HMI 스케치
[ ] 사용자 계정

경고:
- 네트워크 설정 복원 시 IP 충돌 가능
- 사용자 계정 복원은 초기 버전에서 지원하지 않음

[복원 적용]
```

현재 개발 범위에서는 복원 항목에서 다음 항목을 표시하지 않는다.

```text
폴링 설정
HMI 설정
```

---

## 22. 프론트 상태 흐름

복원 UI 상태는 다음 단계로 관리한다.

```text
IDLE
    ↓ 파일 선택
UPLOADING
    ↓ 업로드 완료
VALIDATING
    ↓ 검증 성공
READY_TO_APPLY
    ↓ 적용 요청
APPLYING
    ↓ 적용 완료
DONE
```

오류 상태:

```text
VALIDATION_FAILED
APPLY_FAILED
```

---

## 23. 백엔드 구현 파일 후보

현재 프로젝트 구조 기준으로 다음 파일에 기능을 추가할 수 있다.

```text
api/ApiServer.cpp
api/ApiServer.h
config/AppConfig.cpp
config/AppConfig.h
data_collection/database/DeviceDatabase.cpp
data_collection/database/DeviceDatabase.h
```

추가 파일 후보:

```text
maintenance/BackupManager.h
maintenance/BackupManager.cpp
maintenance/RestoreManager.h
maintenance/RestoreManager.cpp
maintenance/BackupModels.h
```

역할 분리:

| 파일 | 역할 |
|---|---|
| `ApiServer` | HTTP API route 처리 |
| `BackupManager` | 백업 ZIP 생성 |
| `RestoreManager` | 업로드 파일 검증 및 복원 적용 |
| `DeviceDatabase` | 장비/레지스터 export/import |
| `AppConfig` | config export/import |

---

## 24. API 목록

초기 버전에서 필요한 API는 다음과 같다.

```text
GET  /api/maintenance/backup
POST /api/maintenance/restore/validate
POST /api/maintenance/restore/apply
GET  /api/maintenance/restore/status
POST /api/system/restart
```

---

## 25. 개발 단계

### 단계 1. 백업 데이터 export

- `AppConfig` export JSON 구현
- `DeviceDatabase` devices export 구현
- `DeviceDatabase` registers export 구현
- `manifest.json` 생성
- checksum 생성

### 단계 2. 백업 ZIP 생성

- 임시 디렉토리 생성
- JSON 파일 생성
- checksum 파일 생성
- ZIP 파일 생성
- HTTP 다운로드 응답 구현

### 단계 3. 복원 validate

- ZIP 업로드 수신
- 임시 저장
- 압축 해제
- 필수 파일 존재 확인
- manifest 파싱
- checksum 검증
- firmware/schema compatibility 확인
- 복원 가능 항목 응답

### 단계 4. 복원 apply

- restoreId 확인
- 폴링 상태 확인
- DB transaction 시작
- devices/registers 전체 교체
- 선택된 config 적용
- transaction commit
- 임시 파일 삭제
- restartRequired 반환

### 단계 5. 프론트 연동

- 백업 Download 버튼 구현
- 복원 파일 선택/드래그드롭 구현
- validate API 호출
- 미리보기 표시
- 복원 항목 선택 UI 구현
- apply API 호출
- 재시작 필요 알림 표시

### 단계 6. 테스트

- 같은 장비에서 백업/복원 테스트
- 다른 SWR 장비로 복원 테스트
- 잘못된 ZIP 파일 업로드 테스트
- checksum 오류 테스트
- schemaVersion mismatch 테스트
- 네트워크 설정 제외 테스트
- 복원 실패 시 rollback 테스트

---

## 26. 오류 처리 정책

복원 validate 실패 예:

| 오류 | 처리 |
|---|---|
| ZIP 아님 | 복원 차단 |
| manifest.json 없음 | 복원 차단 |
| devices.json 없음 | 장비 복원 불가 |
| registers.json 없음 | 레지스터 복원 불가 |
| checksum 불일치 | 복원 차단 |
| schemaVersion 불일치 | 기본 차단 |
| firmware major version 불일치 | 복원 차단 |
| 알 수 없는 파일 포함 | 무시 가능 |

복원 apply 실패 예:

| 오류 | 처리 |
|---|---|
| restoreId 없음 | 실패 |
| 임시 파일 만료 | 실패 |
| DB transaction 실패 | rollback |
| config 적용 실패 | rollback |
| 폴링 동작 중 | 복원 차단 |

초기 버전에서는 부분 복원보다 전체 실패 처리가 안전하다.

---

## 27. 보안 정책

백업/복원 API는 관리자 권한에서만 허용한다.

```text
GET  /api/maintenance/backup              ADMIN only
POST /api/maintenance/restore/validate    ADMIN only
POST /api/maintenance/restore/apply       ADMIN only
```

보안 주의사항:

```text
- ZIP path traversal 방지 (파일명에 ../ 포함 여부 검사)
- 임시 파일 사용 후 자동 삭제
- admin 계정 복원 대상 제외 (id=0 고정)
- 인증 토큰/세션 정보 백업 금지
- 모든 백업/복원 작업 로그 기록
```

---

## 28. 로그 기록

다음 이벤트는 Logger를 통해 기록한다.

| 이벤트 | 레벨 | 로그 메시지 예시 |
|---|---|---|
| 백업 다운로드 요청 | INFO | `Backup download requested by admin` |
| 백업 생성 성공 | INFO | `Backup generated: swr_backup_20260624_102431.zip (3 devices, 48 registers)` |
| 백업 생성 실패 | ERROR | `Backup generation failed: disk full` |
| 복원 파일 업로드 | INFO | `Restore file uploaded: swr_backup_20260624_102431.zip` |
| 복원 validate 성공 | INFO | `Restore validate OK: 3 devices, 48 registers (restoreId: tmp_restore_20260624_102431)` |
| 복원 validate 실패 | WARN | `Restore validate failed: checksum mismatch (file: devices.json)` |
| 복원 apply 시작 | INFO | `Restore apply started (restoreId: tmp_restore_20260624_102431)` |
| 복원 성공 | INFO | `Restore completed: devices=3, registers=48` |
| 복원 실패 | ERROR | `Restore failed: DB transaction error` |
| rollback 수행 | WARN | `Restore rollback completed` |
| 복원 후 재시작 요청 | INFO | `System restart requested after restore by admin` |

---

## 29. 1차 구현 범위

초기 구현에서는 다음만 포함한다.

```text
백업:
- manifest.json
- config.json
- devices.json
- registers.json
- users.json (admin 제외)
- hmi.json (빈 데이터)
- checksum.sha256
- ZIP 다운로드

복원:
- ZIP 업로드
- validate
- 미리보기
- 시스템 설정 복원
- 장비 목록 복원
- 레지스터 목록 복원
- 네트워크 설정은 선택 가능하되 기본 OFF
- 사용자 계정 복원 미지원
- 전체 교체 방식
- 복원 후 restartRequired 반환
```

---

## 30. 향후 개선 사항

초기 버전 이후 검토할 개선 항목이다.

```text
- 사용자 계정 복원 지원 (admin 제외 선택적 복원)
- 복원 적용 시 폴링 자동 정지/재시작
- 장비 ID 충돌 처리를 통한 병합 방식 복원
- HMI 스케치 설정 백업/복원
- 복원 이력 관리 API
```

---

## 32. 개발 TODO LIST

### 사전 작업

- [x] ZIP 라이브러리 선정 및 적용
  - Qt6 private API (`QZipWriter` / `QZipReader`) 채택 — 외부 라이브러리 불필요
  - sysroot 확인: `QtCore/6.8.3/QtCore/private/qzipwriter_p.h`, `qzipreader_p.h` 존재
  - `CMakeLists.txt` `target_link_libraries`에 `Qt6::CorePrivate` 추가 완료
  - `find_package`에는 추가하지 않음 (`Qt6::Core` 로드 시 자동 생성되는 타겟)
- [x] `handleDeleteUser` — admin 계정 삭제 차단 추가
  - `username == "admin"` (대소문자 무관) 이면 403 Forbidden 반환
  - 사양서 5.2: admin id=0 고정, 삭제 불가 정책 반영

---

### 신규 파일 생성

#### `maintenance/BackupModels.h`

- [x] `BackupManifest` 구조체 (product, createdAt, sourceDevice, contents) — backupVersion 제거, schemaVersion=SR_SCHEMA_VERSION
- [x] `RestoreOptions` 구조체 (config, network, devices, registers, users)
- [x] `RestorePreview` 구조체 (backupInfo, items별 available/count, warnings 목록)
- [x] `RestoreItemInfo` 구조체 (available, count, warning)

#### `maintenance/BackupManager.h / .cpp`

- [x] `create(DeviceDatabase*)` → ZIP 바이트배열 반환 (다운로드용)
- [x] `buildManifest()` → manifest.json 생성 (SR_VERSION, SR_REVISION, SR_ZCODE, SR_SCHEMA_VERSION 포함)
- [x] `buildConfig()` → config.json 생성 (system, serial, modbusServer, network 포함)
- [x] `buildDevices(DeviceDatabase*)` → devices.json 생성
- [x] `buildRegisters(DeviceDatabase*)` → registers.json 생성
- [x] `buildUsers(DeviceDatabase*)` → users.json 생성 (admin id=0 제외, passwordHash 포함)
- [x] `buildHmi()` → hmi.json 생성 (빈 객체)
- [x] `buildChecksum(파일목록)` → checksum.sha256 생성 (SHA-256, QCryptographicHash 사용)
- [x] ZIP은 QBuffer+QZipWriter로 메모리 내 생성 — 임시 파일 없음
- [x] Logger 호출: 백업 생성 성공

#### `maintenance/RestoreManager.h / .cpp`

- [x] `validate(QByteArray zipData)` → RestorePreview 반환 및 restoreId 발급
  - [x] ZIP 형식 검증 (QZipReader::status)
  - [x] 임시 경로 저장 (`/tmp/swr_restore/{uuid}/`)
  - [x] path traversal 방지 (파일명 내 `../`, `/` 검사)
  - [x] 필수 파일 존재 확인 (manifest.json, checksum.sha256)
  - [x] checksum.sha256 재계산 및 비교 → 불일치 시 차단
  - [x] manifest.json 파싱 및 schemaVersion 호환성 검사
  - [x] 복원 가능 항목 및 건수 집계 (devices, registers, users)
  - [x] 경고 메시지 생성 (hostname 불일치, 네트워크 설정 주의)
  - [x] Logger 호출: validate 성공/실패
- [x] `apply(restoreId, RestoreOptions, DeviceDatabase*, PollingManager*)` → 복원 적용
  - [x] restoreId 유효성 확인 (임시 디렉터리 존재 여부)
  - [x] 폴링 동작 중이면 차단
  - [x] `db->restoreData()` — 단일 트랜잭션으로 clear+insert
  - [x] 선택된 config 항목 적용 (system, serial, modbusServer, loginSecurity, network)
  - [x] 임시 파일 삭제
  - [x] Logger 호출: apply 시작/성공/실패
  - [x] `restartRequired = true` 반환
- [x] `cleanupExpired()` — 30분 초과 임시 세션 삭제

---

### 기존 파일 수정

#### `data_collection/database/DeviceDatabase.h / .cpp`

- [x] `restoreData(restoreDevices, devices, registers, restoreUsers, users)` — 단일 트랜잭션
  - [x] devices 전체 삭제 (CASCADE → registers 자동 삭제)
  - [x] devices INSERT + old_id→new_id 매핑 빌드
  - [x] registers INSERT (deviceId 재매핑)
  - [x] users INSERT (기존 username 스킵, admin 제외)
  - [x] rollback on failure

#### `config/AppConfig.h / .cpp`

- [x] 별도 exportToJson/importFromJson 미추가 — RestoreManager 내부에서 직접 JSON 파싱 후 `saveConfig()` 호출

#### `api/ApiServer.h / .cpp`

- [x] `handleGetBackup(request)` 구현 — admin 권한, ZIP 응답, Content-Disposition 헤더
- [x] `handleRestoreValidate(request)` 구현 — admin 권한, raw ZIP body, RestorePreview JSON 반환
- [x] `handleRestoreApply(request)` 구현 — admin 권한, restoreId+options JSON, 폴링 차단
- [x] `setupRoutes()` — 3개 라우트 등록
  ```
  GET  /api/maintenance/backup
  POST /api/maintenance/restore/validate
  POST /api/maintenance/restore/apply
  ```

#### `CMakeLists.txt`

- [x] `maintenance/BackupModels.h` 추가
- [x] `maintenance/BackupManager.h maintenance/BackupManager.cpp` 추가
- [x] `maintenance/RestoreManager.h maintenance/RestoreManager.cpp` 추가
- [x] `Qt6::CorePrivate` — `target_link_libraries`에 추가 완료 (QZipWriter/QZipReader)

---

API 흐름:
① POST /api/maintenance/restore/validate  ← raw ZIP body
   → restoreId + RestorePreview 반환 (items 수량, warnings)

② POST /api/maintenance/restore/apply
   body: { "restoreId": "...", "options": { "config": true, "network": false, ... } }
   → DB 복원 → config 복원 → { "ok": true, "restartRequired": true }



### 로그 연동 확인

- [ ] 백업 다운로드 요청 — `Logger::info`
- [ ] 백업 생성 성공 — `Logger::info` (장비 수, 레지스터 수 포함)
- [ ] 백업 생성 실패 — `Logger::error`
- [ ] 복원 파일 업로드 — `Logger::info`
- [ ] 복원 validate 성공 — `Logger::info`
- [ ] 복원 validate 실패 — `Logger::warning`
- [ ] 복원 apply 시작 — `Logger::info`
- [ ] 복원 성공 — `Logger::info`
- [ ] 복원 실패 — `Logger::error`
- [ ] rollback 수행 — `Logger::warning`
- [ ] 복원 후 재시작 요청 — `Logger::info`

---

### 테스트 항목

- [ ] 같은 장비에서 백업 → 복원 정상 동작 확인
- [ ] 다른 SWR 장비로 복원 (hostname 불일치 경고 포함)
- [ ] 잘못된 형식 파일 업로드 시 차단 확인
- [ ] checksum 불일치 파일 복원 차단 확인
- [ ] schemaVersion 불일치 시 복원 차단 확인
- [ ] 네트워크 설정 기본 OFF 확인
- [ ] 폴링 중 복원 차단 확인
- [ ] DB transaction 실패 시 rollback 확인 (현재 설정 유지)
- [ ] admin 계정 삭제 차단 확인
- [ ] path traversal 공격 방어 확인

---

## 31. 최종 정책 요약

```text
백업 파일은 ZIP 패키지로 생성한다.
백업 파일에는 manifest, config, devices, registers, users, hmi, checksum을 포함한다.
현재 개발 범위에서 HMI 데이터는 빈 데이터로 포함한다.
네트워크 설정은 기본 복원 대상에서 제외하고 사용자가 명시적으로 선택해야 한다.
admin 계정은 백업/복원하지 않으며 id=0으로 항상 고정한다.
복원 전 validate API로 파일과 호환성을 검증한다.
복원 중 폴링이 동작 중이면 적용을 차단한다.
DB 변경은 transaction으로 처리하고 실패 시 rollback한다.
복원 완료 후 restartRequired를 반환하고 웹에서 재시작 필요 알림을 표시한다.
백업/복원 API는 관리자 권한에서만 허용한다.
모든 백업/복원 작업은 Logger에 기록한다.
```


