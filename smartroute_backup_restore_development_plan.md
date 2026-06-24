# SmartRoute 백업/복원 개발 플랜

## 1. 목적

이 문서는 SmartRoute(SWR) 장비의 설정 백업 및 복원 기능 개발 방향을 정의한다.

목표는 하나의 SWR 장비에서 구성한 설정을 백업 파일로 다운로드하고, 다른 SWR 장비에 업로드하여 동일한 운영 설정을 안정적으로 적용할 수 있도록 하는 것이다.

백업/복원 기능은 단순 파일 복사가 아니라 다음 조건을 만족해야 한다.

- 장비 목록, 레지스터 목록, 시스템 설정을 안전하게 이관한다.
- 네트워크 설정과 사용자 계정처럼 장비 고유성이 강한 항목은 기본 복원 대상에서 제외한다.
- 복원 전 파일 검증과 미리보기 단계를 제공한다.
- 적용 전 현재 설정을 자동 백업한다.
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
└── checksum.sha256
```

현재 개발 범위에서는 **폴링 설정**과 **HMI 설정**은 별도 백업 항목으로 포함하지 않는다.  
현재 별도로 관리할 내용이 없으므로 백업/복원 대상에서 제외한다.

---

## 4. 백업 포함 항목

### 4.1 포함 대상

| 파일 | 내용 |
|---|---|
| `manifest.json` | 백업 파일 메타 정보 |
| `config.json` | 시스템 설정 정보 |
| `devices.json` | 장비 목록 |
| `registers.json` | 레지스터 목록 |
| `checksum.sha256` | 백업 파일 무결성 검증 정보 |

### 4.2 제외 대상

다음 항목은 초기 버전의 백업/복원 대상에서 제외한다.

| 항목 | 제외 이유 |
|---|---|
| 폴링 설정 | 현재 별도 설정 항목 없음 |
| HMI 설정 | 현재 별도 설정 항목 없음 |
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

사용자 계정은 기본 백업/복원 대상에서 제외한다.

이유:

- 현재 장비의 관리자 계정이 덮어씌워질 수 있음
- 로그인 불가 상태가 발생할 수 있음
- 장비별 운영자 계정이 다를 수 있음
- 비밀번호 해시와 계정 정책 이관 시 보안 검토가 필요함

권장 정책:

```text
사용자 계정은 기본 백업/복원 대상에서 제외
로그인 보안 정책만 별도 백업 대상으로 검토 가능
초기 버전에서는 사용자 계정 복원 미지원
```

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
    "serialNumber": "SWR-0001",
    "firmwareVersion": "1.2.0",
    "schemaVersion": 3
  },
  "contents": {
    "config": true,
    "devices": true,
    "registers": true,
    "network": true,
    "users": false,
    "polling": false,
    "hmi": false
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
| `serialNumber` | 백업 원본 장비 식별 번호 |
| `firmwareVersion` | 백업 생성 당시 펌웨어 버전 |
| `schemaVersion` | DB 또는 설정 스키마 버전 |
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
  "metadata": {
    "version": 1,
    "revision": 12,
    "lastUpdate": "2026-06-24"
  },
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

필수 정보:

- 장비 ID
- 태그명
- 표시명
- 원본 주소
- 레지스터 타입
- 데이터 타입
- 길이
- 단위
- 스케일
- 읽기/쓰기 모드
- 활성 여부

---

## 10. checksum.sha256

백업 파일 무결성 검증을 위해 각 JSON 파일에 대한 SHA-256 체크섬을 기록한다.

예시:

```text
a7b9f2...  manifest.json
c4d1aa...  config.json
3f88bc...  devices.json
91aa32...  registers.json
```

복원 시 서버는 checksum을 다시 계산하여 파일 변조 여부를 확인한다.

정책:

```text
checksum 불일치 시 복원 차단
필수 파일 누락 시 복원 차단
알 수 없는 파일이 있어도 무시 가능
```

---

## 11. 백업 다운로드 API

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
6. checksum.sha256 생성
7. ZIP 패키지 생성
8. 파일 다운로드 응답 반환
```

---

## 12. 복원 Upload 절차

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

## 13. 복원 검증 API

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
    "firmwareVersion": "1.2.0",
    "schemaVersion": 3
  },
  "compatibility": {
    "firmware": "compatible",
    "schema": "compatible"
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
    "network": {
      "available": true,
      "restoreDefault": false,
      "warning": "네트워크 설정 복원 시 IP 충돌이 발생할 수 있습니다."
    },
    "users": {
      "available": false,
      "reason": "사용자 계정 복원은 지원하지 않습니다."
    }
  },
  "warnings": [
    "백업 파일의 Hostname이 현재 장비와 다릅니다.",
    "네트워크 설정은 기본 복원 대상에서 제외됩니다."
  ]
}
```

---

## 14. 복원 적용 API

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
    "network": false,
    "users": false
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

## 15. 복원 적용 서버 처리 흐름

```text
1. restoreId 유효성 확인
2. 임시 업로드 파일 존재 확인
3. manifest 재검증
4. checksum 재검증
5. 현재 설정 자동 백업 생성
6. 폴링 동작 중이면 일시 정지 또는 복원 차단
7. DB transaction 시작
8. 기존 장비/레지스터 데이터 삭제
9. 백업 장비/레지스터 데이터 삽입
10. 선택된 config 항목 적용
11. transaction commit
12. 필요 시 restartRequired = true 반환
```

실패 시:

```text
1. transaction rollback
2. 오류 로그 기록
3. 현재 설정 유지
4. 프론트에 실패 사유 반환
```

---

## 16. 적용 전 현재 설정 자동 백업

복원 적용 전 현재 장비의 설정을 자동으로 백업한다.

저장 위치 예시:

```text
/var/lib/swr/backups/pre_restore_20260624_103000.zip
```

정책:

```text
복원 적용 전 자동 백업 생성
자동 백업 실패 시 복원 차단
최근 자동 백업 파일 개수 제한 가능
예: 최근 5개 유지
```

---

## 17. 복원 방식

초기 버전에서는 전체 교체 방식을 사용한다.

### 17.1 전체 교체 방식

```text
기존 장비 목록 삭제
기존 레지스터 목록 삭제
백업 파일의 장비/레지스터 목록으로 새로 구성
```

장점:

- 장비 복제 목적에 적합
- 구현이 단순
- 백업 원본과 대상 장비 구성이 동일해짐

단점:

- 기존 대상 장비의 개별 수정사항이 사라짐

### 17.2 병합 방식

초기 버전에서는 지원하지 않는다.

병합 방식은 충돌 정책이 복잡하므로 추후 확장 항목으로 둔다.

---

## 18. 복원 중 폴링 처리

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

## 19. 재시작 정책

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

- 현재 구현의 `/api/system/restart`가 Qt 애플리케이션 재시작인지, Linux 시스템 재부팅인지 명확히 구분해야 한다.
- 애플리케이션 재시작이면 “애플리케이션 재시작”으로 표시한다.
- OS 재부팅이면 “시스템 재부팅”으로 표시한다.

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

## 21. 프론트 상태 흐름

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

## 22. 백엔드 구현 파일 후보

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

## 23. API 목록

초기 버전에서 필요한 API는 다음과 같다.

```text
GET  /api/maintenance/backup
POST /api/maintenance/restore/validate
POST /api/maintenance/restore/apply
GET  /api/maintenance/restore/status
POST /api/system/restart
```

선택 API:

```text
GET  /api/maintenance/backups
POST /api/maintenance/rollback
```

---

## 24. 개발 단계

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
- 현재 설정 자동 백업
- 폴링 상태 확인
- DB transaction 시작
- devices/registers 전체 교체
- 선택된 config 적용
- transaction commit
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

## 25. 오류 처리 정책

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
| 자동 백업 실패 | 복원 차단 |
| DB transaction 실패 | rollback |
| config 적용 실패 | rollback 또는 부분 실패 처리 |
| 폴링 동작 중 | 복원 차단 |

초기 버전에서는 부분 복원보다 전체 실패 처리가 안전하다.

---

## 26. 보안 정책

백업/복원 API는 관리자 권한에서만 허용한다.

권장 정책:

```text
GET  /api/maintenance/backup              ADMIN only
POST /api/maintenance/restore/validate    ADMIN only
POST /api/maintenance/restore/apply       ADMIN only
POST /api/maintenance/rollback            ADMIN only
```

보안 주의:

- 업로드 파일 크기 제한
- ZIP path traversal 방지
- 임시 파일 자동 삭제
- 사용자 계정 기본 복원 제외
- 인증 토큰/세션 정보 백업 금지
- 관리자 작업 로그 기록

---

## 27. 파일 크기 제한

초기 권장값:

```text
최대 업로드 크기: 20MB
최대 압축 해제 크기: 100MB
허용 확장자: .zip
허용 파일: manifest.json, config.json, devices.json, registers.json, checksum.sha256
```

ZIP 보안 처리:

```text
상대 경로만 허용
../ 포함 경로 차단
절대 경로 차단
심볼릭 링크 차단
```

---

## 28. 로그 기록

다음 이벤트는 로그로 기록한다.

- 백업 다운로드 요청
- 백업 생성 성공/실패
- 복원 파일 업로드
- 복원 validate 성공/실패
- 복원 apply 시작
- 자동 백업 생성 성공/실패
- 복원 성공/실패
- 복원 후 재시작 요청
- rollback 수행

로그 예:

```text
2026-06-24 10:24:31 admin BACKUP_DOWNLOAD success swr_backup_20260624_102431.zip
2026-06-24 10:30:12 admin RESTORE_VALIDATE success source=swr-field-01 schema=3
2026-06-24 10:31:02 admin RESTORE_APPLY success devices=24 registers=1284 restartRequired=true
```

---

## 29. 1차 구현 범위

초기 구현에서는 다음만 포함한다.

```text
백업:
- manifest.json
- config.json
- devices.json
- registers.json
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

초기 구현에서 제외:

```text
- 폴링 설정 복원
- HMI 설정 복원
- 사용자 계정 복원
- 병합 복원
- schema migration
- rollback UI
```

---

## 30. 향후 확장 항목

추후 다음 기능을 확장할 수 있다.

- 병합 복원
- schema migration
- rollback UI
- 백업 파일 암호화
- 백업 파일 서명
- 사용자 계정 선택 복원
- 자동 정기 백업
- 외부 USB 저장 백업
- 백업 이력 관리
- 부분 백업
- 원격 SWR 간 직접 복제
- 복원 전 차이점 비교 화면

---

## 31. 최종 정책 요약

```text
백업 파일은 ZIP 패키지로 생성한다.
백업 파일에는 manifest, config, devices, registers, checksum을 포함한다.
현재 개발 범위에서 polling 설정과 HMI 설정은 제외한다.
네트워크 설정은 기본 복원 대상에서 제외하고 사용자가 명시적으로 선택해야 한다.
사용자 계정은 초기 버전에서 복원하지 않는다.
복원 전 validate API로 파일과 호환성을 검증한다.
복원 적용 전 현재 설정을 자동 백업한다.
복원은 초기 버전에서 전체 교체 방식으로 수행한다.
복원 중 폴링이 동작 중이면 적용을 차단한다.
DB 변경은 transaction으로 처리하고 실패 시 rollback한다.
복원 완료 후 restartRequired를 반환하고 웹에서 재시작 필요 알림을 표시한다.
백업/복원 API는 관리자 권한에서만 허용한다.
```
