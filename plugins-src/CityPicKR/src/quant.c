#include "quant.h"
#include <windows.h>

// 색 줄이기 — quant.h 의 설명 참고.
//
// 두 갈래로 간다.
//   쓰인 색이 얼마 없으면  그 색들을 그대로 팔레트에 담는다(왕복해도 한 점도 안 틀린다).
//   색이 많으면            15비트(5·5·5) 히스토그램을 미디언컷으로 잘라 86색을 고른다.
// 어느 쪽이든 마지막 색인 붙이기는 같다 — 고정색 + 새 팔레트를 다 후보로 두고 가장
// 가까운 것을 찾는다. 그래서 고정색으로 딱 맞는 자리는 팔레트를 한 칸도 안 쓴다.

#define HASH_BITS  14
#define HASH_N     (1 << HASH_BITS)          // 16384 슬롯
#define UNIQ_MAX   6000                      // 이보다 색이 많으면 미디언컷으로 간다

#define HIST_BITS  5
#define HIST_N     (1 << (HIST_BITS * 3))    // 32768 칸

// 팔레트에 담는 색은 6비트 DAC 값(하위 두 비트가 잘린다). 원본 CITYCG.CDS 의 팔레트가
// 전부 4의 배수인 것도 같은 이유다. 미리 잘라 두어야 넣은 뒤에 색이 밀리지 않는다.
#define DAC(v)  ((unsigned char)((v) & 0xFC))

typedef struct { unsigned key; unsigned n; } Slot;

static Slot          g_slot[HASH_N];
static unsigned      g_uniq[UNIQ_MAX];       // 쓰인 색(24비트)
static unsigned      g_uniqN[UNIQ_MAX];      // 그 색의 점 수
static unsigned int  g_hist[HIST_N];

// 쓰인 색을 모은다. UNIQ_MAX 를 넘으면 -1(색이 너무 많다).
static int CollectColors(const unsigned char* rgb, int npix)
{
    int i, n = 0;
    ZeroMemory(g_slot, sizeof(g_slot));
    for (i = 0; i < npix; i++) {
        unsigned c = ((unsigned)rgb[i*3+0] << 16) | ((unsigned)rgb[i*3+1] << 8) | rgb[i*3+2];
        unsigned h = (c * 2654435761u) >> (32 - HASH_BITS);
        for (;;) {
            if (g_slot[h].n == 0) {
                if (n >= UNIQ_MAX) return -1;
                g_slot[h].key = c; g_slot[h].n = 1;
                g_uniq[n] = c; g_uniqN[n] = 1; n++;
                break;
            }
            if (g_slot[h].key == c) { g_slot[h].n++; break; }
            h = (h + 1) & (HASH_N - 1);
        }
    }
    // 점 수는 해시 쪽에만 쌓였으니 목록으로 옮긴다(미디언컷 대신 쓸 때 쓰인다).
    for (i = 0; i < n; i++) {
        unsigned c = g_uniq[i];
        unsigned h = (c * 2654435761u) >> (32 - HASH_BITS);
        while (g_slot[h].key != c) h = (h + 1) & (HASH_N - 1);
        g_uniqN[i] = g_slot[h].n;
    }
    return n;
}

// ---- 미디언컷 ----
typedef struct { int r0, r1, g0, g1, b0, b1; unsigned long n; } Box;

#define HIDX(r,g,b) (((r) << (HIST_BITS*2)) | ((g) << HIST_BITS) | (b))

static unsigned long BoxSum(const Box* x)
{
    int r, g, b;
    unsigned long s = 0;
    for (r = x->r0; r <= x->r1; r++)
        for (g = x->g0; g <= x->g1; g++)
            for (b = x->b0; b <= x->b1; b++)
                s += g_hist[HIDX(r,g,b)];
    return s;
}

// 빈 가장자리를 잘라낸다. 남은 점이 없으면 0.
static int Shrink(Box* x)
{
    int r, g, b;
    int rr0 = 32, rr1 = -1, gg0 = 32, gg1 = -1, bb0 = 32, bb1 = -1;
    for (r = x->r0; r <= x->r1; r++)
        for (g = x->g0; g <= x->g1; g++)
            for (b = x->b0; b <= x->b1; b++) {
                if (!g_hist[HIDX(r,g,b)]) continue;
                if (r < rr0) rr0 = r; if (r > rr1) rr1 = r;
                if (g < gg0) gg0 = g; if (g > gg1) gg1 = g;
                if (b < bb0) bb0 = b; if (b > bb1) bb1 = b;
            }
    if (rr1 < 0) return 0;
    x->r0 = rr0; x->r1 = rr1; x->g0 = gg0; x->g1 = gg1; x->b0 = bb0; x->b1 = bb1;
    x->n = BoxSum(x);
    return 1;
}

// 가장 긴 축을 따라 점 수가 반이 되는 자리에서 자른다. 못 자르면 0.
static int Split(Box* x, Box* out)
{
    int lr = x->r1 - x->r0, lg = x->g1 - x->g0, lb = x->b1 - x->b0;
    int axis = (lr >= lg && lr >= lb) ? 0 : (lg >= lb ? 1 : 2);
    int lo = axis == 0 ? x->r0 : axis == 1 ? x->g0 : x->b0;
    int hi = axis == 0 ? x->r1 : axis == 1 ? x->g1 : x->b1;
    unsigned long half = x->n / 2, acc = 0;
    int cut = lo, i, r, g, b;

    if (hi <= lo) return 0;
    for (i = lo; i < hi; i++) {
        unsigned long slice = 0;
        for (r = (axis == 0 ? i : x->r0); r <= (axis == 0 ? i : x->r1); r++)
            for (g = (axis == 1 ? i : x->g0); g <= (axis == 1 ? i : x->g1); g++)
                for (b = (axis == 2 ? i : x->b0); b <= (axis == 2 ? i : x->b1); b++)
                    slice += g_hist[HIDX(r,g,b)];
        acc += slice;
        cut = i;
        if (acc >= half) break;
    }
    if (cut >= hi) cut = hi - 1;

    *out = *x;
    if (axis == 0)      { out->r0 = cut + 1; x->r1 = cut; }
    else if (axis == 1) { out->g0 = cut + 1; x->g1 = cut; }
    else                { out->b0 = cut + 1; x->b1 = cut; }
    if (!Shrink(x)) return 0;
    if (!Shrink(out)) return 0;
    return 1;
}

// 박스의 대표색(점 수로 가중한 평균).
static void BoxColor(const Box* x, unsigned char* out)
{
    int r, g, b;
    unsigned long n = 0, sr = 0, sg = 0, sb = 0;
    for (r = x->r0; r <= x->r1; r++)
        for (g = x->g0; g <= x->g1; g++)
            for (b = x->b0; b <= x->b1; b++) {
                unsigned long c = g_hist[HIDX(r,g,b)];
                if (!c) continue;
                n += c;
                sr += c * (unsigned long)(r * 8 + 4);
                sg += c * (unsigned long)(g * 8 + 4);
                sb += c * (unsigned long)(b * 8 + 4);
            }
    if (!n) { out[0] = out[1] = out[2] = 0; return; }
    out[0] = DAC(sr / n); out[1] = DAC(sg / n); out[2] = DAC(sb / n);
}

static int MedianCut(const unsigned char* rgb, int npix, int palN, unsigned char* palOut)
{
    static Box box[256];
    int nbox = 1, i;

    ZeroMemory(g_hist, sizeof(g_hist));
    for (i = 0; i < npix; i++)
        g_hist[HIDX(rgb[i*3+0] >> 3, rgb[i*3+1] >> 3, rgb[i*3+2] >> 3)]++;

    box[0].r0 = box[0].g0 = box[0].b0 = 0;
    box[0].r1 = box[0].g1 = box[0].b1 = 31;
    if (!Shrink(&box[0])) return 0;

    while (nbox < palN) {
        int pick = -1;
        unsigned long best = 0;
        for (i = 0; i < nbox; i++) {
            int lr = box[i].r1 - box[i].r0, lg = box[i].g1 - box[i].g0, lb = box[i].b1 - box[i].b0;
            if (lr + lg + lb == 0) continue;          // 더 못 자르는 박스
            if (box[i].n > best) { best = box[i].n; pick = i; }
        }
        if (pick < 0) break;
        if (!Split(&box[pick], &box[nbox])) { box[pick].r1 = box[pick].r0; box[pick].g1 = box[pick].g0; box[pick].b1 = box[pick].b0; continue; }
        nbox++;
    }

    for (i = 0; i < nbox; i++) BoxColor(&box[i], palOut + i * 3);
    for (; i < palN; i++) { palOut[i*3+0] = palOut[i*3+1] = palOut[i*3+2] = 0; }
    return nbox;
}

// ---- 색인 붙이기 ----
int Quant_Index(const unsigned char* rgb, int npix,
                const unsigned char* gamePal, int fixLo, int fixHi,
                int palBase, int palN,
                unsigned char* palOut, unsigned char* idxOut)
{
    int nuniq, i, exact = 1, np = 0;
    unsigned lastColor = 0xFFFFFFFFu;
    unsigned char lastIdx = 0;

    ZeroMemory(palOut, (unsigned)palN * 3);

    nuniq = CollectColors(rgb, npix);
    if (nuniq >= 0) {
        // 고정색으로 그대로 되는 색은 팔레트 칸을 안 쓴다. 남는 색만 담아 본다.
        for (i = 0; i < nuniq && np <= palN; i++) {
            unsigned c = g_uniq[i];
            int r = (int)(c >> 16), g = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
            int k, hit = 0;
            for (k = fixLo; k <= fixHi; k++)
                if (gamePal[k*3+0] == r && gamePal[k*3+1] == g && gamePal[k*3+2] == b) { hit = 1; break; }
            if (hit) continue;
            if (DAC(r) != r || DAC(g) != g || DAC(b) != b) exact = 0;   // 6비트로 잘리며 아주 조금 밀린다
            if (np < palN) { palOut[np*3+0] = DAC(r); palOut[np*3+1] = DAC(g); palOut[np*3+2] = DAC(b); }
            np++;
        }
        if (np > palN) nuniq = -1;      // 다 못 담는다 — 미디언컷으로
    }
    if (nuniq < 0) {
        MedianCut(rgb, npix, palN, palOut);
        exact = 0;
    }

    // 고정색 + 새 팔레트를 다 후보로 두고 가장 가까운 색인을 붙인다.
    // 같은 색이 잇달아 나오는 그림이라 직전 결과를 하나 기억해 두는 것만으로 훨씬 빨라진다.
    for (i = 0; i < npix; i++) {
        int r = rgb[i*3+0], g = rgb[i*3+1], b = rgb[i*3+2];
        unsigned c = ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
        int bestd = 0x7FFFFFFF, best = palBase, k;

        if (c == lastColor) { idxOut[i] = lastIdx; continue; }

        for (k = fixLo; k <= fixHi; k++) {
            int dr = r - gamePal[k*3+0], dg = g - gamePal[k*3+1], db = b - gamePal[k*3+2];
            int d = dr*dr + dg*dg + db*db;
            if (d < bestd) { bestd = d; best = k; }
        }
        if (bestd) for (k = 0; k < palN; k++) {
            int dr = r - palOut[k*3+0], dg = g - palOut[k*3+1], db = b - palOut[k*3+2];
            int d = dr*dr + dg*dg + db*db;
            if (d < bestd) { bestd = d; best = palBase + k; if (!d) break; }
        }
        idxOut[i] = (unsigned char)best;
        lastColor = c; lastIdx = (unsigned char)best;
    }
    return exact;
}
