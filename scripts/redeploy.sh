#!/bin/bash
# redeploy.sh — 개발 중 코드 수정 후 빌드 + 장치 재시작
#
# 사용법:
#   ./scripts/redeploy.sh          # 빌드 + 배포 + 재시작
#   ./scripts/redeploy.sh --clean  # 클린 빌드 + 배포 + 재시작

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/i_MX6ULL_Qt6_SDK-Release"
SDK_ENV="/opt/imx6ull-core-sdk/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi"
DEVICE="192.168.0.150"
DEVICE_USER="root"

CLEAN=0
[[ "${1}" == "--clean" ]] && CLEAN=1

echo "[1/3] 빌드..."
source "$SDK_ENV"
if [[ $CLEAN -eq 1 ]]; then
    cmake --build "$BUILD_DIR" --clean-first --parallel "$(nproc)" 2>&1 | tail -5
else
    cmake --build "$BUILD_DIR" --parallel "$(nproc)" 2>&1 | tail -3
fi

echo "[2/3] 서비스 중지 + 바이너리 배포..."
ssh -o StrictHostKeyChecking=no "${DEVICE_USER}@${DEVICE}" 'systemctl stop swr'
scp -o StrictHostKeyChecking=no \
    "$BUILD_DIR/swr" \
    "${DEVICE_USER}@${DEVICE}:/usr/bin/swr"

echo "[3/3] 서비스 시작..."
ssh -o StrictHostKeyChecking=no "${DEVICE_USER}@${DEVICE}" \
    'systemctl start swr && sleep 1 && systemctl is-active swr'

echo ""
echo "완료 — http://${DEVICE}:8080"
