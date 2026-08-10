#include "patronpick.h"
#include "chardb.h"       // CharacterUtilKR/src — 후원자 표(이름·도시·자금·취향)
#include "warp_data.h"    // TradeUtilKR/src — kWarps[226] (도시 이름 + 워프 16바이트)

// 분류 번호(발견물 표 +0x04) -> 취향 비트(chardb.c kPatronPref).
// 지리0 역사1 보물2 종교3 교역품4 미신5 생물6 민족7
//   ->  지리0 역사1 보물7 종교2 교역품6 미신5 생물4 민족3
static const int kCatToPref[8] = { 0, 1, 7, 2, 6, 5, 4, 3 };

#define WARP_N ((int)(sizeof(kWarps)/sizeof(kWarps[0])))

static int g_row[PPICK_MAX];
static int g_warp[PPICK_MAX];
static int g_n = 0;

// 이름에서 빈칸을 뺀 뒤 견준다. 후원자 표와 워프 표의 도시 이름이 한 곳만 다르다
// (오포르트 / 오포르토) — 그런 것은 앞 세 글자로 찾는다.
static int SameCity(const wchar_t* a, const wchar_t* b)
{
    wchar_t x[64], y[64];
    int i = 0, j = 0;
    while (*a && i < 63) { if (*a != L' ') x[i++] = *a; a++; }
    while (*b && j < 63) { if (*b != L' ') y[j++] = *b; b++; }
    x[i] = 0; y[j] = 0;
    if (!lstrcmpW(x, y)) return 1;
    if (i >= 3 && j >= 3) { x[3] = 0; y[3] = 0; return !lstrcmpW(x, y); }
    return 0;
}

static int WarpOf(const wchar_t* city)
{
    int i;
    if (!city || !city[0]) return -1;
    for (i = 0; i < WARP_N; i++) if (SameCity(city, kWarps[i].city)) return i;
    return -1;
}

int PPick_Build(int cat)
{
    int i, bit;
    g_n = 0;
    if (cat < 0 || cat > 7) return 0;
    bit = kCatToPref[cat];
    for (i = 0; i < CharDb_PatronCount() && g_n < PPICK_MAX; i++) {
        if (!((CharDb_PatronPrefAt(i) >> bit) & 1)) continue;
        g_row[g_n] = i;
        g_warp[g_n] = WarpOf(CharDb_PatronCity(i));
        g_n++;
    }
    // 자금이 많은 쪽을 위로 — 후원을 받으러 가는 목록이라 그게 쓸모 있다.
    { int a, b;
      for (a = 1; a < g_n; a++) {
          int vr = g_row[a], vw = g_warp[a], vv = CharDb_PatronWealthAt(vr);
          for (b = a - 1; b >= 0; b--) {
              if (CharDb_PatronWealthAt(g_row[b]) >= vv) break;
              g_row[b + 1] = g_row[b]; g_warp[b + 1] = g_warp[b];
          }
          g_row[b + 1] = vr; g_warp[b + 1] = vw;
      } }
    return g_n;
}

int PPick_Count(void) { return g_n; }
int PPick_Row(int i)  { return (i >= 0 && i < g_n) ? g_row[i] : -1; }
int PPick_WarpIndex(int i) { return (i >= 0 && i < g_n) ? g_warp[i] : -1; }
