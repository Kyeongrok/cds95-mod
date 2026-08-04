# -*- coding: utf-8 -*-
"""
LS11/LS12 (KOEI) 아카이브 디코더 + 인코더.

대항해시대3 의 .CDS 파일 대부분이 이 형식이다 — 퀘스트 이벤트(ECQ/EDG/EEX/EHT/PCQ/PDG/
PEX/PHT/STORY*), 역사 이벤트(HIST_EV), 발견물 이벤트(DISEV), 얼굴(MALE/FEMALE) 등.
(WORLD.CDS · SAVEDATA.CDS · ACCDATA.CDS 만 예외로 날것이다.)

    파일 구조
    0x000  매직 "Ls12"(또는 "LS11") + 공백 패딩          16바이트
    0x010  사전 dictionary[256]                        256바이트
    0x110  파트 표 — 12바이트씩 N개 (전부 빅엔디안)
             +0 압축크기   +4 원본크기   +8 시작주소
           4바이트 0 = 표 끝
           데이터 블록들

    파트 개수에 형식상 상한이 없다. 늘려도 게임이 읽는다
    (kseokjung님 퀘스트패치 v1.5 가 파일당 120파트로 늘려 10년째 쓰이고 있다).

디코더는 plugins-src/CharacterUtilKR/src/ls12.c 를 옮긴 것이고,
인코더는 天翔記.jp 의 LS11Archiever(ls11_encode.cpp)를 옮긴 것이다.
    https://xn--rssu31gj1g.jp/?page=nobu_mod_the_ls11archiever

주의 — 이 인코더는 실제로 압축하지 않는다. 원본 구현이 LZ77 매치를 찾지 않고(point 가 늘 0)
허프만 사전도 항등(dict[i]=i)이라, 바이트를 가변길이 코드로 그대로 낸다. 결과가 원본보다
2배 남짓 커지지만 게임은 문제없이 읽는다(백동수 모드의 HIST_EV.CDS 도 같은 이유로
9,214 → 14,574 바이트로 커진 채 정상 동작한다).

쓰는 법:
    python ls12.py list   ECQ.CDS
    python ls12.py unpack ECQ.CDS out/
    python ls12.py pack   out/ NEW.CDS

라이브러리로:
    from ls12 import Ls12, build
    f = Ls12("ECQ.CDS")
    parts = [bytearray(f.decode(i)) for i in range(len(f.parts))]
    parts[2][0x119:0x11D] = struct.pack("<I", 50000)   # 보수 고치기
    open("ECQ.CDS", "wb").write(build([bytes(p) for p in parts]))

이벤트 스크립트의 오피코드는 obsidian 문서 "11.이벤트 스크립트 오피코드 표" 참고.
"""
import os
import struct
import sys


# ------------------------------------------------------------------ 디코더

class Ls12:
    def __init__(self, path):
        d = open(path, "rb").read()
        if d[:4] not in (b"LS11", b"Ls12"):
            raise ValueError("LS11/Ls12 파일이 아님: %r" % d[:4])
        self.d = d
        self.magic = d[:4]
        self.dict = d[16:272]
        self.parts = []                       # (압축크기, 원본크기, 시작주소)
        pos = 272
        while pos + 12 <= len(d):
            comp = struct.unpack_from(">I", d, pos)[0]
            if comp == 0:                     # 4바이트 0 = 표 끝
                break
            self.parts.append((comp,
                               struct.unpack_from(">I", d, pos + 4)[0],
                               struct.unpack_from(">I", d, pos + 8)[0]))
            pos += 12

    def decode(self, i):
        comp_len, out_len, off = self.parts[i]
        comp = self.d[off:off + comp_len]
        if comp_len == out_len:               # 무압축 저장
            return bytearray(comp)
        out = bytearray()
        total = comp_len * 8
        bp = 0
        delta = 0
        while len(out) < out_len and bp < total:
            # unary: 1이 이어지는 동안 읽다가 0을 만나면 멈춘다
            ml = 0
            while True:
                bit = (comp[bp >> 3] >> (7 - (bp & 7))) & 1
                bp += 1
                ml += 1
                if not bit or bp >= total:
                    break
            factor = 0
            for _ in range(ml):
                if bp >= total:
                    break
                factor = (factor << 1) | ((comp[bp >> 3] >> (7 - (bp & 7))) & 1)
                bp += 1
            code = ((1 << ml) - 2) + factor
            if delta > 0:                     # 앞서 거리를 받아 뒀으면 길이가 온 것
                for _ in range(3 + code):
                    if len(out) >= out_len:
                        break
                    out.append(out[-delta] if len(out) >= delta else 0)
                delta = 0
            elif code < 256:
                out.append(self.dict[code])
            else:
                delta = code - 256
        return out


# ------------------------------------------------------------------ 인코더

class _BitOut:
    def __init__(self):
        self.buf = bytearray()
        self.bit = 0                          # 다음에 쓸 비트(0 = MSB)

    def _put(self, one):
        if self.bit == 0:
            self.buf.append(0)
        if one:
            self.buf[-1] |= 0x80 >> self.bit
        self.bit = (self.bit + 1) & 7

    def code(self, num):
        m = 0
        while num >= ((2 << (m + 1)) - 2):
            m += 1
        rest = num - ((2 << m) - 2)
        for i in range(m, -1, -1):            # 상부: 1 을 m개 쓰고 0
            self._put(i != 0)
        for i in range(m, -1, -1):            # 하부: rest 를 m+1 비트로
            self._put(rest & (1 << i))


def encode_part(data):
    """한 파트를 LS11 비트스트림으로. 항등 사전이라 바이트값이 곧 코드다."""
    bo = _BitOut()
    for b in data:
        bo.code(b)
    return bytes(bo.buf)


def build(parts, magic=b"Ls12"):
    """원본 바이트열 리스트 -> LS11/Ls12 파일 바이트열."""
    hdr = bytearray(0x110)
    hdr[0:4] = magic
    hdr[4:16] = b"\x20" * 12                  # 원본 파일들이 공백으로 패딩돼 있다
    for i in range(256):
        hdr[0x10 + i] = i                     # 항등 사전
    blobs = [encode_part(p) for p in parts]
    off = 0x110 + 12 * len(parts) + 4         # 표 + 종료표시 다음이 데이터 시작
    out = bytearray(hdr)
    for p, b in zip(parts, blobs):
        out += struct.pack(">III", len(b), len(p), off)
        off += len(b)
    out += b"\x00\x00\x00\x00"
    for b in blobs:
        out += b
    return bytes(out)


# ------------------------------------------------------------------ CLI

def _list(path):
    f = Ls12(path)
    print("%s  매직 %s  파트 %d개" % (path, f.magic.decode(), len(f.parts)))
    for i, (c, u, o) in enumerate(f.parts):
        print("  [%3d] 압축 %6d  원본 %6d  오프셋 0x%06X" % (i, c, u, o))


def _unpack(path, outdir):
    f = Ls12(path)
    os.makedirs(outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(path))[0]
    for i in range(len(f.parts)):
        p = os.path.join(outdir, "%s.%03d" % (base, i))
        open(p, "wb").write(bytes(f.decode(i)))
    print("%d개 파트를 %s 에 풀었다" % (len(f.parts), outdir))


def _pack(indir, outpath):
    names = sorted(n for n in os.listdir(indir) if n.rsplit(".", 1)[-1].isdigit())
    if not names:
        raise SystemExit("번호 파일(.000 .001 …)이 없다: %s" % indir)
    parts = [open(os.path.join(indir, n), "rb").read() for n in names]
    open(outpath, "wb").write(build(parts))
    print("%d개 파트를 %s 로 묶었다 (%d바이트)" % (len(parts), outpath, os.path.getsize(outpath)))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "list":
        _list(argv[2])
    elif cmd == "unpack":
        _unpack(argv[2], argv[3] if len(argv) > 3 else "out")
    elif cmd == "pack":
        _pack(argv[2], argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
