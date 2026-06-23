# 사용자 관리 정책 구현 TODO

정책 문서: `smartroute_user_management_policy.md`

---

## 구현 항목

- [x] **1. `users` 테이블 스키마 확장**
  - `status TEXT` (ACTIVE/DISABLED/LOCKED) 추가 — `enabled INTEGER` 대체
  - `failed_login_count INTEGER DEFAULT 0` 추가
  - `locked_until TEXT` 추가
  - `last_login_ip TEXT` 추가
  - `description TEXT` 추가
  - `UserInfo` 구조체 및 DB 메서드 반영

- [x] **2. `validateUser()` 로직 재작성**
  - DISABLED 계정 차단
  - LOCKED 상태 즉시 차단 — 자동 해제 없음, 관리자 수동 해제만 가능
  - 비밀번호 실패 시 `failed_login_count` 증가
  - 임계값(5회) 도달 시 LOCKED 처리 (`locked_until` 미사용, 컬럼 제거)
  - 성공 시 `failed_login_count` 초기화, `last_login_at` / `last_login_ip` 저장
  - 실패 응답에 잠금 여부 구분 메시지 포함

- [x] **3. 사용자 관리 API 추가**
  - `PUT /api/users/:username` — 사용자 정보 수정 (displayName, description, role)
  - `PUT /api/users/:username/password` — 비밀번호 변경 (일반 사용자: currentPassword 검증 필요, admin: 검증 없이 변경 가능)
  - `PUT /api/users/:username/status` — 상태 전환 (`active` | `locked` | `disabled` 3가지 단일 상태)
    - `→ active` : failedLoginCount 초기화, lockedUntil 초기화
    - `→ locked` : lockedUntil=NULL (무기한, 자동 해제 없음)
    - `→ disabled` : 그대로 유지
  - 마지막 active admin → locked/disabled 전환 시 409 차단 (최소 1명 active admin 보장)

- [x] **4. 로그인 보안 정책 저장 / 조회**
  - `AppConfig`에 `LoginSecurityConfig` 추가 (maxFailedAttempts, sessionTimeoutMinutes, minPasswordLength, autoLogout)
  - `GET /api/users/security-policy`
  - `PUT /api/users/security-policy` (각 필드 범위 검증 포함)
  - `validateUser()`에서 정책값 참조 (런타임에 config 파일에서 로드)

- [ ] **5. 역할 기반 접근 제어 (RBAC)**
  - `isAuthenticated()` 외에 `hasRole()` 헬퍼 추가
  - USER 역할: 장비/레지스터 변경, 사용자 관리, 시스템 설정 차단
  - MANAGER 역할: 사용자 관리, 공장 초기화, 시스템 재시작 차단
  - 각 API 핸들러에 역할 체크 적용

- [ ] **6. 세션 타임아웃**
  - `m_sessionUsers` 맵에 `lastActivity` 타임스탬프 추가
  - `QTimer`로 주기적 만료 세션 정리 (기본 30분)
  - API 요청마다 `lastActivity` 갱신
  - 만료된 세션 자동 무효화
