# API 권한 정책

## 권한 체계

`requireAuth(request, RequiredRole)` 한 곳에서 인증·인가를 처리한다.

| RequiredRole | 대상 | 실패 응답 |
|---|---|---|
| `Any` (기본값) | 로그인한 모든 사용자 | 401 Unauthorized |
| `ManagerOrAbove` | Manager 또는 Admin | 403 Forbidden |
| `AdminOnly` | Admin 전용 | 403 Forbidden |

**처리 순서:**
1. Authorization 헤더 없음 → 401
2. 세션에 토큰 없음(미로그인·만료) → 401
3. `Any` → 통과
4. DB 사용자 조회 실패 → 401
5. 권한 부족 → 403
6. 통과

---

## 특이 사항

- **`handleLogout`**: `requireAuth` 없음 — 만료 토큰으로도 로그아웃 가능해야 하므로 의도적
- **`handlePutUserPassword`**: `Any`로 진입 후 내부에서 isSelf/isAdmin 분기
  - 본인 → `currentPassword` 검증 필요
  - Admin → `currentPassword` 없이 변경 가능
  - 타인(Manager 포함) → 403 Forbidden
- **Default Admin(id=0)**: `handlePutUser`, `handleDeleteUser`, `handlePutUserStatus` 에서 id==0 이면 수정·삭제·상태변경 모두 차단 (403)

---

## 엔드포인트별 권한

### RequiredRole::Any — 로그인한 모든 사용자

| 메서드 | 경로 | 핸들러 |
|---|---|---|
| GET | `/api/session` | `handleSession` |
| GET | `/api/dashboard` | `handleGetDashboard` |
| GET | `/api/polling/status` | `handleGetPollingStatus` |
| POST | `/api/polling/start` | `handleStartPolling` |
| POST | `/api/polling/stop` | `handleStopPolling` |
| GET | `/api/devices` | `handleGetDevices` |
| GET | `/api/devices/status` | `handleGetDeviceStatus` |
| GET | `/api/devices/:id/registers` | `handleGetRegisters` |
| GET | `/api/registers/unified-id/check` | `handleCheckUnifiedId` |
| GET | `/api/registers/realtime` | `handleGetRealtime` |
| GET | `/api/logs` | `handleGetLogs` |
| GET | `/api/users` | `handleGetUsers` |
| PUT | `/api/users/:username/password` | `handlePutUserPassword` |
| GET | `/api/config` | `handleGetConfig` |
| GET | `/api/system/info` | `handleGetSystemInfo` |
| GET | `/api/system/resources` | `handleGetSystemResources` |

### RequiredRole::ManagerOrAbove — Manager 또는 Admin

| 메서드 | 경로 | 핸들러 |
|---|---|---|
| POST | `/api/devices` | `handlePostDevice` |
| PUT | `/api/devices/:id` | `handlePutDevice` |
| DELETE | `/api/devices/:id` | `handleDeleteDevice` |
| POST | `/api/devices/:id/registers` | `handlePostRegister` |
| PUT | `/api/registers/:id` | `handlePutRegister` |
| DELETE | `/api/registers/:id` | `handleDeleteRegister` |
| POST | `/api/registers/:id/write` | `handleWriteRegister` |
| POST | `/api/users` | `handlePostUser` |
| PUT | `/api/users/:username` | `handlePutUser` |
| DELETE | `/api/users/:username` | `handleDeleteUser` |
| PUT | `/api/users/:username/status` | `handlePutUserStatus` |
| GET | `/api/users/login-history` | `handleGetLoginHistory` |
| DELETE | `/api/users/login-history` | `handleDeleteLoginHistory` |
| GET | `/api/users/security-policy` | `handleGetSecurityPolicy` |
| PUT | `/api/users/security-policy` | `handlePutSecurityPolicy` |
| POST | `/api/config/reset` | `handlePostConfigReset` |
| PUT | `/api/config/network` | `handlePutConfigNetwork` |
| PUT | `/api/config/serial` | `handlePutConfigSerial` |
| PUT | `/api/config/system` | `handlePutConfigSystem` |
| PUT | `/api/config/modbus-server` | `handlePutConfigModbusServer` |
| POST | `/api/system/restart` | `handlePostRestart` |

### RequiredRole::AdminOnly — Admin 전용

| 메서드 | 경로 | 핸들러 |
|---|---|---|
| GET | `/api/maintenance/backup` | `handleGetBackup` |
| POST | `/api/maintenance/restore/validate` | `handleRestoreValidate` |
| POST | `/api/maintenance/restore/apply` | `handleRestoreApply` |
| POST | `/api/maintenance/factory-reset` | `handlePostFactoryReset` |

### 인증 불필요

| 메서드 | 경로 | 핸들러 |
|---|---|---|
| POST | `/api/login` | `handleLogin` |
| POST | `/api/logout` | `handleLogout` |
