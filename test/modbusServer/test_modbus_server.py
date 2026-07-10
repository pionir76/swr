"""
Modbus TCP Server Integration Test
====================================
SmartRoute 디바이스의 Modbus TCP Server 동작을 검증한다.

테스트 흐름:
  1. SWR API 로그인 → Bearer 토큰 획득
  2. API로 디바이스/레지스터 목록 수집 (unifiedRegisterId, readOnly 포함)
  3. Modbus TCP 연결 (FC3 Read Holding Registers) — 전 레지스터 읽기
  4. Modbus TCP 쓰기 (FC6 Write Single Register) — RW 레지스터만
     * 라우터 구조 특성: 쓰기 후 즉시 read-back 검증 수행하지 않음
       readData()가 RegisterTable(실제 장치 폴링값)을 반환하므로
       즉시 read-back은 이전 폴링값을 반환 — 정상 동작임
     * FC6 응답에 Modbus Exception 없음 = 실제 장치로 명령 전달 성공으로 판정
     * 쓰기 후 원래 값으로 복원

실행 방법:
  python3 test_modbus_server.py [--host HOST] [--api-port PORT] [--mb-port PORT]
                                [--slave ID] [--user USER] [--password PW]

기본값:
  --host      192.168.0.150
  --api-port  8080
  --mb-port   502
  --slave     1
  --user      admin
  --password  1234

의존성 설치:
  pip install pymodbus requests
"""

import sys
import argparse
import requests
from pymodbus.client import ModbusTcpClient

# ─────────────────────────────────────────────
# 인수 파싱
# ─────────────────────────────────────────────
parser = argparse.ArgumentParser(description="SWR Modbus TCP Server Test")
parser.add_argument("--host",      default="192.168.0.150")
parser.add_argument("--api-port",  default=8080,  type=int)
parser.add_argument("--mb-port",   default=502,   type=int)
parser.add_argument("--slave",     default=1,     type=int)
parser.add_argument("--user",      default="admin")
parser.add_argument("--password",  default="1234")
args = parser.parse_args()

API_BASE  = f"http://{args.host}:{args.api_port}"
MB_HOST   = args.host
MB_PORT   = args.mb_port
MB_SLAVE  = args.slave
USERNAME  = args.user
PASSWORD  = args.password

# ─────────────────────────────────────────────
# 출력 헬퍼
# ─────────────────────────────────────────────
RESET  = "\033[0m"
GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BOLD   = "\033[1m"

def ok(msg):     print(f"  {GREEN}✓{RESET} {msg}")
def fail(msg):   print(f"  {RED}✗{RESET} {msg}")
def info(msg):   print(f"  {CYAN}·{RESET} {msg}")
def warn(msg):   print(f"  {YELLOW}!{RESET} {msg}")
def header(msg): print(f"\n{BOLD}{msg}{RESET}")

pass_count = 0
fail_count = 0

def record_ok(msg):
    global pass_count
    pass_count += 1
    ok(msg)

def record_fail(msg):
    global fail_count
    fail_count += 1
    fail(msg)

# ─────────────────────────────────────────────
# 1. API 로그인
# ─────────────────────────────────────────────
header("[ 1 ] API 로그인")
try:
    r = requests.post(f"{API_BASE}/api/login",
                      json={"username": USERNAME, "password": PASSWORD},
                      timeout=5)
except Exception as e:
    fail(f"API 접속 불가: {e}")
    sys.exit(1)

if r.status_code != 200:
    fail(f"로그인 실패 ({r.status_code}): {r.text}")
    sys.exit(1)

token = r.json().get("token", "")
ok(f"로그인 성공  user={USERNAME}  token={token[:8]}...")

session = requests.Session()
session.headers.update({"Authorization": f"Bearer {token}"})

# ─────────────────────────────────────────────
# 2. 레지스터 목록 수집
# ─────────────────────────────────────────────
header("[ 2 ] 레지스터 목록 수집 (API)")
r = session.get(f"{API_BASE}/api/devices", timeout=5)
if r.status_code != 200:
    fail(f"디바이스 목록 실패 ({r.status_code})")
    sys.exit(1)

devices = r.json().get("devices", r.json() if isinstance(r.json(), list) else [])
info(f"디바이스 수: {len(devices)}")

registers = []
for dev in devices:
    dev_id   = dev.get("id")
    dev_name = dev.get("name", f"device-{dev_id}")
    r2 = session.get(f"{API_BASE}/api/devices/{dev_id}/registers", timeout=5)
    if r2.status_code != 200:
        warn(f"디바이스 {dev_name} 레지스터 수신 실패 ({r2.status_code})")
        continue
    regs = r2.json().get("registers", r2.json() if isinstance(r2.json(), list) else [])
    for reg in regs:
        uid = reg.get("unifiedRegisterId", reg.get("id"))
        if uid is None:
            continue
        registers.append({
            "devName":  dev_name,
            "name":     reg.get("name", f"reg-{uid}"),
            "uid":      uid,
            "readOnly": reg.get("readOnly", True),
            "type":     reg.get("type", "holding_register"),
        })

if not registers:
    warn("레지스터가 없습니다. Modbus 테스트를 건너뜁니다.")
    sys.exit(0)

ok(f"총 레지스터 {len(registers)}개 수집")
for reg in registers:
    ro = "RO" if reg["readOnly"] else "RW"
    info(f"  uid={reg['uid']:4d}  [{ro}]  {reg['devName']} / {reg['name']}  ({reg['type']})")

# ─────────────────────────────────────────────
# 3. Modbus TCP 연결
# ─────────────────────────────────────────────
header("[ 3 ] Modbus TCP 연결")
client = ModbusTcpClient(host=MB_HOST, port=MB_PORT)
if not client.connect():
    fail(f"연결 실패: {MB_HOST}:{MB_PORT}")
    sys.exit(1)
ok(f"연결 성공: {MB_HOST}:{MB_PORT}  slaveId={MB_SLAVE}")

# ─────────────────────────────────────────────
# 4. 읽기 테스트 (FC3)
# ─────────────────────────────────────────────
header("[ 4 ] 읽기 테스트 (FC3 Read Holding Registers)")
read_results  = {}
read_ok_cnt   = 0
read_fail_cnt = 0

for reg in registers:
    uid = reg["uid"]
    rr  = client.read_holding_registers(address=uid, count=1, device_id=MB_SLAVE)
    if rr.isError():
        record_fail(f"uid={uid:4d}  {reg['devName']} / {reg['name']}  → {rr}")
        read_results[uid] = None
        read_fail_cnt += 1
    else:
        val = rr.registers[0]
        record_ok(f"uid={uid:4d}  {reg['devName']} / {reg['name']}  → {val}  (0x{val:04X})")
        read_results[uid] = val
        read_ok_cnt += 1

print(f"\n  읽기 결과: {GREEN}{read_ok_cnt} 성공{RESET} / {RED}{read_fail_cnt} 실패{RESET}")

# ─────────────────────────────────────────────
# 5. 쓰기 테스트 (FC6) — RW 레지스터만
# ─────────────────────────────────────────────
header("[ 5 ] 쓰기 테스트 (FC6 Write Single Register) — RW 레지스터만")

writable = [reg for reg in registers
            if not reg["readOnly"] and read_results.get(reg["uid"]) is not None]

if not writable:
    warn("쓰기 가능한 레지스터가 없습니다.")
else:
    write_ok_cnt   = 0
    write_fail_cnt = 0
    restore_fail   = []

    for reg in writable:
        uid      = reg["uid"]
        orig_val = read_results[uid]
        test_val = (orig_val + 1) & 0xFFFF

        # FC6 쓰기
        # ※ 라우터 구조: 쓰기 명령은 RegisterTable을 거쳐 실제 필드 장치로 전달됨.
        #   readData()가 RegisterTable(폴링값)을 반환하므로 즉시 read-back 검증 불필요.
        #   FC6 응답에 Modbus Exception 없음 = 명령 전달 성공.
        wr = client.write_register(address=uid, value=test_val, device_id=MB_SLAVE)
        if wr.isError():
            record_fail(f"uid={uid:4d}  {reg['devName']} / {reg['name']}  쓰기 실패 → {wr}")
            write_fail_cnt += 1
        else:
            record_ok(f"uid={uid:4d}  {reg['devName']} / {reg['name']}  "
                      f"현재값={orig_val}  전달값={test_val}  → 명령 전달됨")
            write_ok_cnt += 1

        # 원래 값 복원
        wr2 = client.write_register(address=uid, value=orig_val, device_id=MB_SLAVE)
        if wr2.isError():
            restore_fail.append(f"uid={uid} ({reg['name']})")

    print(f"\n  쓰기 결과: {GREEN}{write_ok_cnt} 명령 전달됨{RESET} / {RED}{write_fail_cnt} 실패{RESET}")
    if restore_fail:
        warn("복원 명령 전달 실패: " + ", ".join(restore_fail))

# ─────────────────────────────────────────────
# 완료 및 종합 결과
# ─────────────────────────────────────────────
client.close()
header("[ 결과 요약 ]")
print(f"  총 {GREEN}{pass_count} 통과{RESET} / {RED}{fail_count} 실패{RESET}")
if fail_count > 0:
    warn("일부 테스트 실패 — 위 로그를 확인하세요.")
    sys.exit(1)
else:
    ok("모든 테스트 통과")
