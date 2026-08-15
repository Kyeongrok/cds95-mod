#include "patronpick.h"
#include "chardb.h"       // CharacterUtilKR/src — 후원자 표(이름·도시·자금·취향)
#include "patrons.h"      // CharacterUtilKR/src — 실행 중 등장연도(도감에서 고친 값이 여기 있다)
#include "livechar.h"     // CharacterUtilKR/src — 게임 안의 지금 연도
#include "warp_data.h"    // TradeUtilKR/src — kWarps[226] (도시 이름 + 워프 16바이트)

// 분류 번호(발견물 표 +0x04) -> 취향 비트(chardb.c kPatronPref).
// 지리0 역사1 보물2 종교3 교역품4 미신5 생물6 민족7
//   ->  지리0 역사1 보물7 종교2 교역품6 미신5 생물4 민족3
static const int kCatToPref[8] = { 0, 1, 7, 2, 6, 5, 4, 3 };

#define WARP_N ((int)(sizeof(kWarps)/sizeof(kWarps[0])))

static int g_row[PPICK_MAX];
static int g_warp[PPICK_MAX];
static int g_live[PPICK_MAX];
static int g_n = 0;
static int g_now = 0;      // 판정에 쓴 게임 연도
static int g_nowN = 0;     // 그 중 지금 만날 수 있는 수

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

// 등장연도는 실행 중 표(도감에서 고칠 수 있는 값)를 먼저 보고, 못 읽으면 구운 표를 쓴다.
static int AppearOf(int row)
{
    int y = Patron_Ready() ? Patron_Year(row) : 0;
    return y > 0 ? y : CharDb_PatronAppear(row);
}

// 지금 찾아갈 수 있는 후원자인가. 연도를 못 읽으면(세이브 전) 다 같이 PPICK_NOW 로 둔다 —
// 근거 없이 차례를 흔들지 않기 위해서다.
static int LiveOf(int row, int now)
{
    int ap, re;
    if (now <= 0) return PPICK_NOW;
    ap = AppearOf(row);
    re = CharDb_PatronRetire(row);
    if (re > 0 && now >= re) return PPICK_GONE;
    if (ap > 0 && now <  ap) return PPICK_LATER;
    return PPICK_NOW;
}

int PPick_Build(int cat)
{
    int i, bit;
    g_n = 0;
    g_nowN = 0;
    if (cat < 0 || cat > 7) return 0;

    Patron_Load();                  // 실패해도 된다 — 그러면 구운 등장연도를 쓴다
    g_now = LiveChar_Year();        // 세이브를 안 불러왔으면 0

    bit = kCatToPref[cat];
    for (i = 0; i < CharDb_PatronCount() && g_n < PPICK_MAX; i++) {
        if (!((CharDb_PatronPrefAt(i) >> bit) & 1)) continue;
        g_row[g_n] = i;
        g_warp[g_n] = WarpOf(CharDb_PatronCity(i));
        g_live[g_n] = LiveOf(i, g_now);
        if (g_live[g_n] == PPICK_NOW) g_nowN++;
        g_n++;
    }
    // 지금 만날 수 있는 쪽이 위로, 그 안에서는 자금이 많은 쪽이 위로.
    // (등장 전 -> 은퇴 순으로 뒤에 붙는다. 찾아가 봐야 없는 사람들이라 목록 끝이 맞다.)
    { int a, b;
      for (a = 1; a < g_n; a++) {
          int vr = g_row[a], vw = g_warp[a], vl = g_live[a];
          int vv = CharDb_PatronWealthAt(vr);
          for (b = a - 1; b >= 0; b--) {
              int bl = g_live[b];
              if (bl > vl) break;
              if (bl == vl && CharDb_PatronWealthAt(g_row[b]) >= vv) break;
              g_row[b + 1] = g_row[b]; g_warp[b + 1] = g_warp[b]; g_live[b + 1] = g_live[b];
          }
          g_row[b + 1] = vr; g_warp[b + 1] = vw; g_live[b + 1] = vl;
      } }
    return g_n;
}

int PPick_Count(void) { return g_n; }
int PPick_Row(int i)  { return (i >= 0 && i < g_n) ? g_row[i] : -1; }
int PPick_WarpIndex(int i) { return (i >= 0 && i < g_n) ? g_warp[i] : -1; }
int PPick_Live(int i) { return (i >= 0 && i < g_n) ? g_live[i] : PPICK_NOW; }
int PPick_Appear(int i) { return (i >= 0 && i < g_n) ? AppearOf(g_row[i]) : 0; }
int PPick_Retire(int i) { return (i >= 0 && i < g_n) ? CharDb_PatronRetire(g_row[i]) : 0; }
int PPick_LiveCount(void) { return g_nowN; }
int PPick_Year(void) { return g_now; }
