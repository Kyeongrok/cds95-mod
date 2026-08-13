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

// 매직·사전·파트 표를 읽는다. f->data / f->size 는 부르기 전에 채워 둔다. 성공 1.
static int ParseTable(Ls12File* f)
{
    unsigned pos;
    if (f->size < 272) return 0;
    if (memcmp(f->data, "LS11", 4) != 0 && memcmp(f->data, "Ls12", 4) != 0) return 0;
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

int Ls12_Open(Ls12File* f, const char* path)
{
    FILE* fp; long n;
    f->data = NULL; f->size = 0; f->count = 0; f->owns = 0;
    fp = fopen(path, "rb");
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n < 272) { fclose(fp); return 0; }
    f->data = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, n);
    if (!f->data) { fclose(fp); return 0; }
    if (fread(f->data, 1, n, fp) != (size_t)n) { fclose(fp); HeapFree(GetProcessHeap(),0,f->data); f->data=NULL; return 0; }
    fclose(fp);
    f->size = n;
    f->owns = 1;
    if (!ParseTable(f)) { HeapFree(GetProcessHeap(),0,f->data); f->data=NULL; f->owns=0; return 0; }
    return 1;
}

// 이미 메모리에 있는 아카이브를 그대로 읽는다(버퍼는 부르는 쪽이 계속 들고 있어야 한다).
// Ls12_Rewrite 로 만든 결과에 파트를 하나 더 갈아 끼울 때 쓴다 — 도시 그림처럼 한 장이
// (그림 + 팔레트) 두 파트인 경우 임시 파일을 거치지 않고 이어서 고칠 수 있다.
int Ls12_OpenMem(Ls12File* f, const unsigned char* buf, unsigned len)
{
    f->data = (unsigned char*)buf; f->size = (long)len; f->count = 0; f->owns = 0;
    if (!buf || !ParseTable(f)) { f->data = NULL; f->size = 0; return 0; }
    return 1;
}

unsigned Ls12_PartSize(Ls12File* f, int index)
{
    if (index < 0 || index >= f->count) return 0;
    return f->uncomp[index];
}

void Ls12_Close(Ls12File* f)
{
    // Ls12_OpenMem 으로 연 것은 버퍼가 남의 것이라 놓지 않는다.
    if (f->data && f->owns) HeapFree(GetProcessHeap(), 0, f->data);
    f->data = NULL; f->owns = 0;
    f->count = 0;
}

// 파트 하나를 있는 그대로 푼다. 얼굴이 아니라 이벤트 스크립트 같은 것도 쓰므로
// 크기를 얼굴 고정치로 자르지 않는다. 실제로 쓴 바이트 수를 돌려준다(실패 0).
int Ls12_DecodePart(Ls12File* f, int index, unsigned char* out, unsigned outcap)
{
    const unsigned char* comp;
    unsigned complen, outlen, totalbits, bitpos, outpos, delta;
    if (index < 0 || index >= f->count) return 0;
    // 파트 테이블의 오프셋/길이는 파일에서 그대로 읽은 값이라 파일 밖을 가리킬 수 있다.
    // 검사 없이 쓰면 힙 밖을 읽어 프로세스가 죽는다.
    if (!f->data || f->off[index] >= (unsigned)f->size) return 0;
    if (f->comp[index] > (unsigned)f->size - f->off[index]) return 0;
    comp = f->data + f->off[index];
    complen = f->comp[index];
    outlen = f->uncomp[index];
    if (outlen > outcap) outlen = outcap;
    if (f->comp[index] == f->uncomp[index]) {           // 무압축 저장
        memcpy(out, comp, outlen);
        return (int)outlen;
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
    return (int)outpos;
}

// 그 파트가 쓰는 가장 큰 코드값. 디코더와 같은 걸음으로 비트만 읽어 훑는다.
unsigned Ls12_PartMaxCode(Ls12File* f, int index)
{
    const unsigned char* comp;
    unsigned complen, outlen, totalbits, bitpos, outpos, delta, best = 0;

    if (index < 0 || index >= f->count) return 0;
    if (!f->data || f->off[index] >= (unsigned)f->size) return 0;
    if (f->comp[index] > (unsigned)f->size - f->off[index]) return 0;
    if (f->comp[index] == f->uncomp[index]) return 0;      // 무압축이면 코드가 없다

    comp = f->data + f->off[index];
    complen = f->comp[index];
    outlen = f->uncomp[index];
    totalbits = complen * 8;
    bitpos = 0; outpos = 0; delta = 0;
    while (outpos < outlen && bitpos < totalbits) {
        unsigned mask_len = 0, factor = 0, code, k;
        int bit;
        do {
            bit = (comp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1;
            bitpos++; mask_len++;
        } while (bit && bitpos < totalbits);
        for (k = 0; k < mask_len && bitpos < totalbits; k++) {
            factor = (factor << 1) | ((comp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1);
            bitpos++;
        }
        code = ((1u << mask_len) - 2u) + factor;
        if (code > best) best = code;
        if (delta > 0)        { outpos += 3 + code; delta = 0; }
        else if (code < 256)  { outpos++; }
        else                  { delta = code - 256; }
    }
    return best;
}

// ---- 인코더 ----
// 디코더를 거꾸로 돌린 것. 수 num 을 상부(1을 m개 쓰고 0) + 하부(나머지를 m+1 비트)로 낸다.
// 사전이 항등이라 바이트값 자체가 코드다. 255 가 16비트라 결과가 원본의 2배 남짓 된다.
typedef struct { unsigned char* buf; unsigned cap, len; int bit; int over; } BitOut;

static void BoPut(BitOut* b, int one)
{
    if (b->bit == 0) {
        if (b->len >= b->cap) { b->over = 1; return; }
        b->buf[b->len++] = 0;
    }
    if (one) b->buf[b->len - 1] |= (unsigned char)(0x80 >> b->bit);
    b->bit = (b->bit + 1) & 7;
}

static void BoCode(BitOut* b, unsigned num)
{
    unsigned m = 0, rest;
    int i;
    while (num >= ((2u << (m + 1)) - 2u)) m++;
    rest = num - ((2u << m) - 2u);
    for (i = (int)m; i >= 0; i--) BoPut(b, i != 0);
    for (i = (int)m; i >= 0; i--) BoPut(b, (int)(rest & (1u << i)));
}

// 뒤복사(LZ)까지 하는 인코딩 본체. 정의는 아래 "파트 하나만 갈아 끼우기" 절에 있다.
static void EncodePart(BitOut* b, const unsigned char* raw, unsigned n, const unsigned char* rev);

static void WrBE(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

unsigned Ls12_BuildCap(const unsigned* lens, int count)
{
    unsigned total = 0;
    int i;
    for (i = 0; i < count; i++) total += lens[i];
    // 바이트당 최대 16비트 = 2바이트. 여유로 조금 더 얹는다.
    return 0x110u + 12u * (unsigned)count + 4u + total * 2u + 64u;
}

unsigned Ls12_Build(unsigned char* const* parts, const unsigned* lens, int count,
                    unsigned char* out, unsigned outcap)
{
    unsigned hdr = 0x110u + 12u * (unsigned)count + 4u;
    unsigned pos = hdr;
    unsigned char rev[256];
    int i;

    if (count <= 0 || outcap < hdr) return 0;
    memset(out, 0, hdr);
    memcpy(out, "Ls12", 4);
    memset(out + 4, 0x20, 12);                 // 원본 파일들이 공백으로 패딩돼 있다
    for (i = 0; i < 256; i++) out[0x10 + i] = (unsigned char)i;   // 항등 사전
    for (i = 0; i < 256; i++) rev[i] = (unsigned char)i;          // 그래서 되짚기도 항등

    for (i = 0; i < count; i++) {
        BitOut b;
        unsigned clen;
        b.buf = out + pos; b.cap = outcap - pos; b.len = 0; b.bit = 0; b.over = 0;
        EncodePart(&b, parts[i], lens[i], rev);
        if (b.over) return 0;
        clen = b.len;
        if (clen >= lens[i]) {                 // 안 줄면 날것으로(무압축 저장)
            if (lens[i] > outcap - pos) return 0;
            memcpy(out + pos, parts[i], lens[i]);
            clen = lens[i];
        }
        WrBE(out + 0x110 + i * 12,     clen);
        WrBE(out + 0x110 + i * 12 + 4, lens[i]);
        WrBE(out + 0x110 + i * 12 + 8, pos);
        pos += clen;
    }
    return pos;
}

// ---- 파트 하나만 갈아 끼우기 ----
// 자세한 뜻은 ls12.h 참고. 핵심은 원본 사전을 그대로 두는 것이다 — 그래야 손대지 않은
// 파트의 압축 바이트를 그냥 옮겨도 그대로 풀린다.

// 되풀이를 뒤복사(LZ)로 낸다. 코드 하나가 최대 16비트라 그냥 내면 파일이 두 배로 붇는데,
// 게임은 파일마다 그 파일의 관례에 맞춘 버퍼로 읽는 듯해서 파트가 원본보다 커지면 죽는다
// (CITYCG.CDS 로 실제로 겪었다 — ls12.h 의 경고 참고). 그래서 원본 코에이 인코더처럼
// 앞을 되짚어 같은 구간을 찾는다.
//
// 뒤복사 한 번은 (거리 코드 256+d) + (길이 코드 L-3) 두 개다. 거리가 멀수록 코드가 커져
// 비트가 늘어나므로 가까운 것을 좋아하고, 길이 3부터 이득이다(리터럴 셋보다 짧다).
// 거리·길이의 상한은 취향이 아니라 게임이 받는 범위다. 원본 파일들의 압축 스트림을 훑어
// 실제로 쓰인 값을 재 보면 코에이 인코더는 거리를 8191 까지만 쓰고(코드 8447, 상부 13비트),
// 길이는 512 까지 쓴다. 욕심내서 거리 16382 까지 쓴 파일을 넣었더니 크기는 원본보다
// 작은데도 그 도시에 들어갈 때 게임이 죽었다 — 게임 디코더가 13비트까지만 받는 듯하다.
// 늘리지 말 것.
#define LZ_MIN_MATCH 3
#define LZ_MAX_MATCH 512
#define LZ_WINDOW    8191
#define LZ_CHAINS    1024          // 한 자리에서 훑어 볼 후보 수(속도와 압축률의 절충)
#define LZ_MAX_CODE  8447          // 원본이 쓰는 가장 큰 코드(= 256 + 8191)
#define LZ_HASH_BITS 15
#define LZ_HASH_N    (1 << LZ_HASH_BITS)

// 코드 하나가 차지하는 비트 수. BoCode 와 같은 셈이다.
static unsigned CodeBits(unsigned num)
{
    unsigned m = 0;
    while (num >= ((2u << (m + 1)) - 2u)) m++;
    return 2u * (m + 1u);
}

static unsigned HashAt(const unsigned char* p)
{
    return (((unsigned)p[0] << 10) ^ ((unsigned)p[1] << 5) ^ (unsigned)p[2]) & (LZ_HASH_N - 1);
}

// pos 에서 가장 이득이 큰 뒤복사를 찾는다. 찾으면 길이, 못 찾으면 0.
// 긴 것이 늘 이득은 아니다 — 거리가 멀면 거리 코드가 커지므로, 짧아도 가까운 쪽이
// 쌀 때가 있다. 그래서 "리터럴로 냈을 때의 비트 - 뒤복사 비트"를 재서 고른다.
// litCum[i] = 앞에서부터 i 바이트를 리터럴로 냈을 때의 비트 수(누적합).
static unsigned FindMatch(const unsigned char* raw, unsigned n, unsigned pos,
                          const int* head, const int* prev, const unsigned* litCum,
                          unsigned* distOut, int* gainOut)
{
    unsigned bestLen = 0, bestDist = 0;
    int bestGain = 0;
    int cand;
    int chain = LZ_CHAINS;
    unsigned maxLen = n - pos, prevLen = 0;

    if (maxLen > LZ_MAX_MATCH) maxLen = LZ_MAX_MATCH;
    if (maxLen < LZ_MIN_MATCH) return 0;

    for (cand = head[HashAt(raw + pos)]; cand >= 0 && chain-- > 0; cand = prev[cand]) {
        unsigned dist = pos - (unsigned)cand, len = 0, cost;
        int gain;
        if (dist == 0 || dist > LZ_WINDOW) break;
        // 겹쳐도 된다 — 디코더가 한 바이트씩 앞을 되짚어 채우므로 거리 1 이 곧 되풀이다.
        while (len < maxLen && raw[cand + len] == raw[pos + len]) len++;
        if (len < LZ_MIN_MATCH) continue;
        cost = CodeBits(256 + dist) + CodeBits(len - LZ_MIN_MATCH);
        gain = (int)(litCum[pos + len] - litCum[pos]) - (int)cost;
        if (gain > bestGain) { bestGain = gain; bestLen = len; bestDist = dist; }
        // 뒤로 갈수록 거리가 머니, 길이가 더 안 늘면 더 볼 것도 없다.
        if (len >= maxLen) break;
        if (len > prevLen) prevLen = len;
    }
    if (!bestLen) return 0;
    *distOut = bestDist;
    if (gainOut) *gainOut = bestGain;
    return bestLen;
}

static void EncodePart(BitOut* b, const unsigned char* raw, unsigned n, const unsigned char* rev)
{
    int *head = NULL, *prev = NULL;
    unsigned *litCum = NULL;
    unsigned i, k;

    head   = (int*)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * LZ_HASH_N);
    prev   = (int*)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * (n ? n : 1));
    litCum = (unsigned*)HeapAlloc(GetProcessHeap(), 0, sizeof(unsigned) * (n + 1));
    if (!head || !prev || !litCum) {   // 자리를 못 잡으면 되풀이 없이 그냥 낸다(크기만 커진다)
        if (head) HeapFree(GetProcessHeap(), 0, head);
        if (prev) HeapFree(GetProcessHeap(), 0, prev);
        if (litCum) HeapFree(GetProcessHeap(), 0, litCum);
        for (i = 0; i < n && !b->over; i++) BoCode(b, rev[raw[i]]);
        return;
    }
    for (k = 0; k < LZ_HASH_N; k++) head[k] = -1;
    litCum[0] = 0;
    for (i = 0; i < n; i++) litCum[i + 1] = litCum[i] + CodeBits(rev[raw[i]]);

    i = 0;
    while (i < n && !b->over) {
        unsigned dist = 0, len = 0;
        int gain = 0;

        if (i + LZ_MIN_MATCH <= n) len = FindMatch(raw, n, i, head, prev, litCum, &dist, &gain);

        // 한 칸 미뤄 보고 그쪽 이득이 더 크면 지금은 리터럴로 흘린다(lazy matching).
        if (len >= LZ_MIN_MATCH && i + 1 + LZ_MIN_MATCH <= n) {
            unsigned d2 = 0, l2;
            int g2 = 0;
            unsigned h = HashAt(raw + i);
            prev[i] = head[h]; head[h] = (int)i;     // 다음 자리를 보려면 이 자리를 먼저 넣는다
            l2 = FindMatch(raw, n, i + 1, head, prev, litCum, &d2, &g2);
            if (l2 && g2 > gain) {
                BoCode(b, rev[raw[i]]);
                i++;
                continue;
            }
        } else if (i + LZ_MIN_MATCH <= n) {
            unsigned h = HashAt(raw + i);
            prev[i] = head[h]; head[h] = (int)i;
        }

        // FindMatch 는 이득이 남는 것만 돌려준다(gain > 0).
        if (len >= LZ_MIN_MATCH) {
            unsigned j;
            BoCode(b, 256 + dist);
            BoCode(b, len - LZ_MIN_MATCH);
            // 건너뛰는 자리들도 표에 넣어 둔다 — 안 넣으면 뒤에서 후보를 놓친다.
            for (j = 1; j < len; j++) {
                unsigned p = i + j, h;
                if (p + LZ_MIN_MATCH > n) break;
                h = HashAt(raw + p);
                prev[p] = head[h]; head[h] = (int)p;
            }
            i += len;
            continue;
        }
        BoCode(b, rev[raw[i]]);
        i++;
    }

    HeapFree(GetProcessHeap(), 0, head);
    HeapFree(GetProcessHeap(), 0, prev);
    HeapFree(GetProcessHeap(), 0, litCum);
}

unsigned Ls12_RewriteCap(const Ls12File* f, unsigned rawlen)
{
    unsigned total = 0;
    int i;
    if (!f) return 0;
    for (i = 0; i < f->count; i++) total += f->comp[i];
    // 헤더 + 옮길 압축 바이트 + 새 파트(최악의 경우 바이트당 2바이트) + 여유
    return 0x110u + 12u * (unsigned)(f->count + 1) + 4u + total + rawlen * 2u + 64u;
}

unsigned Ls12_Rewrite(const Ls12File* f, int index, const unsigned char* raw, unsigned rawlen,
                      unsigned char* out, unsigned outcap)
{
    return Ls12_RewriteEx(f, index, raw, rawlen, out, outcap, 0);
}

unsigned Ls12_RewriteEx(const Ls12File* f, int index, const unsigned char* raw, unsigned rawlen,
                        unsigned char* out, unsigned outcap, int forceRaw)
{
    unsigned char rev[256];
    int seen[256];
    int count, tgt, i;
    unsigned hdr, pos;

    if (!f || !f->data || !raw || !rawlen) return 0;
    if (index >= f->count) return 0;
    count = f->count + (index < 0 ? 1 : 0);
    tgt   = index < 0 ? f->count : index;
    hdr   = 0x110u + 12u * (unsigned)count + 4u;
    if (outcap < hdr) return 0;

    for (i = 0; i < 256; i++) seen[i] = 0;
    for (i = 0; i < 256; i++) { rev[f->dict[i]] = (unsigned char)i; seen[f->dict[i]] = 1; }
    for (i = 0; i < 256; i++) if (!seen[i]) return 0;   // 순열이 아니면 인코딩 불가

    memset(out, 0, hdr);
    memcpy(out, f->data, 0x110);       // 매직 + 패딩 + 사전을 그대로 물려받는다

    pos = hdr;
    for (i = 0; i < count; i++) {
        unsigned clen, ulen;
        if (i == tgt) {
            ulen = rawlen;
            clen = rawlen + 1;               // 아래에서 줄었는지 견주는 초깃값
            if (!forceRaw) {
                BitOut b;
                b.buf = out + pos; b.cap = outcap - pos; b.len = 0; b.bit = 0; b.over = 0;
                EncodePart(&b, raw, rawlen, rev);
                if (b.over) return 0;
                clen = b.len;
            }
            // 안 줄었거나 날것으로 담으라고 했으면 그대로 둔다(압축크기 == 원본크기 = 무압축 저장).
            // 원본 파일들도 그렇게 담은 파트가 있다 — CITYCG.CDS 의 팔레트 226개가 전부 그렇다.
            if (clen >= rawlen) {
                if (rawlen > outcap - pos) return 0;
                memcpy(out + pos, raw, rawlen);
                clen = rawlen;
            }
        } else {
            clen = f->comp[i]; ulen = f->uncomp[i];
            if (f->off[i] > (unsigned)f->size || clen > (unsigned)f->size - f->off[i]) return 0;
            if (pos > outcap || clen > outcap - pos) return 0;
            memcpy(out + pos, f->data + f->off[i], clen);
        }
        WrBE(out + 0x110 + i * 12,     clen);
        WrBE(out + 0x110 + i * 12 + 4, ulen);
        WrBE(out + 0x110 + i * 12 + 8, pos);
        pos += clen;
    }
    return pos;
}

int Ls12_VerifyPart(const unsigned char* buf, unsigned buflen, int index,
                    const unsigned char* raw, unsigned rawlen)
{
    Ls12File t;
    unsigned char* tmp;
    unsigned pos;
    int n, ok;

    if (!buf || buflen < 0x114 || !raw) return 0;
    ZeroMemory(&t, sizeof(t));
    t.data = (unsigned char*)buf;      // 읽기만 한다. Ls12_Close 를 부르지 않으므로 안전하다
    t.size = (long)buflen;
    memcpy(t.dict, buf + 16, 256);
    pos = 0x110;
    while (pos + 12 <= buflen && t.count < 512) {
        if (RdBE(buf + pos) == 0) break;
        t.comp[t.count]   = RdBE(buf + pos);
        t.uncomp[t.count] = RdBE(buf + pos + 4);
        t.off[t.count]    = RdBE(buf + pos + 8);
        t.count++;
        pos += 12;
    }
    if (index < 0) index = t.count - 1;
    if (index < 0 || index >= t.count) return 0;
    if (t.uncomp[index] != rawlen) return 0;

    tmp = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, rawlen);
    if (!tmp) return 0;
    n = Ls12_DecodePart(&t, index, tmp, rawlen);
    ok = (n == (int)rawlen) && (memcmp(tmp, raw, rawlen) == 0);
    HeapFree(GetProcessHeap(), 0, tmp);
    return ok;
}

int Ls12_DecodeFace(Ls12File* f, int index, unsigned char* out)
{
    int n = Ls12_DecodePart(f, index, out, (unsigned)LS12_FACE_SZ);
    if (!n) return 0;
    while (n < LS12_FACE_SZ) out[n++] = 0;   // 부족분 0채움
    return 1;
}
