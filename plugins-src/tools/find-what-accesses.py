#!/usr/bin/env python3
"""
⚠ 폐기(DO NOT USE) — 이 유저모드 HW-BP 방식은 cds_95.exe(WOW64)에서 접근 순간 **게임을 크래시**시킴
   (개선판인데도 크래시, 2번 확인). 참고/교훈용으로만 보관. 실제 트레이스는 **`ce-trace.ps1`(CE Lua 자동화)**
   를 쓸 것. CE는 커널 디버거라 안 죽음.

find-what-accesses.py — cds_95.exe(32-bit/WOW64)에서 특정 주소에 READ/WRITE 하는
명령을 하드웨어 브레이크포인트(디버그 레지스터 DR0/DR7)로 잡는다. CE "Find out what
accesses this address"의 자동화 버전. (w2)

사용:
  python find-what-accesses.py <addr_hex> [seconds] [--mode rw|r|w] [--dry] [--len N]
    addr_hex : 감시 주소 (예: 5A4E40 = 기함 함선종류)
    seconds  : 수집 시간 (기본 30)
    --mode   : rw(기본, 접근=읽기+쓰기) | r(읽기전용은 HW불가→rw로) | w(쓰기)
               ※ x86 데이터 BP는 '쓰기(01)' 또는 '읽기+쓰기(11)'만 지원. 순수 읽기전용 없음.
    --len    : 감시 길이 1/2/4/8 (기본 4). 주소는 len 배수로 정렬돼야 함.
    --dry    : attach→(arm 생략)→detach 배관만 검증(게임 안전 확인용)

★ 이전 find-what-writes.py 크래시 수정 ★
  - HW BP 히트(=EXCEPTION_SINGLE_STEP) 후에도 **반드시 ContinueDebugEvent(DBG_CONTINUE)로
    예외를 소비**한 뒤 계속 루프. break-후-detach 로 미처리 예외를 남기지 않는다.
    (미처리 single-step 예외가 detach 시 게임에 재전달 → STATUS_SINGLE_STEP 핸들러 없음 → 크래시)
  - 종료 시 모든 스레드 DR 청소 + 대기 이벤트 배수 + DebugActiveProcessStop.
  - DebugSetProcessKillOnExit(False): 이 도구가 죽어도 게임은 유지.
  - 여러 접근 명령을 EIP로 dedupe 해 전부 수집(1개만 잡고 끝내지 않음).
"""
import sys, ctypes, time
from ctypes import wintypes, byref, sizeof

def parse_args():
    a = sys.argv[1:]
    if not a or a[0].startswith("--"):
        print("addr_hex 필수. 예: python find-what-accesses.py 5A4E40 30 --mode rw"); sys.exit(1)
    addr = int(a[0], 16)
    seconds = 30; mode = "rw"; length = 4; dry = "--dry" in a
    i = 1
    while i < len(a):
        if a[i] == "--mode": mode = a[i+1]; i += 2
        elif a[i] == "--len": length = int(a[i+1]); i += 2
        elif a[i] == "--dry": i += 1
        elif not a[i].startswith("--"): seconds = int(a[i]); i += 1
        else: i += 1
    return addr, seconds, mode, length, dry

ADDR, SECONDS, MODE, LEN, DRY = parse_args()
PROC = "cds_95.exe"
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

for fn, res, args in [
    ("DebugActiveProcess", wintypes.BOOL, [wintypes.DWORD]),
    ("DebugActiveProcessStop", wintypes.BOOL, [wintypes.DWORD]),
    ("DebugSetProcessKillOnExit", wintypes.BOOL, [wintypes.BOOL]),
    ("ContinueDebugEvent", wintypes.BOOL, [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD]),
    ("OpenThread", wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
    ("OpenProcess", wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
    ("SuspendThread", wintypes.DWORD, [wintypes.HANDLE]),
    ("ResumeThread", wintypes.DWORD, [wintypes.HANDLE]),
    ("CloseHandle", wintypes.BOOL, [wintypes.HANDLE]),
    ("CreateToolhelp32Snapshot", wintypes.HANDLE, [wintypes.DWORD, wintypes.DWORD]),
    ("ReadProcessMemory", wintypes.BOOL, [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]),
]:
    f = getattr(k32, fn); f.restype = res; f.argtypes = args

TH32CS_SNAPTHREAD = 0x00000004
class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                ("th32ThreadID", wintypes.DWORD), ("th32OwnerProcessID", wintypes.DWORD),
                ("tpBasePri", wintypes.LONG), ("tpDeltaPri", wintypes.LONG), ("dwFlags", wintypes.DWORD)]
k32.Thread32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(THREADENTRY32)]
k32.Thread32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(THREADENTRY32)]

class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [
        ("ContextFlags", wintypes.DWORD),
        ("Dr0", wintypes.DWORD), ("Dr1", wintypes.DWORD), ("Dr2", wintypes.DWORD),
        ("Dr3", wintypes.DWORD), ("Dr6", wintypes.DWORD), ("Dr7", wintypes.DWORD),
        ("FloatSave", ctypes.c_ubyte * 112),
        ("SegGs", wintypes.DWORD), ("SegFs", wintypes.DWORD), ("SegEs", wintypes.DWORD), ("SegDs", wintypes.DWORD),
        ("Edi", wintypes.DWORD), ("Esi", wintypes.DWORD), ("Ebx", wintypes.DWORD), ("Edx", wintypes.DWORD),
        ("Ecx", wintypes.DWORD), ("Eax", wintypes.DWORD), ("Ebp", wintypes.DWORD), ("Eip", wintypes.DWORD),
        ("SegCs", wintypes.DWORD), ("EFlags", wintypes.DWORD), ("Esp", wintypes.DWORD), ("SegSs", wintypes.DWORD),
        ("ExtendedRegisters", ctypes.c_ubyte * 512),
    ]
k32.Wow64GetThreadContext.argtypes = [wintypes.HANDLE, ctypes.POINTER(WOW64_CONTEXT)]
k32.Wow64SetThreadContext.argtypes = [wintypes.HANDLE, ctypes.POINTER(WOW64_CONTEXT)]

CONTEXT_DEBUG   = 0x00010000 | 0x00000010
CONTEXT_FULLDBG = 0x00010000 | 0x1 | 0x2 | 0x10

class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wintypes.DWORD), ("dwProcessId", wintypes.DWORD),
                ("dwThreadId", wintypes.DWORD), ("_pad", wintypes.DWORD),
                ("u", ctypes.c_ubyte * 168)]
k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wintypes.DWORD]
k32.WaitForDebugEvent.restype = wintypes.BOOL

EXIT_PROCESS=5; CREATE_THREAD=2; EXCEPTION=1
EXC_SINGLE_STEP=0x80000004; EXC_BREAKPOINT=0x80000003
DBG_CONTINUE=0x00010002; DBG_NOT_HANDLED=0x80010001
THREAD_ALL=0x1FFFFF
PROCESS_VM_READ=0x10; PROCESS_QUERY=0x400

def dr7_value(mode, length):
    rw = 0b01 if mode == "w" else 0b11          # 쓰기 or 읽기/쓰기
    lenbits = {1:0b00, 2:0b01, 8:0b10, 4:0b11}[length]
    return (1 << 0) | (rw << 16) | (lenbits << 18)   # L0 + R/W0 + LEN0

DR7 = dr7_value(MODE, LEN)

def find_pid():
    import subprocess
    out = subprocess.check_output(["tasklist","/FI",f"IMAGENAME eq {PROC}","/FO","CSV","/NH"], text=True)
    for line in out.splitlines():
        if PROC.lower() in line.lower():
            return int([p.strip('"') for p in line.split('","')][1])
    return None

def iter_threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32(); te.dwSize = sizeof(THREADENTRY32)
    ok = k32.Thread32First(snap, byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            yield te.th32ThreadID
        ok = k32.Thread32Next(snap, byref(te))
    k32.CloseHandle(snap)

def set_dr_all(pid, dr0, dr7):
    n = 0
    for tid in iter_threads(pid):
        h = k32.OpenThread(THREAD_ALL, False, tid)
        if not h: continue
        k32.SuspendThread(h)
        c = WOW64_CONTEXT(); c.ContextFlags = CONTEXT_DEBUG
        if k32.Wow64GetThreadContext(h, byref(c)):
            c.ContextFlags = CONTEXT_DEBUG
            c.Dr0=dr0; c.Dr1=0; c.Dr2=0; c.Dr3=0; c.Dr6=0; c.Dr7=dr7
            if k32.Wow64SetThreadContext(h, byref(c)): n += 1
        k32.ResumeThread(h)
        k32.CloseHandle(h)
    return n

def set_dr_one(tid, dr0, dr7):
    h = k32.OpenThread(THREAD_ALL, False, tid)
    if not h: return
    c = WOW64_CONTEXT(); c.ContextFlags = CONTEXT_DEBUG
    if k32.Wow64GetThreadContext(h, byref(c)):
        c.ContextFlags = CONTEXT_DEBUG
        c.Dr0=dr0; c.Dr1=0; c.Dr2=0; c.Dr3=0; c.Dr6=0; c.Dr7=dr7
        k32.Wow64SetThreadContext(h, byref(c))
    k32.CloseHandle(h)

def main():
    pid = find_pid()
    if not pid:
        print(f"{PROC} 프로세스 없음"); return
    print(f"PID={pid} addr=0x{ADDR:X} mode={MODE} len={LEN} {SECONDS}s dry={DRY} DR7=0x{DR7:X}")

    if not k32.DebugActiveProcess(pid):
        print("DebugActiveProcess 실패", ctypes.get_last_error()); return
    k32.DebugSetProcessKillOnExit(False)
    hproc = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY, False, pid)

    hits = {}           # eip -> {regs..., count}
    armed = False
    t0 = time.time()
    ev = DEBUG_EVENT()
    try:
        while time.time() - t0 < SECONDS:
            if not k32.WaitForDebugEvent(byref(ev), 100):
                continue
            code = ev.dwDebugEventCode
            status = DBG_CONTINUE

            if code == EXCEPTION:
                exc_code = int.from_bytes(bytes(ev.u[0:4]), "little")
                first_chance = int.from_bytes(bytes(ev.u[4:8]), "little")
                if exc_code == EXC_BREAKPOINT and not armed and not DRY:
                    # 어태치 시점의 초기 브레이크포인트 → 여기서 전 스레드 arm
                    n = set_dr_all(pid, ADDR, DR7)
                    armed = True
                    print(f"[arm] hw bp armed on {n} threads. 이제 대상 접근을 유발하세요...")
                elif exc_code == EXC_SINGLE_STEP and armed:
                    h = k32.OpenThread(THREAD_ALL, False, ev.dwThreadId)
                    c = WOW64_CONTEXT(); c.ContextFlags = CONTEXT_FULLDBG
                    k32.Wow64GetThreadContext(h, byref(c))
                    if c.Dr6 & 0xF:     # B0..B3 중 하나 = 우리 데이터 BP
                        eip = c.Eip
                        rec = hits.get(eip)
                        if not rec:
                            rec = {k: getattr(c, k) for k in ("Eip","Eax","Ecx","Edx","Ebx","Esp","Ebp","Esi","Edi")}
                            rec["count"] = 0
                            cb = (ctypes.c_ubyte*24)(); rd = ctypes.c_size_t(0)
                            if k32.ReadProcessMemory(hproc, ctypes.c_void_p(eip-16), cb, 24, byref(rd)):
                                rec["bytes"] = bytes(cb)
                            hits[eip] = rec
                        rec["count"] += 1
                        c.Dr6 = 0
                        k32.Wow64SetThreadContext(h, byref(c))   # DR6 클리어 후
                    k32.CloseHandle(h)
                    # ★ 반드시 계속: 예외 소비 (break 하지 않음)
                    status = DBG_CONTINUE
                else:
                    # 게임 자체 예외(첫 chance 아님 등)는 게임에 넘김
                    status = DBG_CONTINUE if first_chance else DBG_NOT_HANDLED

            elif code == CREATE_THREAD and armed:
                set_dr_one(ev.dwThreadId, ADDR, DR7)   # 새 스레드도 arm
            elif code == EXIT_PROCESS:
                print("게임 프로세스 종료됨"); k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE); break

            k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, status)

            if DRY and not armed:
                print("[dry] 초기 이벤트 처리, arm 생략 → detach")
                break
    finally:
        set_dr_all(pid, 0, 0)                 # DR 청소
        # 대기 이벤트 배수(detach 전 미처리 예외 방지)
        for _ in range(50):
            if not k32.WaitForDebugEvent(byref(ev), 0): break
            k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE)
        k32.DebugActiveProcessStop(pid)
        if hproc: k32.CloseHandle(hproc)
        print("detached.")

    if hits:
        try:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        except Exception:
            md = None
        print(f"\n=== 접근 명령 {len(hits)}개 (히트순) ===")
        for eip, r in sorted(hits.items(), key=lambda kv: -kv[1]["count"]):
            print(f"\n[EIP=0x{eip:X}] hits={r['count']}  (접근 명령은 EIP 직전)")
            print(f"  EAX={r['Eax']:08X} ECX={r['Ecx']:08X} EDX={r['Edx']:08X} EBX={r['Ebx']:08X}")
            print(f"  ESI={r['Esi']:08X} EDI={r['Edi']:08X} EBP={r['Ebp']:08X} ESP={r['Esp']:08X}")
            if md and r.get("bytes"):
                for ins in md.disasm(r["bytes"], eip-16):
                    mark = "  <== 접근" if ins.address < eip <= ins.address+ins.size else ""
                    print(f"    0x{ins.address:X}: {ins.mnemonic} {ins.op_str}{mark}")
            elif r.get("bytes"):
                print("  bytes(eip-16..):", r["bytes"].hex())
    elif not DRY:
        print("접근 미포착. seconds 늘리거나, 접근을 확실히 유발(재로드 등) 후 재시도.")

main()
