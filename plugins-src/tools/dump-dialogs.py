#!/usr/bin/env python3
# EXE 안의 한국어 문구를 뽑는다 — DialogUtilKR 의 dialogs\*.json 을 적을 때 쓴다.
#
#   python dump-dialogs.py --exe "…\CDS_95.EXE" --find 술집
#   python dump-dialogs.py --exe "…\CDS_95.EXE" --range 0x54A200 0x54B0B0
#   python dump-dialogs.py --exe "…\CDS_95.EXE" --find 술집 --json > 술집.json
#
# "자리" 는 그 문구가 쓸 수 있는 최대 바이트 수(원문 + 뒤에 이어지는 0)다. 그보다 길게 쓰면
# DialogUtilKR 이 새 글을 딴 자리에 두고 포인터를 돌린다 — 그래도 되지만, 자리 안에 들면
# 건드리는 곳이 그 문구뿐이라 더 얌전하다.
import argparse, io, json, sys
import pefile

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")


def load(path):
    data = open(path, "rb").read()
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    secs = []
    for s in pe.sections:
        name = s.Name.decode(errors="replace").rstrip("\x00")
        vs, rs = s.Misc_VirtualSize, s.SizeOfRawData
        use = vs if (vs and vs < rs) else rs
        secs.append((name, base + s.VirtualAddress, s.PointerToRawData, use))
    return data, secs


def off_to_va(secs, off):
    for _, va, raw, size in secs:
        if raw <= off < raw + size:
            return va + (off - raw)
    return 0


def slot_cap(data, off, length):
    z = 0
    q = off + length
    while z < 512 and q + z + 1 < len(data) and data[q + z + 1] == 0:
        z += 1
    return length + z


def walk(data, secs, names=(".data", ".rdata")):
    """널로 끝나는 CP949 문구를 훑는다. (파일오프셋, VA, 글, 길이, 자리) 를 내놓는다."""
    for name, va, raw, size in secs:
        if name not in names:
            continue
        i = raw
        end = raw + size
        while i < end:
            e = data.find(b"\x00", i, end)
            if e < 0:
                break
            s = data[i:e]
            if 2 <= len(s) <= 400 and any(b >= 0x80 for b in s):
                try:
                    t = s.decode("cp949")
                except UnicodeDecodeError:
                    t = None
                if t and t.isprintable():
                    yield i, off_to_va(secs, i), t, len(s), slot_cap(data, i, len(s))
            i = e + 1


def main():
    ap = argparse.ArgumentParser(description="CDS_95.EXE 안의 한국어 문구를 뽑는다")
    ap.add_argument("--exe", required=True, help="CDS_95.EXE 경로")
    ap.add_argument("--find", help="이 말이 든 문구만")
    ap.add_argument("--range", nargs=2, metavar=("VA1", "VA2"), help="이 VA 구간의 문구만 (예: 0x54A200 0x54B0B0)")
    ap.add_argument("--json", action="store_true", help="dialogs\\*.json 모양으로 (Text 는 원문 그대로 — 고쳐 쓰라는 자리)")
    ap.add_argument("--limit", type=int, default=200, help="몇 개까지 (기본 200)")
    args = ap.parse_args()

    data, secs = load(args.exe)
    lo = hi = None
    if args.range:
        lo, hi = int(args.range[0], 16), int(args.range[1], 16)

    rows = []
    for off, va, text, length, cap in walk(data, secs):
        if args.find and args.find not in text:
            continue
        if lo is not None and not (lo <= va < hi):
            continue
        rows.append((off, va, text, length, cap))
        if len(rows) >= args.limit:
            break

    if args.json:
        out = [
            {"Note": f"0x{off:X} (자리 {cap})", "Find": text, "Text": text, "Enabled": False}
            for off, va, text, length, cap in rows
        ]
        print(json.dumps(out, ensure_ascii=False, indent=2))
        return

    print(f"{'VA':>9} {'파일오프셋':>10} {'길이':>4} {'자리':>4}  문구")
    for off, va, text, length, cap in rows:
        print(f"0x{va:X} 0x{off:08X} {length:>4} {cap:>4}  {text}")
    print(f"\n{len(rows)}개" + ("  (--limit 로 늘릴 수 있다)" if len(rows) >= args.limit else ""))


if __name__ == "__main__":
    main()
