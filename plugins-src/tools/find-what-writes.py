#!/usr/bin/env python3
"""
cds_95.exe(32-bit/WOW64)에서 특정 주소에 WRITE 하는 명령을 하드웨어 브레이크포인트로
잡는다. 치트엔진 "이 주소에 쓰는 코드 찾기"와 동일. MinHook 대상(날짜 +30 루틴) 특정용.

사용:
  python find-what-writes.py [addr_hex] [seconds] [--dry]
    addr_hex : 감시할 주소 (기본 5A4D28 = day)
    seconds  : write 대기 시간 (기본 120)
    --dry    : attach→arm→detach 배관만 검증(브레이크포인트 안 걸고 바로 뗌)

안전장치:
  - DebugSetProcessKillOnExit(FALSE): 이 도구가 죽어도 게임은 안 죽음.
  - 종료(정상/타임아웃/에러) 시 모든 스레드의 Dr0-3/Dr7 을 반드시 0으로 청소 후 detach.
"""
import sys, ctypes, time
from ctypes import wintypes, byref, sizeof

addr = int(sys.argv[1], 16) if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else 0x5A4D28
seconds = int(sys.argv[2]) if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else 120
DRY = "--dry" in sys.argv
PROC = "cds_95.exe"

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

# --- 핸들 반환/포인터 인자 truncation 방지 ---
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

# --- WOW64_CONTEXT (x86 CONTEXT) ---
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

CONTEXT_DEBUG = 0x00010000 | 0x00000010          # i386 | DEBUG_REGISTERS
CONTEXT_FULLDBG = 0x00010000 | 0x1 | 0x2 | 0x10    # i386|CONTROL|INTEGER|DEBUG

# --- DEBUG_EVENT (64-bit 디버거: union 은 offset 16) ---
class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("dwDebugEventCode", wintypes.DWORD), ("dwProcessId", wintypes.DWORD),
                ("dwThreadId", wintypes.DWORD), ("_pad", wintypes.DWORD),
                ("u", ctypes.c_ubyte * 168)]
k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wintypes.DWORD]
k32.WaitForDebugEvent.restype = wintypes.BOOL

EXIT=1; CREATE_THREAD=2; CREATE_PROCESS=3; EXCEPTION=1
EXC_SINGLE_STEP=0x80000004; EXC_BREAKPOINT=0x80000003
DBG_CONTINUE=0x00010002; DBG_NOT_HANDLED=0x80010001
THREAD_ALL=0x1FFFFF
PROCESS_VM_READ=0x10; PROCESS_QUERY=0x400
DR7_WRITE4 = (1<<0) | (0b01<<16) | (0b11<<18)   # L0, write, len4 = 0xD0001

def find_pid():
    import subprocess
    # Toolhelp process snapshot 대신 간단히: WMIC 없이 EnumProcesses 생략, tasklist 파싱
    out = subprocess.check_output(["tasklist","/FI",f"IMAGENAME eq {PROC}","/FO","CSV","/NH"], text=True)
    for line in out.splitlines():
        if PROC.lower() in line.lower():
            parts = [p.strip('"') for p in line.split('","')]
            return int(parts[1])
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
    n=0
    for tid in iter_threads(pid):
        h = k32.OpenThread(THREAD_ALL, False, tid)
        if not h: continue
        k32.SuspendThread(h)
        c = WOW64_CONTEXT(); c.ContextFlags = CONTEXT_DEBUG
        if k32.Wow64GetThreadContext(h, byref(c)):
            c.ContextFlags = CONTEXT_DEBUG
            c.Dr0 = dr0; c.Dr1=0; c.Dr2=0; c.Dr3=0; c.Dr6=0; c.Dr7 = dr7
            if k32.Wow64SetThreadContext(h, byref(c)): n+=1
        k32.ResumeThread(h)
        k32.CloseHandle(h)
    return n

def main():
    pid = find_pid()
    if not pid:
        print(f"{PROC} 프로세스 없음"); return
    print(f"PID={pid}, 감시주소=0x{addr:X}, {seconds}s, dry={DRY}")

    if not k32.DebugActiveProcess(pid):
        print("DebugActiveProcess 실패", ctypes.get_last_error()); return
    k32.DebugSetProcessKillOnExit(False)
    hproc = k32.OpenProcess(PROCESS_VM_READ|PROCESS_QUERY, False, pid)

    armed = False
    hit = None
    t0 = time.time()
    ev = DEBUG_EVENT()
    try:
        # 초기 이벤트 배수 + arming, 그리고 write 대기 루프
        while time.time() - t0 < seconds:
            if not k32.WaitForDebugEvent(byref(ev), 200):
                # 타임아웃(이벤트 없음) — arming 후면 그냥 계속 대기
                if not armed and not DRY:
                    set_dr_all(pid, addr, DR7_WRITE4); armed = True
                    print("hw bp armed. 이제 게임에서 숙박 하세요...")
                if DRY and not armed:
                    armed = True
                    print("dry: arm 생략, 곧 detach")
                    break
                continue
            code = ev.dwDebugEventCode
            status = DBG_CONTINUE
            if code == EXCEPTION:
                exc_code = int.from_bytes(bytes(ev.u[0:4]), "little")
                if exc_code == EXC_SINGLE_STEP and armed:
                    h = k32.OpenThread(THREAD_ALL, False, ev.dwThreadId)
                    c = WOW64_CONTEXT(); c.ContextFlags = CONTEXT_FULLDBG
                    k32.Wow64GetThreadContext(h, byref(c))
                    if c.Dr6 & 0x1:  # B0 = Dr0 hit
                        hit = {k:getattr(c,k) for k in ("Eip","Eax","Ecx","Edx","Ebx","Esp","Ebp","Esi","Edi")}
                        # 콜스택 후보: [esp..esp+0x80] 중 .text 범위
                        stack=[]
                        buf=(ctypes.c_ubyte*0x80)(); rd=ctypes.c_size_t(0)
                        if k32.ReadProcessMemory(hproc, ctypes.c_void_p(c.Esp), buf, 0x80, byref(rd)):
                            for i in range(0, rd.value-3, 4):
                                v=int.from_bytes(bytes(buf[i:i+4]),"little")
                                if 0x401000 <= v < 0x4C3000: stack.append(v)
                        hit["stack"]=stack
                        # write 명령 바이트 (Eip 앞 16바이트)
                        cb=(ctypes.c_ubyte*24)(); rd2=ctypes.c_size_t(0)
                        if k32.ReadProcessMemory(hproc, ctypes.c_void_p(c.Eip-16), cb, 24, byref(rd2)):
                            hit["bytes_before_eip"]=bytes(cb)
                        c.Dr6 = 0
                        k32.Wow64SetThreadContext(h, byref(c))
                        k32.CloseHandle(h)
                        break
                    k32.CloseHandle(h)
                elif exc_code == EXC_BREAKPOINT:
                    status = DBG_CONTINUE
                else:
                    status = DBG_NOT_HANDLED  # 게임 자체 예외는 게임에 넘김
            elif code == EXIT:
                pass
            k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, status)
    finally:
        # ★ 반드시 브레이크포인트 청소 후 detach ★
        cleared = set_dr_all(pid, 0, 0)
        # 대기중 이벤트가 있으면 흘려보냄
        k32.DebugActiveProcessStop(pid)
        if hproc: k32.CloseHandle(hproc)
        print(f"detached. bp cleared on {cleared} threads.")

    if hit:
        print("\n=== WRITE 포착! ===")
        print(f"  EIP(=다음명령) = 0x{hit['Eip']:X}  → write 명령은 그 직전")
        print(f"  EAX=0x{hit['Eax']:X} ECX=0x{hit['Ecx']:X} EDX=0x{hit['Edx']:X} EBX=0x{hit['Ebx']:X}")
        print(f"  ESI=0x{hit['Esi']:X} EDI=0x{hit['Edi']:X} EBP=0x{hit['Ebp']:X} ESP=0x{hit['Esp']:X}")
        if hit.get("bytes_before_eip"):
            try:
                import capstone
                md=capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32);
                print("  디스어셈블 (Eip-16 .. Eip):")
                for ins in md.disasm(hit["bytes_before_eip"], hit['Eip']-16):
                    mark = "  <<= write?" if ins.address < hit['Eip'] <= ins.address+ins.size else ""
                    print(f"    0x{ins.address:X}: {ins.mnemonic} {ins.op_str}{mark}")
            except Exception as e:
                print("  bytes:", hit["bytes_before_eip"].hex())
        print(f"  콜스택(.text 리턴주소 후보): {[hex(x) for x in hit['stack'][:12]]}")
    elif not DRY:
        print("write 미포착 (시간 내 숙박 안 함/다른 방식). seconds 늘리거나 재시도.")

main()
