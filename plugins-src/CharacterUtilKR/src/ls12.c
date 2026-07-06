#include "ls12.h"
#include <stdio.h>

// LS11/LS12 (KOEI) 디코더. 참고: tzengyuxio/kaodata ls11.py
//   [0:4] 매직 "LS11"/"Ls12"
//   [16:272] 256바이트 사전
//   [272:] 파트 테이블 — 12바이트씩 (압축크기, 원본크기, 오프셋) big-endian, 0 4바이트로 종료
//   압축 파트: 가변길이 비트코드 -> code. code<256: dict[code] 출력. code>=256: 거리=code-256,
//   다음 code로 길이=3+code 만큼 뒤에서 복사.

static unsigned RdBE(const unsigned char* p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

int Ls12_Open(Ls12File* f, const char* path)
{
    FILE* fp; long n; unsigned pos;
    f->data = NULL; f->size = 0; f->count = 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n < 272) { fclose(fp); return 0; }
    f->data = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, n);
    if (!f->data) { fclose(fp); return 0; }
    if (fread(f->data, 1, n, fp) != (size_t)n) { fclose(fp); HeapFree(GetProcessHeap(),0,f->data); f->data=NULL; return 0; }
    fclose(fp);
    f->size = n;
    if (memcmp(f->data, "LS11", 4) != 0 && memcmp(f->data, "Ls12", 4) != 0) {
        HeapFree(GetProcessHeap(),0,f->data); f->data=NULL; return 0;
    }
    memcpy(f->dict, f->data + 16, 256);
    pos = 16 + 256;
    f->count = 0;
    while (pos + 12 <= (unsigned)f->size && f->count < 512) {
        if (RdBE(f->data + pos) == 0) break;
        f->comp[f->count]   = RdBE(f->data + pos);
        f->uncomp[f->count] = RdBE(f->data + pos + 4);
        f->off[f->count]    = RdBE(f->data + pos + 8);
        f->count++;
        pos += 12;
    }
    return f->count > 0;
}

void Ls12_Close(Ls12File* f)
{
    if (f->data) { HeapFree(GetProcessHeap(), 0, f->data); f->data = NULL; }
    f->count = 0;
}

int Ls12_DecodeFace(Ls12File* f, int index, unsigned char* out)
{
    const unsigned char* comp;
    unsigned complen, outlen, totalbits, bitpos, outpos, delta;
    if (index < 0 || index >= f->count) return 0;
    comp = f->data + f->off[index];
    complen = f->comp[index];
    outlen = f->uncomp[index];
    if (outlen > (unsigned)LS12_FACE_SZ) outlen = LS12_FACE_SZ;
    if (f->comp[index] == f->uncomp[index]) {           // 무압축 저장
        memcpy(out, comp, outlen);
        return 1;
    }
    totalbits = complen * 8;
    bitpos = 0; outpos = 0; delta = 0;
    while (outpos < outlen && bitpos < totalbits) {
        unsigned mask_len = 0, factor = 0, code, k;
        int bit;
        // unary: 1이 이어지는 동안 읽다가 0을 만나면 멈춤. mask_len = 읽은 비트수.
        do {
            bit = (comp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1;
            bitpos++; mask_len++;
        } while (bit && bitpos < totalbits);
        // factor: mask_len 비트
        for (k = 0; k < mask_len && bitpos < totalbits; k++) {
            factor = (factor << 1) | ((comp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1);
            bitpos++;
        }
        code = ((1u << mask_len) - 2u) + factor;
        if (delta > 0) {
            unsigned nc = 3 + code, i;
            for (i = 0; i < nc && outpos < outlen; i++) {
                out[outpos] = (outpos >= delta) ? out[outpos - delta] : 0;
                outpos++;
            }
            delta = 0;
        } else if (code < 256) {
            out[outpos++] = f->dict[code];
        } else {
            delta = code - 256;
        }
    }
    // 부족분 0채움
    while (outpos < (unsigned)LS12_FACE_SZ) out[outpos++] = 0;
    return 1;
}
