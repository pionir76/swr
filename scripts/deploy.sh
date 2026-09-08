#!/bin/bash
# deploy.sh — 플래시 직후 최초 1회 실행: 빌드 + 전체 배포 + 서비스 등록
#
# 사용법:
#   ./scripts/deploy.sh
#
# 사전 조건:
#   - 장치가 192.168.0.150 으로 접근 가능한 상태
#   - SDK: /opt/imx6ull-core-sdk/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi
#   - 백업 파일: DB Backup/etc-swr/config.json, DB Backup/var-lib-swr/smartroute.db

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/i_MX6ULL_Qt6_SDK-Release"
SDK_ENV="/opt/imx6ull-core-sdk/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi"
DEVICE="192.168.0.150"
DEVICE_USER="root"
SSH_KEY="$HOME/.ssh/id_ed25519"

# SSH 비밀번호 헬퍼 (최초 키 등록 전까지 사용)
ASKPASS_SCRIPT="/tmp/swr_askpass_$$.sh"
echo '#!/bin/sh
echo "root"' > "$ASKPASS_SCRIPT"
chmod +x "$ASKPASS_SCRIPT"
cleanup() { rm -f "$ASKPASS_SCRIPT"; }
trap cleanup EXIT

ssh_cmd() {
    ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 \
        -o PreferredAuthentications=publickey \
        "${DEVICE_USER}@${DEVICE}" "$@"
}

ssh_pw() {
    DISPLAY=:0 SSH_ASKPASS="$ASKPASS_SCRIPT" SSH_ASKPASS_REQUIRE=force \
    ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 \
        "${DEVICE_USER}@${DEVICE}" "$@"
}

echo "========================================"
echo "  SWR Deploy — $(date '+%Y-%m-%d %H:%M:%S')"
echo "========================================"

# 1. SSH known_hosts 갱신
echo "[1/7] SSH known_hosts 갱신..."
ssh-keygen -f "$HOME/.ssh/known_hosts" -R "$DEVICE" 2>/dev/null || true
DISPLAY=:0 SSH_ASKPASS="$ASKPASS_SCRIPT" SSH_ASKPASS_REQUIRE=force \
    ssh-copy-id -o StrictHostKeyChecking=no -i "${SSH_KEY}.pub" \
    "${DEVICE_USER}@${DEVICE}" 2>&1 | grep -v "^Warning\|^/usr"
echo "    SSH 키 등록 완료"

# 2. 빌드
echo "[2/7] 크로스 컴파일..."
source "$SDK_ENV"
cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1 | tail -3
echo "    빌드 완료"

# 3. 장치 디렉터리 생성
echo "[3/7] 장치 디렉터리 생성..."
ssh_cmd 'mkdir -p /etc/swr /var/lib/swr /var/log/swr /trend_data'
echo "    완료"

# 4. 바이너리 배포
echo "[4/7] 바이너리 배포..."
scp -o StrictHostKeyChecking=no "$BUILD_DIR/swr" "${DEVICE_USER}@${DEVICE}:/usr/bin/swr"
ssh_cmd 'chmod +x /usr/bin/swr'
echo "    완료"

# 5. config + DB 배포
echo "[5/7] 설정/DB 배포..."
scp -o StrictHostKeyChecking=no \
    "$PROJECT_DIR/DB Backup/etc-swr/config.json" \
    "${DEVICE_USER}@${DEVICE}:/etc/swr/config.json"
scp -o StrictHostKeyChecking=no \
    "$PROJECT_DIR/DB Backup/var-lib-swr/smartroute.db" \
    "${DEVICE_USER}@${DEVICE}:/var/lib/swr/smartroute.db"
echo "    완료"

# 6. systemd 서비스 등록
echo "[6/7] systemd 서비스 등록..."
scp -o StrictHostKeyChecking=no \
    "$SCRIPT_DIR/swr.service" \
    "${DEVICE_USER}@${DEVICE}:/etc/systemd/system/swr.service"
ssh_cmd 'systemctl daemon-reload && systemctl enable swr && systemctl restart swr'
echo "    완료"

# 7. 동작 확인
echo "[7/7] 동작 확인..."
sleep 2
ssh_cmd 'systemctl is-active swr && journalctl -u swr -n 5 --no-pager'

echo ""
echo "========================================"
echo "  배포 완료"
echo "  API: http://${DEVICE}:8080"
echo "========================================"
