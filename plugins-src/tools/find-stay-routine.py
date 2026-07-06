#!/usr/bin/env python3
"""
cds_95.exe 에서 "숙박(+30일)" 처리 루틴 후보를 찾는다.
아이디어: 상수 30(0x1E)을 로드/사용하는 명령 중, 근처(±window 명령)에서
날짜(0x5A4D20/24/28)나 소지금(0x5B6194) 절대주소를 참조하거나 CALL 하는 것을
'숙박 루틴 후보'로 보고 주변 디스어셈블을 출력한다.
"""
import sys, capstone

EXE = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\ocean\Desktop\대항해시대3\cds_95.exe"
IMAGE_BASE = 0x400000
DATE_ADDRS = {0x5A4D20, 0x5A4D24, 0x5A4D28}
GOLD_ADDR = 0x5B6194
WATCH = DATE_ADDRS | {GOLD_ADDR}

data = open(EXE, "rb").read()

# --- PE 섹션 파싱 (.text 찾기) ---
e_lfanew = int.from_bytes(data[0x3C:0x40], "little")
fh = e_lfanew + 4
num_sec = int.from_bytes(data[fh+2:fh+4], "little")
opt_size = int.from_bytes(data[fh+16:fh+18], "little")
sec_tab = fh + 20 + opt_size
text = None
for s in range(num_sec):
    off = sec_tab + s*40
    name = data[off:off+8].rstrip(b"\x00").decode("latin1")
    va = int.from_bytes(data[off+12:off+16], "little")
    rsize = int.from_bytes(data[off+16:off+20], "little")
    rptr = int.from_bytes(data[off+20:off+24], "little")
    if name == ".text":
        text = (va, rptr, rsize)
if not text:
    print("no .text"); sys.exit(1)
va, rptr, rsize = text
code = data[rptr:rptr+rsize]
base_va = IMAGE_BASE + va

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True

# 전체 선형 디스어셈블 (한 번)
insns = list(md.disasm(code, base_va))
print(f".text {len(insns)} instrs @ 0x{base_va:X}")

def refs_watch(ins):
    hits = set()
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            if op.mem.disp in WATCH:
                hits.add(op.mem.disp)
        if op.type == capstone.x86.X86_OP_IMM and op.imm in WATCH:
            hits.add(op.imm)
    return hits

def loads_30(ins):
    if ins.mnemonic in ("mov", "push", "add", "sub", "cmp", "mov"):
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_IMM and op.imm == 30:
                return True
    return False

# 인덱스별 watch 참조/30로드 표시
n = len(insns)
watch_idx = [i for i,ins in enumerate(insns) if refs_watch(ins)]
thirty_idx = [i for i,ins in enumerate(insns) if loads_30(ins)]
watch_set = set(watch_idx)

WINDOW = 40  # 명령 개수 반경
print(f"\n=== '30 로드' {len(thirty_idx)}건 중, ±{WINDOW}명령 내 날짜/소지금 참조가 있는 후보 ===\n")
import bisect
reported = 0
for ti in thirty_idx:
    lo = bisect.bisect_left(watch_idx, ti-WINDOW)
    hi = bisect.bisect_right(watch_idx, ti+WINDOW)
    near = watch_idx[lo:hi]
    if not near:
        continue
    kinds = set()
    for wi in near:
        for a in refs_watch(insns[wi]):
            kinds.add("gold" if a == GOLD_ADDR else "date")
    reported += 1
    ins = insns[ti]
    print(f"--- 후보 @ 0x{ins.address:X}  ({ins.mnemonic} {ins.op_str})  근처참조={sorted(kinds)} ---")
    s = max(0, ti-6); e = min(n, ti+10)
    for k in range(s, e):
        x = insns[k]
        mark = "  <<30" if k == ti else ("  <<W" if k in watch_set else "")
        print(f"    0x{x.address:X}: {x.mnemonic:6} {x.op_str}{mark}")
    print()
print(f"총 후보 {reported}건")
