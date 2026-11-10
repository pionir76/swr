#!/bin/bash
# redeploy.sh — 개발 중 코드 수정 후 빌드 + 장치 재시작
#
# 사용법:
#   ./scripts/redeploy.sh          # 빌드 + 배포 + 재시작
#   ./scripts/redeploy.sh --clean  # 클린 빌드 + 배포 + 재시작
#
# SDK는 장치 이미지와 같은 Qt 버전이어야 한다(앱이 Qt 비공개 API QZipWriter를 쓰므로
# 버전이 다르면 실행 중 문제가 생길 수 있음). 기본값은 wrynose 이미지용 SDK(Qt 6.11).
# 다른 SDK로 빌드할 때는 SDK_ENV와 BUILD_DIR을 함께 지정한다.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build/wrynose-Release}"
SDK_ENV="${SDK_ENV:-/opt/imx6ull-core-sdk-wrynose/environment-setup-cortexa7t2hf-neon-poky-linux-gnueabi}"
DEVICE="192.168.0.150"
DEVICE_USER="root"

CLEAN=0
[[ "${1}" == "--clean" ]] && CLEAN=1

echo "[1/3] 빌드... (SDK: $SDK_ENV, 빌드 폴더: $BUILD_DIR)"
source "$SDK_ENV"
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
fi
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
