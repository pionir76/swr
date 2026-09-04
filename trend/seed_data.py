#!/usr/bin/env python3
"""
SmartRoute Trend DB Seeder — frontend integration test용 샘플 데이터 생성.

# 데이터 구조
  저장값  : rawRegisters[0] 그대로 저장 (quint16, 0~65535)
             프론트에서 TrendConfig의 scale 적용 후 표시 (fixed point 개념)
             예) v=430, scale=0.1 → 표시값 43.0

  레코딩 기간: 현재 시간 기준 RECORDING_DAYS일 전 ~ 현재
  trend_data    : SAMPLE_INTERVAL마다 수집된 raw quint16 값
  trend_data_5m : 5분 구간별 avg(반올림 정수) / min / max (quint16)
  trend_data_10m: 10분 구간별 avg(반올림 정수) / min / max (quint16)

# 보존 기간 정책 (실제 운영)
  RECORDING_DAYS < 365  → 세 테이블 모두 동일 기간 (raw 데이터가 1년 미만)
  RECORDING_DAYS >= 365 → raw:5m:10m = 1:3:5 비율 적용
    예) raw 365일, 5m 1095일, 10m 1825일

사용법:
  python3 seed_data.py

생성 후 장치에 배포:
  scp trend_seed.db root@192.168.0.150:/var/lib/swr/trend.db

채널 ID(CHANNELS)는 실제 unified_register_id 값으로 수정하세요.
"""

import sqlite3, math, random, time, os

# ── 설정 ──────────────────────────────────────────────────────────────────────
OUTPUT_DB       = "trend_seed.db"
DEVICE_IP       = "192.168.0.150"
CHANNELS        = [40003, 40004, 40006, 40009, 40010, 40011, 40012, 40013,
                   40014, 40015, 40016, 40017, 40018, 40020, 40021, 40024]
SAMPLE_INTERVAL = 10   # 초 (10 | 30 | 60)
RECORDING_DAYS  = 21   # raw 기준 기간 (실제 운영: 365일)

# 보존 기간 자동 계산
# RECORDING_DAYS < 365 이면 세 테이블 동일 기간 사용
RAW_DAYS     = RECORDING_DAYS
AGG_5M_DAYS  = RECORDING_DAYS if RECORDING_DAYS < 365 else RECORDING_DAYS * 3
AGG_10M_DAYS = RECORDING_DAYS if RECORDING_DAYS < 365 else RECORDING_DAYS * 5
# ─────────────────────────────────────────────────────────────────────────────

# 채널 프로파일: {reg_id: profile}
#   아날로그: (base, amp, period_h, noise_amp) — raw quint16 단위
#   이진(운전상태): ("on",)   — 평시 1, 3% 확률 0
#   이진(경보상태): ("off",)  — 평시 0, 1% 확률 1
_PROFILES = {
    # ── 아날로그 (raw quint16 단위) ──────────────────────────────────────────
    40003: (500, 300, 24, 20),  # 메인룸 출력값     scale=0.1 → 20.0~80.0 %
    40009: (  0, 500, 12, 30),  # Liquid Outlet Temp (signed, 임시 양수 범위)
    40010: ( 80,  40,  8,  3),  # Superheat         scale=0.1 → 4.0~12.0 °C
    40011: (400, 150,  6, 10),  # Suction Pressure  scale=0.01 → 2.5~5.5 bar
    40012: (300, 500, 12, 30),  # Saturation Temp   (signed, 임시 양수 범위)
    40013: (500, 800,  8, 50),  # 흡입 온도          (signed, 임시 양수 범위)
    40014: (450, 350,  6, 30),  # 밸브 개도율        scale=0.1 → 10.0~80.0 %
    40017: (490,   3, 24,  1),  # 액 출구 온도 설정값 (signed, 준고정)
    40018: ( 80,   3, 24,  1),  # 과열도 설정값      scale=0.1 → 7.7~8.3 °C
    40020: (400, 300,  8, 20),  # 수동 밸브 위치     scale=0.1 → 10.0~70.0 %
    40021: ( 60,   1, 24,  0),  # 고온실 설정온도    scale=1.0 → ~60 °C
    # ── 이진 상태 ────────────────────────────────────────────────────────────
    40004: ("on",),   # 메인룸 운전상태
    40015: ("on",),   # 시스템 상태
    40024: ("on",),   # 고온실 운전상태
    40006: ("off",),  # 메인룸 경보상태
    40016: ("off",),  # 알람 상태
}

def _validate():
    missing = [ch for ch in CHANNELS if ch not in _PROFILES]
    if missing:
        raise SystemExit(f"[오류] _PROFILES에 없는 채널 ID: {missing}")

def sensor_value(ch, ts):
    """채널별 raw quint16 시뮬레이션 값 생성."""
    random.seed(ch * 1000 + ts // SAMPLE_INTERVAL)
    prof = _PROFILES[ch]
    if prof[0] == "on":
        return 0 if random.random() < 0.03 else 1
    if prof[0] == "off":
        return 1 if random.random() < 0.01 else 0
    base, amp, period_h, noise_amp = prof
    t = ts / 3600.0
    noise = random.randint(-noise_amp, noise_amp) if noise_amp > 0 else 0
    phase = (ch % 100) * 0.17
    val = int(round(base + amp * math.sin(2 * math.pi * t / period_h + phase) + noise))
    return max(0, min(65535, val))  # quint16 범위 클램프

def create_schema(cur):
    cur.executescript("""
        PRAGMA journal_mode=WAL;

        CREATE TABLE IF NOT EXISTS trend_data (
            id     INTEGER PRIMARY KEY AUTOINCREMENT,
            reg_id INTEGER NOT NULL,
            ts     INTEGER NOT NULL,
            value  INTEGER NOT NULL    -- raw quint16 (0~65535)
        );
        CREATE INDEX IF NOT EXISTS idx_trend_reg_ts ON trend_data (reg_id, ts);

        CREATE TABLE IF NOT EXISTS trend_data_5m (
            reg_id  INTEGER NOT NULL,
            ts      INTEGER NOT NULL,
            avg_val INTEGER NOT NULL,  -- raw avg (반올림, quint16)
            min_val INTEGER NOT NULL,  -- raw min (quint16)
            max_val INTEGER NOT NULL,  -- raw max (quint16)
            PRIMARY KEY (reg_id, ts)
        );

        CREATE TABLE IF NOT EXISTS trend_data_10m (
            reg_id  INTEGER NOT NULL,
            ts      INTEGER NOT NULL,
            avg_val INTEGER NOT NULL,  -- raw avg (반올림, quint16)
            min_val INTEGER NOT NULL,  -- raw min (quint16)
            max_val INTEGER NOT NULL,  -- raw max (quint16)
            PRIMARY KEY (reg_id, ts)
        );
    """)

def seed_raw(cur, now, days):
    start = ((now - days * 86400) // SAMPLE_INTERVAL) * SAMPLE_INTERVAL
    rows = []
    ts = start
    while ts <= now:
        for ch in CHANNELS:
            rows.append((ch, ts, sensor_value(ch, ts)))
        ts += SAMPLE_INTERVAL
        if len(rows) >= 10000:
            cur.executemany("INSERT INTO trend_data (reg_id,ts,value) VALUES (?,?,?)", rows)
            rows.clear()
    if rows:
        cur.executemany("INSERT INTO trend_data (reg_id,ts,value) VALUES (?,?,?)", rows)
    return (now - start) // SAMPLE_INTERVAL * len(CHANNELS)

def seed_5m(cur, now, days):
    start = ((now - days * 86400) // 300) * 300
    rows = []
    ts = start
    while ts < now:
        bucket_end = ts + 300
        for ch in CHANNELS:
            pts = [sensor_value(ch, ts + i * SAMPLE_INTERVAL)
                   for i in range(300 // SAMPLE_INTERVAL)]
            rows.append((ch, ts, round(sum(pts) / len(pts)), min(pts), max(pts)))
        ts = bucket_end
        if len(rows) >= 5000:
            cur.executemany(
                "INSERT OR IGNORE INTO trend_data_5m "
                "(reg_id,ts,avg_val,min_val,max_val) VALUES (?,?,?,?,?)", rows)
            rows.clear()
    if rows:
        cur.executemany(
            "INSERT OR IGNORE INTO trend_data_5m "
            "(reg_id,ts,avg_val,min_val,max_val) VALUES (?,?,?,?,?)", rows)
    return ((now - start) // 300) * len(CHANNELS)

def seed_10m(cur, now, days):
    start = ((now - days * 86400) // 600) * 600
    rows = []
    ts = start
    while ts < now:
        bucket_end = ts + 600
        for ch in CHANNELS:
            pts = [sensor_value(ch, ts + i * SAMPLE_INTERVAL)
                   for i in range(600 // SAMPLE_INTERVAL)]
            rows.append((ch, ts, round(sum(pts) / len(pts)), min(pts), max(pts)))
        ts = bucket_end
        if len(rows) >= 5000:
            cur.executemany(
                "INSERT OR IGNORE INTO trend_data_10m "
                "(reg_id,ts,avg_val,min_val,max_val) VALUES (?,?,?,?,?)", rows)
            rows.clear()
    if rows:
        cur.executemany(
            "INSERT OR IGNORE INTO trend_data_10m "
            "(reg_id,ts,avg_val,min_val,max_val) VALUES (?,?,?,?,?)", rows)
    return ((now - start) // 600) * len(CHANNELS)

def main():
    _validate()

    if os.path.exists(OUTPUT_DB):
        os.remove(OUTPUT_DB)

    con = sqlite3.connect(OUTPUT_DB)
    cur = con.cursor()
    create_schema(cur)
    con.commit()

    now = (int(time.time()) // SAMPLE_INTERVAL) * SAMPLE_INTERVAL
    print(f"SmartRoute Trend DB Seeder")
    print(f"  Channels        : {CHANNELS}")
    print(f"  Sample interval : {SAMPLE_INTERVAL}s")
    print(f"  Reference time  : {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(now))}")
    if RECORDING_DAYS < 365:
        print(f"  Retention       : {RAW_DAYS}d (테스트 모드 — 세 테이블 동일 기간)")
    else:
        print(f"  Retention       : raw {RAW_DAYS}d / 5m {AGG_5M_DAYS}d / 10m {AGG_10M_DAYS}d")
    print()

    t0 = time.time()
    print(f"  [1/3] trend_data     {RAW_DAYS}d @ {SAMPLE_INTERVAL}s ... ", end="", flush=True)
    n = seed_raw(cur, now, RAW_DAYS)
    con.commit()
    print(f"{n:,} rows  ({time.time()-t0:.1f}s)")

    t0 = time.time()
    print(f"  [2/3] trend_data_5m  {AGG_5M_DAYS}d @ 5m  ... ", end="", flush=True)
    n = seed_5m(cur, now, AGG_5M_DAYS)
    con.commit()
    print(f"{n:,} rows  ({time.time()-t0:.1f}s)")

    t0 = time.time()
    print(f"  [3/3] trend_data_10m {AGG_10M_DAYS}d @ 10m ... ", end="", flush=True)
    n = seed_10m(cur, now, AGG_10M_DAYS)
    con.commit()
    print(f"{n:,} rows  ({time.time()-t0:.1f}s)")

    cur.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    con.close()

    size_mb = os.path.getsize(OUTPUT_DB) / (1024 * 1024)
    print(f"\n  Output: {OUTPUT_DB}  ({size_mb:.1f} MB)")
    print(f"\n  Deploy:")
    print(f"    scp {OUTPUT_DB} root@{DEVICE_IP}:/var/lib/swr/trend.db")

if __name__ == "__main__":
    main()
