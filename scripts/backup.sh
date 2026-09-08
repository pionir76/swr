#!/bin/bash
# backup.sh — 장치 현재 상태를 DB Backup/ 에 스냅샷
#
# 사용법:
#   ./scripts/backup.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEVICE="192.168.0.150"
DEVICE_USER="root"

echo "장치 데이터 백업 중 (${DEVICE})..."

scp -o StrictHostKeyChecking=no \
    "${DEVICE_USER}@${DEVICE}:/etc/swr/config.json" \
    "$PROJECT_DIR/DB Backup/etc-swr/config.json"

scp -o StrictHostKeyChecking=no \
    "${DEVICE_USER}@${DEVICE}:/var/lib/swr/smartroute.db" \
    "$PROJECT_DIR/DB Backup/var-lib-swr/smartroute.db"

# WAL 체크포인트 후 로그 DB 백업
ssh -o StrictHostKeyChecking=no "${DEVICE_USER}@${DEVICE}" \
    'sqlite3 /var/log/swr/smartroute_log.db "PRAGMA wal_checkpoint(FULL);" 2>/dev/null || true'
scp -o StrictHostKeyChecking=no \
    "${DEVICE_USER}@${DEVICE}:/var/log/swr/smartroute_log.db" \
    "$PROJECT_DIR/DB Backup/var-log-swr/smartroute_log.db" 2>/dev/null || true

echo "백업 완료:"
echo "  DB Backup/etc-swr/config.json"
echo "  DB Backup/var-lib-swr/smartroute.db"
echo "  DB Backup/var-log-swr/smartroute_log.db"
