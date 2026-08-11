#include "trade.h"
#include <commctrl.h>
#include <windowsx.h>
#include <string.h>
#include <stdlib.h>
#include "cities_data.h"
#include "item_names.h"
#include "goods_names.h"
#include "warp_data.h"
#include "itempic.h"        // 교역품 그림 — CharacterUtilKR/src 의 ITEM.CDS 디코더를 같이 빌드한다

// TradeUtilKR — 한국어판 전용 "교역" 메뉴 + 시세 일람 창.
// 게임 메뉴바에 항목을 추가하고(서브클래싱으로 클릭 가로챔), 클릭 시 전 도시 목록을 표시한다.
//
// Phase 2a: 도시명/문화권/시설(임베드 cities_data.h)로 리스트뷰를 채운다.
// Phase 2b(예정): 국적/시세/규모/투자액/방문·발견을 게임 메모리에서 읽어 컬럼 추가.

#define ID_TRADE_SISE 0xB101
#define ID_TRADE_GOODS 0xB102       // fb21: 교역품 관리
#define ID_TRADE_WARP  0xB103       // 워프 창 — 226개를 지역 서브메뉴로 뒤지지 않고 찾아서 간다
#define ID_WARP_BASE  0xC000        // 워프 메뉴 항목 ID = ID_WARP_BASE + kWarps 인덱스
#define WC_SISE       L"TradeUtilKR_Sise"
#define CITY_COUNT    (int)(sizeof(kCities)/sizeof(kCities[0]))
#define WARP_COUNT    (int)(sizeof(kWarps)/sizeof(kWarps[0]))
#define WARP_REGION_MAX 16          // 서로 다른 지역 이름 수(지금 11개)

// fb14: 순간이동(워프). ce/CDS_95.CT "순간이동용" = 현재 위치를 담는 16바이트 @ 0x005B63A8.
//   목적지 도시의 16바이트를 여기에 쓰면 그 도시로 이동한다(현재값이 목록의 현위치와 일치함을 확인).
#define WARP_ADDR     0x005B63A8u

static void RecentWarped(int city);   // 아래 "최근 방문한 도시" 에 있다

// 시세 일람에서 줄을 두 번 누르면 그 도시로 가고, 항해 중이면 들어갈지도 묻는다.
// 그 일은 우리 창 안에서 하지 않고 게임 창에 부쳐서 게임 스레드가 제 차례에 하게 한다
// (게임 대화상자가 우리 팝업 창 프로시저 안쪽에서 열리지 않도록). 아래 "도시 진입" 참고.
static UINT g_msgGoCity = 0;

// kWarps[i] 의 16바이트를 워프 주소에 써서 해당 도시로 순간이동.
static void DoWarp(int i)
{
    void* dst = (void*)WARP_ADDR;
    DWORD old;
    if (i < 0 || i >= WARP_COUNT) return;
    if (IsBadWritePtr(dst, 16)) return;
    if (VirtualProtect(dst, 16, PAGE_READWRITE, &old))
    {
        memcpy(dst, kWarps[i].b, 16);
        VirtualProtect(dst, 16, old, &old);
    }
    else
    {
        memcpy(dst, kWarps[i].b, 16);
    }
    RecentWarped(i);      // 워프한 곳도 "다녀온 곳"이다 — 게임이 현재도시를 갱신하기 전에 넣어 둔다
}

// ---------------- 최근 워프한 도시 ----------------
//
// 226개를 지역별로 훑는 것이 번거로워서, 워프로 다녀온 곳을 메뉴 맨 위에 모아 둔다.
// 이 메뉴로 간 곳만 담는다(배로 정박한 것은 안 담는다) — "아까 갔던 데로 다시"가 쓸모라서다.
// 같은 도시를 다시 고르면 맨 위로 올라오고, 12개가 차면 가장 오래된 것부터 밀려난다.
// 목록은 게임을 켜 둔 동안만 들고 있는다(파일로 남기지 않는다).
#define RECENT_MAX 12
static int   g_recent[RECENT_MAX];
static int   g_recentN = 0;
static HMENU g_recentMenu = NULL;

static void RecentPush(int city)
{
    int i, j;
    if (city < 0 || city >= WARP_COUNT) return;
    for (i = 0; i < g_recentN; i++) if (g_recent[i] == city) break;
    if (i == 0 && g_recentN > 0) return;                  // 이미 맨 앞이다
    if (i == g_recentN)                                   // 목록에 없던 도시
    {
        if (g_recentN < RECENT_MAX) g_recentN++;
        else i = RECENT_MAX - 1;                          // 꽉 찼으면 맨 뒤를 밀어낸다
    }
    for (j = i; j > 0; j--) g_recent[j] = g_recent[j - 1];
    g_recent[0] = city;
}

// 워프로 간 곳만 담는다. 배로 정박한 것까지 잡으려면 게임의 "현재 도시"를 계속 들여다봐야
// 하는데, 이 목록의 쓸모는 "아까 워프했던 데로 다시" 라서 그것만으로 충분하다.
static void RecentWarped(int city)
{
    RecentPush(city);
}

// 서브메뉴를 펼치기 직전(WM_INITMENUPOPUP, 게임 UI 스레드)에 다시 만든다.
// 메뉴를 딴 스레드에서 건드리지 않으려고 이 시점으로 미뤄 둔 것이다.
static void RebuildRecentMenu(void)
{
    int i;
    if (!g_recentMenu) return;
    while (GetMenuItemCount(g_recentMenu) > 0) DeleteMenu(g_recentMenu, 0, MF_BYPOSITION);
    if (g_recentN <= 0)
    {
        AppendMenuW(g_recentMenu, MF_STRING | MF_GRAYED, 0, L"(아직 없음)");
        return;
    }
    for (i = 0; i < g_recentN; i++)
        AppendMenuW(g_recentMenu, MF_STRING, ID_WARP_BASE + g_recent[i], kWarps[g_recent[i]].city);
}

// 게임 라이브 메모리: 도시별 시세 배열 (2026-07-03 배열 시그니처 스캔으로 확정).
//   시세 = u16 @ (SISE_BASE + 도시ID * CITY_STRIDE),  도시0=리스본.
//   226/226 슬롯이 90~105 범위, 현재도시(리스본)=100 으로 검증. 도시 구조체 크기=92바이트.
// 플러그인은 cds_95.exe 프로세스 내부에서 실행되므로 절대주소를 직접 역참조한다.
#define SISE_BASE     0x005863B4u
#define CITY_STRIDE   92

// 도시 구조체(stride 92) 내 필드 오프셋 — SISE_BASE(=시세) 기준. ce/CDS_95.CT 라벨로 확정.
//   규모(0~7)    : -4   (i32)
//   시세         :  0   (i32; 값이 작아 u16 로도 읽힘)
//   건물수치      : +0x10 (u16 비트필드; struct+0x1C) — 건물 유무 플래그.
//   시장 아이템1~8: +20~+48 (i32 ×8) — 값 = item_names.h 인덱스, 빈 슬롯은 -1(로드시)/0.
#define SCALE_OFF     (-4)
// 상태 : +0x34 (도시struct +0x40). 게임 도시정보의 "상태" 줄과 같은 값이다 —
//   0x429D60 이 [도시+0x40] 을 돌려주고 0x429D70 이 그것으로 이름표 0x53CE60 을 찾는다.
#define STATE_OFF     0x34
#define BUILDING_OFF  0x10
#define MKT_ITEM_OFF  20
#define MKT_ITEM_MAX  8

// 건물수치 비트 (fb22 정답 대조로 확정 2026-07-03). 이전 '단가 bit4=조합'은 오류였음.
#define BIT_PORT      0   // 항구
#define BIT_TRADE     1   // 교역소
#define BIT_SHIPYARD  6   // 조선소
#define BIT_GUILD     7   // 조합
#define BIT_LIBRARY   8   // 도서관

// 도시 i 필드 주소 (프로세스 내부이므로 절대주소 직접 사용)
static unsigned CityField(int i, int off)
{
    return SISE_BASE + (unsigned)i * CITY_STRIDE + (unsigned)off;
}

// i32 안전 읽기. 매핑 안 돼 있으면 FALSE.
static BOOL ReadI32(unsigned addr, int* out)
{
    const int* p = (const int*)addr;
    if (IsBadReadPtr(p, sizeof(*p))) return FALSE;
    *out = *p; return TRUE;
}

// 도시 상태 이름. 게임 안의 이름표(0x0053CE60, 포인터 14개)를 그대로 옮겨 적었다.
// 살아 있는 값으로 대조: 226개 도시가 전부 0~13 안에 들었다(224개 통상, 2개 대조선).
#define STATE_N 14
static const wchar_t* kStateName[STATE_N] = {
    L"통상", L"전염병", L"기근", L"대기근", L"풍작", L"대풍작", L"대한파",
    L"혹서", L"노동력부족", L"전쟁", L"축제", L"호경기", L"불경기", L"대조선"
};
static const wchar_t* StateName(int v)
{
    return (v >= 0 && v < STATE_N) ? kStateName[v] : L"-";
}

// 도시 i 의 라이브 시세. 매핑 안 돼 있으면 -1.
static int ReadSise(int i)
{
    int v; return ReadI32(CityField(i, 0), &v) ? v : -1;
}

// 도시 i 의 상태(0~13). 매핑 안 돼 있으면 -1.
static int ReadState(int i)
{
    int v; return ReadI32(CityField(i, STATE_OFF), &v) ? v : -1;
}

// 도시 i 의 규모(0~7). 매핑 안 돼 있으면 -1.
static int ReadScale(int i)
{
    int v; return ReadI32(CityField(i, SCALE_OFF), &v) ? v : -1;
}

// 도시 i 의 건물수치(u16) 비트. 매핑 안 돼 있으면 -1.
static int ReadBuildingBit(int i, int bit)
{
    const unsigned short* p = (const unsigned short*)CityField(i, BUILDING_OFF);
    if (IsBadReadPtr(p, sizeof(*p))) return -1;
    return (int)((*p >> bit) & 1);
}

// 비트값(1/0/-1) → ○/×/-
static const wchar_t* BitMark(int b) { return b == 1 ? L"○" : (b == 0 ? L"×" : L"-"); }

// 도시 i 의 시장 아이템(유효한 것)들을 "이름, 이름 …" 으로 buf 에 채운다.
// 유효 판정: 0 < id < 286 (빈 슬롯 -1/0, 잠수폭탄(id0) 은 시장품이 아니므로 제외). 반환=유효 개수.
static int BuildMarketItems(int i, wchar_t* buf, int cap)
{
    int slot, cnt = 0; (void)cap;
    buf[0] = 0;
    for (slot = 0; slot < MKT_ITEM_MAX; slot++)
    {
        int v;
        if (!ReadI32(CityField(i, MKT_ITEM_OFF + slot * 4), &v)) continue;
        if (v > 0 && v < (int)(sizeof(kItemNames) / sizeof(kItemNames[0])))
        {
            if (cnt) lstrcatW(buf, L", ");
            lstrcatW(buf, kItemNames[v]);
            cnt++;
        }
    }
    return cnt;
}

static HINSTANCE g_hinst = NULL;
static HWND    g_hwnd = NULL;      // 게임 메인 창
static HWND    g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static HWND    g_siseWnd = NULL;   // 시세 일람 창
static HWND    g_list = NULL;
// 문화권 고르기 — 콤보박스를 쓰지 않는다. 게임 DirectDraw 화면 위에서는 자식 컨트롤이
// 불안정한데(CharacterUtilKR 이 위젯을 전부 직접 그리는 이유가 이것이다), 특히 COMBOBOX 는
// 펼칠 때 별도 최상위 창을 띄우고 캡처·포커스를 가져가 몇 초 뒤 게임이 죽었다.
// 그래서 닫힌 상자는 창에 직접 그리고, 펼친 목록만 우리 클래스의 자식 창으로 띄운다.
static HWND    g_sphereDrop = NULL; // 펼친 목록(우리가 그리는 자식 창). 닫혀 있으면 NULL
static HWND    g_hdr = NULL;       // 리스트뷰 헤더(오너드로우용 서브클래스)
static WNDPROC g_origHdr = NULL;
// 세 폰트는 시세 창과 교역품 창이 같이 쓴다. 예전에는 시세 창이 WM_CREATE 에서 만들고
// WM_DESTROY 에서 지웠는데, 그러면 두 가지가 어긋났다:
//   - 교역품 창을 시세 창보다 먼저 열면 셋 다 NULL 이라 시스템 기본 글꼴로 그려졌다.
//   - 두 창을 같이 띄우고 시세 창을 먼저 닫으면, 교역품 창의 컨트롤들이 WM_SETFONT 로
//     받아 둔 핸들이 그 순간 삭제돼 매달린 핸들이 됐다.
// 그래서 창 소유가 아니라 모듈 소유로 바꿨다 — 처음 쓸 때 한 번 만들고 지우지 않는다
// (프로세스가 끝나면 OS 가 회수한다. 셋뿐이라 들고 있어도 부담이 없다).
static HFONT   g_titleFont = NULL;
static HFONT   g_hdrFont = NULL;
static HFONT   g_listFont = NULL;

static void EnsureFonts(void)
{
    if (g_titleFont) return;
    g_titleFont = CreateFontW(-16, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
    g_hdrFont   = CreateFontW(-13, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
    g_listFont  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
}

// ---------------- 시세 일람 창 (여관 다이얼로그와 같은 세피아/브론즈 오너드로우) ----------------

// 창 레이아웃 (컬럼 총폭보다 좁으면 리스트뷰가 가로 스크롤)
#define WIN_W    672
#define WIN_H    560
#define FRAME    3        // 갈색 외곽 프레임 두께
#define TITLE_H  26       // 커스텀 타이틀바 높이
#define SISE_FILTER_H 30  // 타이틀바 아래 필터줄(문화권 고르기). 교역품 창 FILTER_H 와는 별개다

// 팔레트 (dialog.c 와 동일 계열)
#define COL_BG        RGB(150,130,105)
#define COL_FACE_TOP  RGB(216,201,176)
#define COL_FACE_BOT  RGB(158,138,113)
#define COL_LIGHT     RGB(238,228,208)
#define COL_DARK      RGB( 90, 75, 60)
#define COL_TEXT      RGB( 55, 40, 25)
#define COL_ROW_A     RGB(206,194,171)   // 짝수 행
#define COL_ROW_B     RGB(224,214,193)   // 홀수 행
#define COL_SEL_BG    RGB(150,120, 85)   // 선택 행 배경
#define COL_SEL_TX    RGB(250,244,228)   // 선택 행 글자

static void VGradient(HDC dc, RECT r, COLORREF top, COLORREF bot)
{
    int h = r.bottom - r.top, i;
    if (h <= 0) return;
    for (i = 0; i < h; i++)
    {
        int rr = GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * i / h;
        int gg = GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * i / h;
        int bb = GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * i / h;
        RECT line; HBRUSH br = CreateSolidBrush(RGB(rr, gg, bb));
        line.left = r.left; line.right = r.right; line.top = r.top + i; line.bottom = r.top + i + 1;
        FillRect(dc, &line, br); DeleteObject(br);
    }
}

static void Bevel(HDC dc, RECT r, BOOL sunken)
{
    COLORREF lt = sunken ? COL_DARK : COL_LIGHT;
    COLORREF dk = sunken ? COL_LIGHT : COL_DARK;
    HPEN pl = CreatePen(PS_SOLID, 1, lt), pd = CreatePen(PS_SOLID, 1, dk);
    HPEN old = (HPEN)SelectObject(dc, pl);
    MoveToEx(dc, r.left, r.bottom - 1, NULL);
    LineTo(dc, r.left, r.top); LineTo(dc, r.right - 1, r.top);
    SelectObject(dc, pd);
    LineTo(dc, r.right - 1, r.bottom - 1); LineTo(dc, r.left, r.bottom - 1);
    SelectObject(dc, old); DeleteObject(pl); DeleteObject(pd);
}

// 닫기 버튼 사각형 (타이틀바 우측)
static RECT CloseRect(RECT client)
{
    RECT cb; int cbw = 22, cbh = 18;
    cb.right = client.right - FRAME - 4;
    cb.left  = cb.right - cbw;
    cb.top   = FRAME + (TITLE_H - cbh) / 2;
    cb.bottom = cb.top + cbh;
    return cb;
}

// 컬럼 제목 — AddCol 과 동일. 헤더는 이 배열에서 직접 그린다(Header_GetItem 의 ANSI 확장
// 인코딩 깨짐을 피하기 위해 컨트롤에서 텍스트를 되읽지 않는다).
#define COL_COUNT 12
static const wchar_t* kCols[COL_COUNT] = {
    L"번호", L"도시명", L"문화권", L"규모", L"시세", L"상태", L"교역소", L"시장", L"도서관", L"조선소", L"조합", L"시장아이템"
};
static const int kColW[COL_COUNT] = { 40, 104, 78, 40, 46, 76, 52, 40, 52, 52, 44, 240 };

// ---- 필터 + 정렬 ----
// 줄마다 라이브 값(규모/시세/건물비트/시장아이템)을 한 번만 읽어 담아 둔다. 정렬할 때마다
// 게임 메모리를 다시 읽으면 비교 중에 값이 바뀔 수 있고 느리기도 하다.
typedef struct {
    int     id, scale, sise, state, trade, market, lib, yard, guild;
    wchar_t mkt[256];
} SiseRow;

static SiseRow g_sise[CITY_COUNT];      // 도시 번호 순서 그대로
static int     g_view[CITY_COUNT];      // 걸러 내고 정렬한 결과(g_sise 색인)
static int     g_viewN = 0;

static int  g_sortCol = -1;             // 정렬 기준 컬럼. -1 이면 정렬 안 함(도시 번호순)
static int  g_sortDir = 0;              // 1 오름차순 / -1 내림차순 / 0 안 함

// 문화권 목록 — kCities 에 나온 순서대로 모은다. 0번은 "전체".
#define SPHERE_MAX 24
static const wchar_t* g_sphere[SPHERE_MAX];
static int  g_sphereN = 0;
static int  g_sphereSel = 0;

static void BuildSpheres(void)
{
    int i, k;
    g_sphereN = 0;
    for (i = 0; i < CITY_COUNT && g_sphereN < SPHERE_MAX; i++) {
        const wchar_t* s = kCities[i].sphere;
        if (!s || !s[0]) continue;
        for (k = 0; k < g_sphereN; k++) if (lstrcmpW(g_sphere[k], s) == 0) break;
        if (k == g_sphereN) g_sphere[g_sphereN++] = s;
    }
}

// 라이브 값을 한 바퀴 읽어 담는다.
static void ReadRows(void)
{
    int i;
    for (i = 0; i < CITY_COUNT; i++) {
        SiseRow* r = &g_sise[i];
        r->id     = i;
        r->scale  = ReadScale(i);
        r->sise   = ReadSise(i);
        r->state  = ReadState(i);
        r->trade  = ReadBuildingBit(i, BIT_TRADE);
        r->lib    = ReadBuildingBit(i, BIT_LIBRARY);
        r->yard   = ReadBuildingBit(i, BIT_SHIPYARD);
        r->guild  = ReadBuildingBit(i, BIT_GUILD);
        r->market = BuildMarketItems(i, r->mkt, 256) > 0 ? 1 : 0;
    }
}

// 컬럼별 비교. 값이 같으면 도시 번호로 갈라 순서가 흔들리지 않게 한다.
static int __cdecl SiseCmp(const void* pa, const void* pb)
{
    const SiseRow* a = &g_sise[*(const int*)pa];
    const SiseRow* b = &g_sise[*(const int*)pb];
    int r = 0;
    switch (g_sortCol) {
    case 0:  r = a->id - b->id; break;
    case 1:  r = lstrcmpW(kCities[a->id].name, kCities[b->id].name); break;
    case 2:  r = lstrcmpW(kCities[a->id].sphere, kCities[b->id].sphere); break;
    case 3:  r = a->scale - b->scale; break;
    case 4:  r = a->sise - b->sise; break;
    case 5:  r = a->state - b->state; break;
    case 6:  r = a->trade - b->trade; break;
    case 7:  r = a->market - b->market; break;
    case 8:  r = a->lib - b->lib; break;
    case 9:  r = a->yard - b->yard; break;
    case 10: r = a->guild - b->guild; break;
    case 11: r = lstrcmpW(a->mkt, b->mkt); break;
    default: break;
    }
    if (r == 0) return a->id - b->id;
    return g_sortDir < 0 ? -r : r;
}

// 고른 문화권 이름(0 = 전체)
static const wchar_t* SphereLabel(void)
{
    if (g_sphereSel <= 0 || g_sphereSel > g_sphereN) return L"전체";
    return g_sphere[g_sphereSel - 1];
}

// 닫힌 상자 자리. 펼친 목록은 이 바로 아래에 붙는다.
#define SPH_BOX_W  150
#define SPH_ITEM_H 20
static RECT SphereBoxRect(void)
{
    RECT r;
    r.left = FRAME + 70; r.right = r.left + SPH_BOX_W;
    r.top  = FRAME + TITLE_H + 4; r.bottom = r.top + 22;
    return r;
}

// 문화권으로 거르고, 정렬 기준이 있으면 정렬한다.
static void RebuildView(void)
{
    int i;
    g_viewN = 0;
    for (i = 0; i < CITY_COUNT; i++) {
        if (g_sphereSel > 0 && g_sphereSel <= g_sphereN &&
            lstrcmpW(kCities[i].sphere, g_sphere[g_sphereSel - 1]) != 0) continue;
        g_view[g_viewN++] = i;
    }
    if (g_sortCol >= 0 && g_sortDir != 0)
        qsort(g_view, (size_t)g_viewN, sizeof(int), SiseCmp);
}

// 헤더 오너드로우 서브클래스 — 세피아 그라데이션 + serif 제목
static LRESULT CALLBACK HdrProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        int n = (int)SendMessageW(h, HDM_GETITEMCOUNT, 0, 0), i;
        HFONT of = (HFONT)SelectObject(dc, g_hdrFont);
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < n; i++)
        {
            RECT rc, tr;
            if (!SendMessageW(h, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&rc)) continue;
            VGradient(dc, rc, COL_FACE_TOP, COL_FACE_BOT);
            Bevel(dc, rc, FALSE);
            tr = rc; tr.left += 6;
            SetTextColor(dc, COL_TEXT);
            if (i < COL_COUNT) {
                wchar_t t[32];
                wsprintfW(t, L"%s%s", kCols[i],
                          (i == g_sortCol && g_sortDir) ? (g_sortDir > 0 ? L" ▲" : L" ▼") : L"");
                DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }
    return CallWindowProcW(g_origHdr, h, m, wp, lp);
}

static void AddCol(HWND lv, int i, const wchar_t* t, int w)
{
    LVCOLUMNW c;
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = (LPWSTR)t; c.cx = w; c.iSubItem = i;
    SendMessageW(lv, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
}

static void SetText(HWND lv, int item, int sub, const wchar_t* t)
{
    LVITEMW it; it.iSubItem = sub; it.pszText = (LPWSTR)t;
    SendMessageW(lv, LVM_SETITEMTEXTW, item, (LPARAM)&it);
}

// g_view 에 담긴 순서대로 다시 채운다(필터·정렬이 바뀔 때마다 호출).
static void PopulateList(HWND lv)
{
    int row;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    for (row = 0; row < g_viewN; row++)
    {
        const SiseRow* r = &g_sise[g_view[row]];
        LVITEMW it; wchar_t num[8], sbuf[12];
        wsprintfW(num, L"%d", r->id);
        it.mask = LVIF_TEXT; it.iItem = row; it.iSubItem = 0; it.pszText = num;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        SetText(lv, row, 1, kCities[r->id].name);
        SetText(lv, row, 2, kCities[r->id].sphere);
        if (r->scale < 0) wsprintfW(sbuf, L"-"); else wsprintfW(sbuf, L"%d", r->scale);
        SetText(lv, row, 3, sbuf);
        if (r->sise < 0) wsprintfW(sbuf, L"-"); else wsprintfW(sbuf, L"%d", r->sise);
        SetText(lv, row, 4, sbuf);
        SetText(lv, row, 5, StateName(r->state));                      // 상태(통상 · 전쟁 · 기근 …)
        // 건물: 건물수치(bit) 로 확정. 미로드시 -1 → "-"
        SetText(lv, row, 6, BitMark(r->trade));                        // 교역소(fb21)
        SetText(lv, row, 7, r->market ? L"○" : L"×");                 // 시장 유무
        SetText(lv, row, 8, BitMark(r->lib));                          // 도서관
        SetText(lv, row, 9, BitMark(r->yard));                         // 조선소
        SetText(lv, row, 10, BitMark(r->guild));                       // 조합(fb22로 bit7 확정)
        SetText(lv, row, 11, r->mkt);                                  // 시장아이템 이름 나열
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, NULL, TRUE);
}

static void PaintFrame(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb, cf, tr; HBRUSH br; HFONT of;
    GetClientRect(h, &rc);
    // 바탕 + 갈색 외곽 프레임
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);
    // 타이틀바 (세피아 그라데이션 + 베벨)
    tb.left = FRAME; tb.top = FRAME; tb.right = rc.right - FRAME; tb.bottom = FRAME + TITLE_H;
    VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, tb, FALSE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_titleFont);
    tr = tb; tr.left += 8;
    DrawTextW(dc, L"시세 일람", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    // 닫기 버튼 (액자형 베벨 + ×)
    cb = CloseRect(rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &cb, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &cb, br); DeleteObject(br);
    cf = cb; InflateRect(&cf, -2, -2); VGradient(dc, cf, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, cf, FALSE);
    DrawTextW(dc, L"×", -1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // 필터줄 — 문화권 라벨 + 오른쪽에 보이는 개수/정렬 상태
    SelectObject(dc, g_listFont);
    {
        RECT fr, sb, tr2;
        wchar_t info[96];
        fr.left = FRAME + 8; fr.right = FRAME + 70;
        fr.top = FRAME + TITLE_H; fr.bottom = fr.top + SISE_FILTER_H;
        DrawTextW(dc, L"문화권", -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // 고르는 상자(콤보박스 대신 직접 그린다)
        sb = SphereBoxRect();
        VGradient(dc, sb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, sb, FALSE);
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &sb, br); DeleteObject(br);
        tr2 = sb; tr2.left += 6; tr2.right -= 18;
        DrawTextW(dc, SphereLabel(), -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        tr2 = sb; tr2.left = sb.right - 18;
        DrawTextW(dc, L"▼", -1, &tr2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        fr.left = FRAME + 230; fr.right = rc.right - FRAME - 8;
        if (g_sortCol >= 0 && g_sortDir)
            wsprintfW(info, L"%d개 · %s %s (제목을 누르면 오름차순 → 내림차순 → 안 함)",
                      g_viewN, kCols[g_sortCol], g_sortDir > 0 ? L"오름차순" : L"내림차순");
        else
            wsprintfW(info, L"%d개 · 정렬 안 함(도시 번호순) — 제목을 누르면 정렬합니다", g_viewN);
        DrawTextW(dc, info, -1, &fr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

// ---- 문화권 펼친 목록 (우리 클래스의 자식 창. 콤보박스를 대신한다) ----
#define WC_SPHERE L"TradeUtilKR_Sphere"

static void SphereApply(HWND parent, int sel)
{
    g_sphereSel = sel;
    RebuildView();
    PopulateList(g_list);
    InvalidateRect(parent, NULL, FALSE);
}

static LRESULT CALLBACK SphereProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc, ir; HBRUSH br; HFONT of; int k;
        GetClientRect(h, &rc);
        br = CreateSolidBrush(COL_FACE_TOP); FillRect(dc, &rc, br); DeleteObject(br);
        br = CreateSolidBrush(COL_DARK);     FrameRect(dc, &rc, br); DeleteObject(br);
        SetBkMode(dc, TRANSPARENT);
        of = (HFONT)SelectObject(dc, g_listFont);
        for (k = 0; k <= g_sphereN; k++)
        {
            ir.left = 1; ir.right = rc.right - 1;
            ir.top = 1 + k * SPH_ITEM_H; ir.bottom = ir.top + SPH_ITEM_H;
            if (k == g_sphereSel) {
                br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &ir, br); DeleteObject(br);
                SetTextColor(dc, COL_SEL_TX);
            } else {
                SetTextColor(dc, COL_TEXT);
            }
            ir.left += 6;
            DrawTextW(dc, k == 0 ? L"전체" : g_sphere[k - 1], -1, &ir,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, of);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int k = (GET_Y_LPARAM(lp) - 1) / SPH_ITEM_H;
        HWND parent = GetParent(h);
        if (k >= 0 && k <= g_sphereN) SphereApply(parent, k);
        PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    }

    case WM_KILLFOCUS:            // 다른 곳을 누르면 그냥 접는다
        PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        g_sphereDrop = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// 펼치기/접기. 이미 펼쳐져 있으면 접는다.
static void SphereToggle(HWND parent, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    RECT box = SphereBoxRect();
    int hgt = (g_sphereN + 1) * SPH_ITEM_H + 2;

    if (g_sphereDrop) { DestroyWindow(g_sphereDrop); g_sphereDrop = NULL; return; }
    if (!reg) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = SphereProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = WC_SPHERE;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    g_sphereDrop = CreateWindowExW(0, WC_SPHERE, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                   box.left, box.bottom, SPH_BOX_W, hgt,
                                   parent, (HMENU)3, hinst, NULL);
    if (g_sphereDrop) {
        SetWindowPos(g_sphereDrop, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetFocus(g_sphereDrop);
    }
}

static LRESULT CALLBACK SiseProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
        EnsureFonts();
        // 문화권 고르기 — 목록에 나온 문화권만 담는다(0번은 "전체"). 상자는 직접 그린다.
        BuildSpheres();
        g_sphereSel = 0;

        // 제목 클릭으로 정렬해야 하므로 LVS_NOSORTHEADER 를 빼서 LVN_COLUMNCLICK 을 받는다
        // (헤더 모양은 어차피 HdrProc 에서 직접 그린다).
        // WS_CLIPSIBLINGS 는 펼친 문화권 목록을 리스트뷰가 덮어 그리지 않게 한다.
        g_list = CreateWindowExW(0, L"SysListView32", L"",
                                 WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL,
                                 FRAME, FRAME + TITLE_H + SISE_FILTER_H,
                                 WIN_W - 2 * FRAME, WIN_H - 2 * FRAME - TITLE_H - SISE_FILTER_H,
                                 h, (HMENU)1, g_hinst, NULL);
        SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_list, LVM_SETBKCOLOR, 0, (LPARAM)COL_ROW_A);
        SendMessageW(g_list, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_ROW_A);
        {
            int c;
            for (c = 0; c < COL_COUNT; c++) AddCol(g_list, c, kCols[c], kColW[c]);
        }
        g_sortCol = -1; g_sortDir = 0;
        ReadRows();
        RebuildView();
        PopulateList(g_list);
        // 헤더 오너드로우 서브클래스
        g_hdr = (HWND)SendMessageW(g_list, LVM_GETHEADER, 0, 0);
        if (g_hdr)
        {
            SendMessageW(g_hdr, WM_SETFONT, (WPARAM)g_hdrFont, TRUE);
            g_origHdr = (WNDPROC)SetWindowLongPtrW(g_hdr, GWLP_WNDPROC, (LONG_PTR)HdrProc);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;  // 깜빡임 방지 — WM_PAINT 에서 전부 그림

    case WM_PAINT:
        PaintFrame(h);
        return 0;

    case WM_NOTIFY:
    {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->idFrom == 1 && nh->code == NM_CUSTOMDRAW)
        {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            switch (cd->nmcd.dwDrawStage)
            {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
            {
                int i = (int)cd->nmcd.dwItemSpec;
                BOOL sel = (ListView_GetItemState(g_list, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                if (sel) { cd->clrText = COL_SEL_TX; cd->clrTextBk = COL_SEL_BG; }
                else     { cd->clrText = COL_TEXT;   cd->clrTextBk = (i & 1) ? COL_ROW_B : COL_ROW_A; }
                SelectObject(cd->nmcd.hdc, g_listFont);
                return CDRF_NEWFONT;
            }
            }
            return CDRF_DODEFAULT;
        }
        // 줄을 두 번 누르면 그 도시로 간다(항해 중이면 들어갈지도 묻는다).
        // 창을 먼저 닫고 게임 창에 부친다 — 게임 대화상자가 이 창 프로시저 안쪽에서
        // 열리면 우리 팝업이 그 위에 남는다. 워프 창이 워프한 뒤 닫는 것과 같은 이유다.
        if (nh->idFrom == 1 && nh->code == NM_DBLCLK)
        {
            int row = ((LPNMITEMACTIVATE)lp)->iItem;
            if (row >= 0 && row < g_viewN && g_msgGoCity)
            {
                int city = g_sise[g_view[row]].id;
                DestroyWindow(h);
                // g_subHwnd 로 부친다 — g_hwnd 는 감시 스레드가 1초마다 NULL 로 비웠다
                // 다시 채우므로 하필 그 틈에 누르면 심부름이 허공으로 간다.
                PostMessageW(g_subHwnd, g_msgGoCity, (WPARAM)city, 0);
            }
            return 0;
        }
        // 제목 클릭 — 오름차순 → 내림차순 → 안 함 순으로 돈다.
        if (nh->idFrom == 1 && nh->code == LVN_COLUMNCLICK)
        {
            int col = ((LPNMLISTVIEW)lp)->iSubItem;
            if (col < 0 || col >= COL_COUNT) return 0;
            if (col == g_sortCol) {
                if      (g_sortDir > 0) g_sortDir = -1;
                else if (g_sortDir < 0) { g_sortDir = 0; g_sortCol = -1; }
                else                    g_sortDir = 1;
            } else {
                g_sortCol = col; g_sortDir = 1;
            }
            RebuildView();
            PopulateList(g_list);
            if (g_hdr) InvalidateRect(g_hdr, NULL, TRUE);
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb, sb;
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        GetClientRect(h, &rc); cb = CloseRect(rc);
        if (PtInRect(&cb, pt)) { DestroyWindow(h); return 0; }
        sb = SphereBoxRect();
        if (PtInRect(&sb, pt)) { SphereToggle(h, g_hinst); return 0; }
        if (pt.y < FRAME + TITLE_H)   // 타이틀바 드래그로 창 이동
        {
            ReleaseCapture();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (g_hdr && g_origHdr) { SetWindowLongPtrW(g_hdr, GWLP_WNDPROC, (LONG_PTR)g_origHdr); }
        // 폰트는 여기서 지우지 않는다 — 교역품 창이 같은 것을 쓰고 있을 수 있다(EnsureFonts 참고).
        g_hdr = NULL; g_origHdr = NULL; g_siseWnd = NULL; g_list = NULL;
        g_sphereDrop = NULL; g_sphereSel = 0; g_sortCol = -1; g_sortDir = 0;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowSiseWindow(HWND owner)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT orc;
    if (g_siseWnd) { SetForegroundWindow(g_siseWnd); return; }
    if (!reg)
    {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = SiseProc;
        wc.hInstance = g_hinst;
        wc.lpszClassName = WC_SISE;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = NULL;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    // fb9: 게임 창 중앙에 뜨도록 위치 계산
    if (owner && GetWindowRect(owner, &orc))
    {
        x = orc.left + ((orc.right - orc.left) - WIN_W) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - WIN_H) / 2;
        if (x < 0) x = 0; if (y < 0) y = 0;
    }
    // WS_POPUP: 시스템 프레임 없음(갈색 테두리·타이틀바는 직접 그림)
    g_siseWnd = CreateWindowExW(0, WC_SISE, L"시세 일람",
                                WS_POPUP, x, y, WIN_W, WIN_H,
                                owner, NULL, g_hinst, NULL);
    if (g_siseWnd) { ShowWindow(g_siseWnd, SW_SHOW); UpdateWindow(g_siseWnd); }
}

// ---------------- 교역품 관리 창 (fb21/fb27, approach B) ----------------
// 현재 정박한 도시의 실시간 판매목록을 메모리에서 스캔해 교역품명/공급량/원산지 로 표시.
//   판매목록 엔트리 = 16바이트 { 품목id(u32), 공급량(u32), 원산지도시id(u32), x(u32; 도시내 동일) }
//   (fb27 CE 트레이스로 확정: 빌더 0x480E80 이 이 목록을 만든다)
#define WC_GOODS  L"TradeUtilKR_Goods"
#define GWIN_W    400      // fb31: 컬럼 4개(120+110+90+56=376)+스크롤바에 맞춰 폭 축소(구 492)
#define GWIN_H    540
#define FILTER_H  26       // 상단 검색창 높이
#define NGOODS    (int)(sizeof(kTradeGoods)/sizeof(kTradeGoods[0]))

// 문화권 교역품 테이블 (EXE .rdata 정적, VA 0x004DF0E0). 문화권당 i32×5, -1 종료.
// (fb28: 이 표 + 도시명 = 각 교역소가 파는 공통 교역품. + 도시 특산품)
#define CULT_TABLE  0x004DF0E0u
#define CULT_OFF    0x4C           // 도시 구조체의 문화권 인덱스 = SISE_BASE+0x4C (struct+0x58)
// idx = 도시 구조체 +0x58 (런타임). 226도시 전수 실측 확정(2026-07-04, → obsidian 문화권 인덱스 support).
// 주의: idx3=아프리카, idx10=아메리카. (이전 버전 아메리카↔아프리카 뒤바뀌어 있었음)
static const wchar_t* kSpheres[11] = {
    L"이베리아", L"북유럽", L"지중해", L"아프리카", L"중근동", L"인도",
    L"중국", L"중앙아시아", L"동남아시아", L"일본", L"아메리카"
};

#define GCOL_COUNT 4
static const wchar_t* kGCols[GCOL_COUNT] = { L"교역품", L"도시", L"문화권", L"구분" };
static const int      kGColW[GCOL_COUNT] = { 120, 110, 90, 56 };

typedef struct { int city, kind, isSpec; } GoodsRow;
static GoodsRow g_goods[1200];
static int      g_goodsCount = 0;
static int      g_gSortCol = 0, g_gSortAsc = 1;   // 기본 교역품순
static HWND     g_goodsWnd = NULL, g_goodsList = NULL, g_goodsHdr = NULL;
static WNDPROC  g_goodsOrigHdr = NULL;
static HWND     g_goodsFilter = NULL;             // 상단 검색 입력창
static HBRUSH   g_goodsFilterBr = NULL;           // 검색창 배경 브러시
static wchar_t  g_goodsFilterText[64] = L"";      // 현재 필터 문자열
// fb32: 세피아 오버레이 스크롤바 (네이티브 회색 스크롤바를 덮어 게임 분위기에 맞춤)
#define WC_GOODSSB  L"TradeUtilKR_GoodsSB"
static HWND     g_goodsSB = NULL;                 // 커스텀 스크롤바 오버레이 창
static WNDPROC  g_goodsListOrig = NULL;           // 리스트뷰 서브클래스 원본 프로시저
static int      g_sbDrag = 0, g_sbDragY = 0;      // 썸 드래그 상태

// 대소문자 무시 부분일치(한글은 그대로 매칭). needle 빈 문자열이면 항상 TRUE.
static BOOL WStrContainsCI(const wchar_t* hay, const wchar_t* needle)
{
    int hlen, nlen, i, j;
    if (!needle || !needle[0]) return TRUE;
    if (!hay) return FALSE;
    hlen = lstrlenW(hay); nlen = lstrlenW(needle);
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen; j++) {
            wchar_t a = hay[i + j], b = needle[j];
            if (a >= L'a' && a <= L'z') a -= 32;
            if (b >= L'a' && b <= L'z') b -= 32;
            if (a != b) break;
        }
        if (j == nlen) return TRUE;
    }
    return FALSE;
}

static int CityCulture(int i)      // 도시 문화권 인덱스(0~10), 실패 -1
{
    int v; return ReadI32(CityField(i, CULT_OFF), &v) && v >= 0 && v < 11 ? v : -1;
}
// 교역품 지역(goods region) 인덱스 — 0x4DF0E0 공통품 테이블의 행 번호. 0~26 (27종).
// ★ 주의: 이것은 문화권(struct+0x58, 0~10)과 다른 별개 인덱스다. (207/226 도시가 서로 다름)
//   공통 교역품은 반드시 이 값으로 인덱싱. fb28의 "struct+0x58"은 이베리아(지역0)에서만 우연히 일치.
//   실측: 이스탄불 지역=13(=밀/총), 리스본 지역=0(=돌소금/올리브유/총). (2026-07-04)
#define REGION_TABLE  0x004D14B0u   // 도시별 136바이트 레코드 배열
#define REGION_STRIDE 136
#define REGION_OFF    0x1C          // 레코드 내 교역품 지역 인덱스
#define NREGION       27
static int GoodsRegion(int i)   // 도시 i 의 교역품 지역(0~26), 실패 -1
{
    int v; return ReadI32(REGION_TABLE + (unsigned)i * REGION_STRIDE + REGION_OFF, &v)
        && v >= 0 && v < NREGION ? v : -1;
}
static int CultGood(int region, int n)   // 교역품 지역 공통 교역품 n번째(0~4), 없으면 -1
{
    const int* p = (const int*)(CULT_TABLE + (unsigned)region * 20 + (unsigned)n * 4);
    int v;
    if (region < 0 || region >= NREGION || n < 0 || n >= 5) return -1;
    if (IsBadReadPtr(p, sizeof(*p))) return -1;
    v = *p;
    return (v >= 0 && v < NGOODS) ? v : -1;
}

// 교역품 판매허용 게이트 (EXE .data, VA 0x0058BAB0). 품목종류별 플래그(1/0).
// 게임 판매목록 빌더가 모든 품목을 이 값으로 필터. 0=미판매(미발견/미언락 지역 품목).
// 동적: 지역발견/시대에 따라 값이 바뀌므로 라이브로 읽는다. (fb29 실측)
#define GATE_TABLE  0x0058BAB0u
static int GoodSellable(int kind)   // 0x58BAB0[kind] != 0 이면 판매 가능
{
    int v;
    if (kind < 0 || kind >= NGOODS) return 0;
    return ReadI32(GATE_TABLE + (unsigned)kind * 4, &v) && v != 0;
}

static int __cdecl GoodsCmp(const void* a, const void* b)
{
    const GoodsRow* x = (const GoodsRow*)a; const GoodsRow* y = (const GoodsRow*)b;
    int r = 0;
    switch (g_gSortCol) {
    case 0: r = lstrcmpW(kTradeGoods[x->kind], kTradeGoods[y->kind]); break;
    case 1: r = lstrcmpW(kCities[x->city].name, kCities[y->city].name); break;
    case 2: r = CityCulture(x->city) - CityCulture(y->city); break;
    case 3: r = x->isSpec - y->isSpec; break;
    }
    if (r == 0) r = lstrcmpW(kCities[x->city].name, kCities[y->city].name);
    return g_gSortAsc ? r : -r;
}

// 전 교역소 도시가 파는 교역품 — 게임 판매목록 빌더(0x480CC0) 3-phase 모델 재현.
//   A. 지역 공통품: 0x4DF0E0[교역품지역(record+0x1C)] (자기 특산품과 같은 종류는 제외 — 게임 dedup)
//   B. 자기 특산품: 도시struct+0x10 (종류)
//   * A·B 모두 게이트 GoodSellable(=0x58BAB0[종류]!=0) 통과분만. (fb29: 이스탄불 골동품 제외)
//   C. 연결 내륙도시 특산품(카디스←코르도바 등)은 Ctx+0xB0 동적 리스트라 별도 과제(리서치1) — 미반영.
static void BuildGoods(void)
{
    int i, n;
    g_goodsCount = 0;
    for (i = 0; i < CITY_COUNT; i++) {
        int region, spec, hasSpec;
        if (ReadBuildingBit(i, BIT_TRADE) != 1) continue;   // 교역소 있는 도시만
        region = GoodsRegion(i);   // ★ 공통품은 교역품 지역(record+0x1C), 문화권(struct+0x58) 아님
        hasSpec = ReadI32(CityField(i, 4), &spec) && spec >= 0 && spec < NGOODS;  // 특산품 종류

        // A. 지역 공통품 (자기 특산품 종류는 B에서 다루므로 제외, 게이트 통과분만)
        for (n = 0; n < 5; n++) {
            int g = CultGood(region, n);
            if (g < 0) break;
            if (hasSpec && g == spec) continue;   // 게임 dedup: 자기 특산품과 겹치면 A 제외
            if (!GoodSellable(g)) continue;       // 게이트: 미판매 품목 제외
            if (g_goodsCount >= 1200) break;
            g_goods[g_goodsCount].city = i; g_goods[g_goodsCount].kind = g; g_goods[g_goodsCount].isSpec = 0;
            g_goodsCount++;
        }
        // B. 자기 특산품 (게이트 통과 시만 — 미판매면 목록에서 빠짐)
        if (hasSpec && GoodSellable(spec) && g_goodsCount < 1200) {
            g_goods[g_goodsCount].city = i; g_goods[g_goodsCount].kind = spec; g_goods[g_goodsCount].isSpec = 1;
            g_goodsCount++;
        }
    }
    qsort(g_goods, g_goodsCount, sizeof(GoodsRow), GoodsCmp);
}

static void PopulateGoods(HWND lv)
{
    int i, row = 0;
    const wchar_t* f = g_goodsFilterText;
    SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_goodsCount; i++) {
        GoodsRow* g = &g_goods[i];
        int cult = CityCulture(g->city);
        const wchar_t* gname = kTradeGoods[g->kind];
        const wchar_t* cname = kCities[g->city].name;
        const wchar_t* culn  = (cult >= 0 && cult < 11) ? kSpheres[cult] : L"?";
        const wchar_t* kindn = g->isSpec ? L"특산" : L"공통";
        LVITEMW it;
        // 필터: 교역품/도시/문화권/구분 중 하나라도 부분일치하면 표시
        if (f[0] && !WStrContainsCI(gname, f) && !WStrContainsCI(cname, f)
                 && !WStrContainsCI(culn, f) && !WStrContainsCI(kindn, f))
            continue;
        // lParam 에 g_goods 색인을 달아 둔다 — 필터가 걸리면 화면 행 번호와 어긋나므로,
        // 더블클릭(그림 보기)에서 눌린 줄이 어느 교역품인지 이걸로 되찾는다.
        it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = row; it.iSubItem = 0;
        it.pszText = (LPWSTR)gname; it.lParam = (LPARAM)i;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        SetText(lv, row, 1, cname);
        SetText(lv, row, 2, culn);
        SetText(lv, row, 3, kindn);
        row++;
    }
}

static LRESULT CALLBACK GoodsHdrProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        int n = (int)SendMessageW(h, HDM_GETITEMCOUNT, 0, 0), i;
        HFONT of = (HFONT)SelectObject(dc, g_hdrFont);
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < n; i++) {
            RECT rc, tr; if (!SendMessageW(h, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&rc)) continue;
            VGradient(dc, rc, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, rc, FALSE);
            tr = rc; tr.left += 6; SetTextColor(dc, COL_TEXT);
            if (i < GCOL_COUNT) {
                wchar_t t[24];
                wsprintfW(t, L"%s%s", kGCols[i], i==g_gSortCol ? (g_gSortAsc?L" ▲":L" ▼") : L"");
                DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        SelectObject(dc, of); EndPaint(h, &ps); return 0;
    }
    return CallWindowProcW(g_goodsOrigHdr, h, m, wp, lp);
}

static void GoodsPaintFrame(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb, cf, tr; HBRUSH br; HFONT of;
    GetClientRect(h, &rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);
    tb.left = FRAME; tb.top = FRAME; tb.right = rc.right - FRAME; tb.bottom = FRAME + TITLE_H;
    VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, tb, FALSE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_titleFont); tr = tb; tr.left += 8;
    DrawTextW(dc, L"교역품 관리", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    cb = CloseRect(rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &cb, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &cb, br); DeleteObject(br);
    cf = cb; InflateRect(&cf, -2, -2); VGradient(dc, cf, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, cf, FALSE);
    DrawTextW(dc, L"×", -1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

// ---- fb32: 세피아 커스텀 스크롤바 (리스트뷰 네이티브 스크롤바 위에 오버레이) ----
typedef struct { BOOL show; int H, sbw, thumbTop, thumbBot; } SBGeom;

static SBGeom GoodsSBCalc(void)
{
    SBGeom g; RECT rc; SCROLLINFO si;
    int trackH, thumbH, span, page, denom, pos, off;
    ZeroMemory(&g, sizeof(g));
    if (!g_goodsSB || !g_goodsList) return g;
    GetClientRect(g_goodsSB, &rc);
    g.H = rc.bottom; g.sbw = rc.right;
    if (g.H <= 2 * g.sbw) return g;
    si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    if (!GetScrollInfo(g_goodsList, SB_VERT, &si)) return g;
    span = si.nMax - si.nMin + 1; page = (int)si.nPage; if (page < 1) page = 1;
    if (span <= page) return g;                       // 스크롤 불필요
    g.show = TRUE;
    trackH = (g.H - g.sbw) - g.sbw;                   // 위/아래 화살표 사이 트랙
    thumbH = trackH * page / span; if (thumbH < 18) thumbH = 18; if (thumbH > trackH) thumbH = trackH;
    denom = span - page; pos = si.nPos - si.nMin;
    off = denom > 0 ? (trackH - thumbH) * pos / denom : 0;
    g.thumbTop = g.sbw + off; g.thumbBot = g.thumbTop + thumbH;
    return g;
}

static int GoodsLineStep(void)
{
    RECT ir;
    if (SendMessageW(g_goodsList, LVM_GETITEMCOUNT, 0, 0) > 0 &&
        ListView_GetItemRect(g_goodsList, 0, &ir, LVIR_BOUNDS) && ir.bottom > ir.top)
        return ir.bottom - ir.top;
    return 17;
}
static int GoodsPageStep(void)
{
    SCROLLINFO si; si.cbSize = sizeof(si); si.fMask = SIF_PAGE;
    return GetScrollInfo(g_goodsList, SB_VERT, &si) ? (int)si.nPage : 40;
}
static void GoodsScrollBy(int dpx)
{
    ListView_Scroll(g_goodsList, 0, dpx);
    if (g_goodsSB) InvalidateRect(g_goodsSB, NULL, FALSE);
}
static void GoodsScrollTo(int newPos)
{
    SCROLLINFO si; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    if (!GetScrollInfo(g_goodsList, SB_VERT, &si)) return;
    GoodsScrollBy(newPos - si.nPos);
}

static void DrawSBArrow(HDC dc, RECT r, BOOL up)
{
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, s = 3;
    POINT p[3];
    HBRUSH br = CreateSolidBrush(COL_TEXT); HPEN pn = CreatePen(PS_SOLID, 1, COL_TEXT);
    HBRUSH ob; HPEN op;
    if (up) { p[0].x = cx; p[0].y = cy - s; p[1].x = cx - s; p[1].y = cy + s; p[2].x = cx + s; p[2].y = cy + s; }
    else    { p[0].x = cx; p[0].y = cy + s; p[1].x = cx - s; p[1].y = cy - s; p[2].x = cx + s; p[2].y = cy - s; }
    ob = (HBRUSH)SelectObject(dc, br); op = (HPEN)SelectObject(dc, pn);
    Polygon(dc, p, 3);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pn);
}

static LRESULT CALLBACK GoodsSBProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        SBGeom g = GoodsSBCalc();
        RECT rc, up, dn, th; HBRUSH bg;
        GetClientRect(h, &rc);
        bg = CreateSolidBrush(COL_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
        if (g.show) {
            up.left = 0; up.top = 0; up.right = g.sbw; up.bottom = g.sbw;
            dn.left = 0; dn.top = g.H - g.sbw; dn.right = g.sbw; dn.bottom = g.H;
            th.left = 1; th.top = g.thumbTop; th.right = g.sbw - 1; th.bottom = g.thumbBot;
            VGradient(dc, up, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, up, FALSE); DrawSBArrow(dc, up, TRUE);
            VGradient(dc, dn, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, dn, FALSE); DrawSBArrow(dc, dn, FALSE);
            VGradient(dc, th, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, th, FALSE);
        }
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN:
    {
        SBGeom g = GoodsSBCalc(); int y = GET_Y_LPARAM(lp);
        if (!g.show) return 0;
        SetCapture(h);
        if      (y < g.sbw)          GoodsScrollBy(-GoodsLineStep());
        else if (y >= g.H - g.sbw)   GoodsScrollBy( GoodsLineStep());
        else if (y < g.thumbTop)     GoodsScrollBy(-GoodsPageStep());
        else if (y >= g.thumbBot)    GoodsScrollBy( GoodsPageStep());
        else { g_sbDrag = 1; g_sbDragY = y - g.thumbTop; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_sbDrag) {
            SBGeom g = GoodsSBCalc();
            int y = GET_Y_LPARAM(lp), trackH, thumbH, off, denom, span, page, newPos;
            SCROLLINFO si;
            if (!g.show) return 0;
            trackH = (g.H - g.sbw) - g.sbw;
            thumbH = g.thumbBot - g.thumbTop;
            off = (y - g_sbDragY) - g.sbw;
            denom = trackH - thumbH; if (denom < 1) denom = 1;
            if (off < 0) off = 0; if (off > denom) off = denom;
            si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            if (!GetScrollInfo(g_goodsList, SB_VERT, &si)) return 0;
            span = si.nMax - si.nMin + 1; page = (int)si.nPage;
            newPos = si.nMin + (span - page) * off / denom;
            GoodsScrollTo(newPos);
        }
        return 0;
    case WM_LBUTTONUP:
        g_sbDrag = 0;
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL:
        SendMessageW(g_goodsList, WM_MOUSEWHEEL, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// 리스트뷰 서브클래스 — 휠/키보드/선택 등으로 스크롤되면 오버레이 썸 갱신
static LRESULT CALLBACK GoodsListSubProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    LRESULT r = CallWindowProcW(g_goodsListOrig, h, m, wp, lp);
    if (g_goodsSB && (m == WM_MOUSEWHEEL || m == WM_KEYDOWN || m == WM_VSCROLL ||
                      m == WM_LBUTTONDOWN || m == WM_LBUTTONUP))
        InvalidateRect(g_goodsSB, NULL, FALSE);
    return r;
}

// 오버레이를 리스트뷰 네이티브 스크롤바 위에 배치하고, 필요 없으면 숨김
static void UpdateGoodsSB(void)
{
    RECT lr; int sbw; SBGeom g; POINT tl, br; HWND parent;
    if (!g_goodsSB || !g_goodsList) return;
    parent = GetParent(g_goodsList);   // WM_CREATE 시점엔 g_goodsWnd 미대입이라 부모를 직접 조회
    if (!parent) return;
    GetWindowRect(g_goodsList, &lr);
    tl.x = lr.left; tl.y = lr.top; br.x = lr.right; br.y = lr.bottom;
    ScreenToClient(parent, &tl); ScreenToClient(parent, &br);
    sbw = GetSystemMetrics(SM_CXVSCROLL);
    MoveWindow(g_goodsSB, br.x - sbw, tl.y, sbw, br.y - tl.y, FALSE);
    g = GoodsSBCalc();
    ShowWindow(g_goodsSB, g.show ? SW_SHOW : SW_HIDE);
    if (g.show) InvalidateRect(g_goodsSB, NULL, TRUE);
}

// ---------------- 교역품 그림 창 (목록에서 줄을 더블클릭) ----------------
//
// 그림은 게임이 도시정보 · 특산품 창에 띄우는 것과 같은 것이다 — 게임 폴더의 ITEM.CDS.
// 교역품 종류 -> 그림 번호는 GOOD_PIC_BASE + 종류. 70종이 그림 134~203 에 이름 순서
// 그대로 놓여 있다(206장을 전부 펼쳐 하나씩 맞춰 봤다: 0 밀=134, 11 올리브유=145,
// 16 금=150, 33 양모=167, 69 노예=203). 아이템 표(kItemNames)와는 딴 표다 —
// 교역품 이름은 그 표에 아예 없으므로 itemdb 의 그림번호로는 못 찾는다.
#define GOOD_PIC_BASE 134
#define WC_GPIC   L"TradeUtilKR_GoodsPic"
#define GPIC_SZ   200                                  // 화면에 그리는 크기(원본 120x120 을 늘린다)
#define GPIC_PAD  16
#define GPIC_W    (GPIC_SZ + 2*(FRAME + GPIC_PAD))
#define GPIC_H    (FRAME + TITLE_H + 10 + GPIC_SZ + 8 + 44 + FRAME)

static HWND     g_gpicWnd = NULL;
static GoodsRow g_gpic;                                // 지금 보여 주는 줄의 사본
static int      g_gpicOk = 0;                          // g_gpic 에 값이 들어 있나
// 색인이 아니라 사본을 들고 있는 것은, 창을 띄워 둔 채로 목록을 다시 정렬해도
// 그림과 설명이 딴 교역품으로 바뀌지 않게 하려는 것이다.

static void GoodsPicPaint(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb, cf, tr, in, box; HBRUSH br; HFONT of;
    int px = FRAME + GPIC_PAD, py = FRAME + TITLE_H + 10;
    int cult = g_gpicOk ? CityCulture(g_gpic.city) : -1;
    wchar_t buf[96];

    GetClientRect(h, &rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);

    tb.left = FRAME; tb.top = FRAME; tb.right = rc.right - FRAME; tb.bottom = FRAME + TITLE_H;
    VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, tb, FALSE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_titleFont); tr = tb; tr.left += 8;
    DrawTextW(dc, g_gpicOk ? kTradeGoods[g_gpic.kind] : L"교역품", -1, &tr,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    in.left = px; in.top = py; in.right = px + GPIC_SZ; in.bottom = py + GPIC_SZ;
    if (!g_gpicOk || !ItemPic_Draw(dc, px, py, GPIC_SZ, GPIC_SZ, GOOD_PIC_BASE + g_gpic.kind)) {
        of = (HFONT)SelectObject(dc, g_listFont);
        br = CreateSolidBrush(COL_LIGHT); FillRect(dc, &in, br); DeleteObject(br);
        Bevel(dc, in, TRUE);
        DrawTextW(dc, ItemPic_Count() > 0 ? L"그림이 없습니다" : L"ITEM.CDS 를 열지 못했습니다",
                  -1, &in, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }
    box = in; InflateRect(&box, 1, 1);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br);

    if (g_gpicOk) {
        of = (HFONT)SelectObject(dc, g_listFont);
        tr.left = px; tr.right = rc.right - FRAME - GPIC_PAD;
        tr.top = in.bottom + 8; tr.bottom = tr.top + 20;
        wsprintfW(buf, L"%s · %s", kCities[g_gpic.city].name,
                  (cult >= 0 && cult < 11) ? kSpheres[cult] : L"?");
        DrawTextW(dc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        tr.top = tr.bottom; tr.bottom = tr.top + 20;
        DrawTextW(dc, g_gpic.isSpec ? L"특산품" : L"문화권 공통 교역품", -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }

    cb = CloseRect(rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &cb, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &cb, br); DeleteObject(br);
    cf = cb; InflateRect(&cf, -2, -2); VGradient(dc, cf, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, cf, FALSE);
    of = (HFONT)SelectObject(dc, g_titleFont);
    DrawTextW(dc, L"×", -1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    EndPaint(h, &ps);
}

static LRESULT CALLBACK GoodsPicProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: GoodsPicPaint(h); return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb; pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        GetClientRect(h, &rc); cb = CloseRect(rc);
        if (PtInRect(&cb, pt)) { DestroyWindow(h); return 0; }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_gpicWnd = NULL; g_gpicOk = 0; return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// 목록 창(owner) 옆에 그림 창을 띄운다. 이미 떠 있으면 내용만 갈아 끼운다.
// 게임이 전체화면 DirectDraw 라 그냥 띄우면(SW_SHOWNOACTIVATE) 게임 화면 뒤로 숨는다.
// 앞으로 낸 다음 초점은 목록에 돌려준다 — 딸린 창은 주인 위에 붙어 다니므로 그대로 보인다.
static void BringToFront(HWND w, HWND owner)
{
    ShowWindow(w, SW_SHOW);
    UpdateWindow(w);
    SetForegroundWindow(w);
    if (g_goodsList) SetFocus(g_goodsList);
    else if (owner)  SetForegroundWindow(owner);
}

static void ShowGoodsPic(HWND owner, int gi)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT; RECT orc;

    if (gi < 0 || gi >= g_goodsCount) return;
    EnsureFonts();
    ItemPic_Load();
    g_gpic = g_goods[gi]; g_gpicOk = 1;
    if (g_gpicWnd) {
        InvalidateRect(g_gpicWnd, NULL, TRUE);
        BringToFront(g_gpicWnd, owner);
        return;
    }

    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = GoodsPicProc; wc.hInstance = g_hinst; wc.lpszClassName = WC_GPIC;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {          // 목록을 가리지 않게 오른쪽에 붙인다
        x = orc.right + 6; y = orc.top;
        if (x + GPIC_W > GetSystemMetrics(SM_CXSCREEN)) x = orc.left - GPIC_W - 6;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    g_gpicWnd = CreateWindowExW(0, WC_GPIC, L"교역품 그림", WS_POPUP, x, y, GPIC_W, GPIC_H,
                                owner, NULL, g_hinst, NULL);
    if (g_gpicWnd) BringToFront(g_gpicWnd, owner);
}

static LRESULT CALLBACK GoodsProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
    {
        int c;
        EnsureFonts();          // 교역품 창을 시세 창보다 먼저 열어도 글꼴이 제대로 나오도록
        g_goodsFilterText[0] = 0;
        // 상단 검색창 (도시명/교역품명/문화권/구분 실시간 필터)
        g_goodsFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            FRAME + 2, FRAME + TITLE_H + 2, GWIN_W - 2*FRAME - 4, FILTER_H - 4,
            h, (HMENU)2, g_hinst, NULL);
        SendMessageW(g_goodsFilter, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_goodsFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"검색: 도시 · 교역품 · 문화권 · 구분");
        g_goodsList = CreateWindowExW(0, L"SysListView32", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL,
            FRAME, FRAME + TITLE_H + FILTER_H, GWIN_W - 2*FRAME, GWIN_H - 2*FRAME - TITLE_H - FILTER_H,
            h, (HMENU)1, g_hinst, NULL);
        SendMessageW(g_goodsList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
        SendMessageW(g_goodsList, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_goodsList, LVM_SETBKCOLOR, 0, (LPARAM)COL_ROW_A);
        SendMessageW(g_goodsList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_ROW_A);
        for (c = 0; c < GCOL_COUNT; c++) AddCol(g_goodsList, c, kGCols[c], kGColW[c]);
        BuildGoods();
        PopulateGoods(g_goodsList);
        g_goodsHdr = (HWND)SendMessageW(g_goodsList, LVM_GETHEADER, 0, 0);
        if (g_goodsHdr) {
            SendMessageW(g_goodsHdr, WM_SETFONT, (WPARAM)g_hdrFont, TRUE);
            g_goodsOrigHdr = (WNDPROC)SetWindowLongPtrW(g_goodsHdr, GWLP_WNDPROC, (LONG_PTR)GoodsHdrProc);
        }
        // fb32: 세피아 오버레이 스크롤바 생성 + 리스트뷰 서브클래스 (리스트뷰보다 나중에 = z-order 위)
        g_goodsSB = CreateWindowExW(0, WC_GOODSSB, L"", WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 10, 10, h, (HMENU)3, g_hinst, NULL);
        g_goodsListOrig = (WNDPROC)SetWindowLongPtrW(g_goodsList, GWLP_WNDPROC, (LONG_PTR)GoodsListSubProc);
        UpdateGoodsSB();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: GoodsPaintFrame(h); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 2 && HIWORD(wp) == EN_CHANGE) {   // 검색창 내용 변경
            GetWindowTextW(g_goodsFilter, g_goodsFilterText, 64);
            PopulateGoods(g_goodsList);
            UpdateGoodsSB();
            return 0;
        }
        return 0;
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_LIGHT);
        if (!g_goodsFilterBr) g_goodsFilterBr = CreateSolidBrush(COL_LIGHT);
        return (LRESULT)g_goodsFilterBr;
    }
    case WM_NOTIFY:
    {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->idFrom == 1 && nh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                int i = (int)cd->nmcd.dwItemSpec;
                BOOL sel = (ListView_GetItemState(g_goodsList, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                if (sel) { cd->clrText = COL_SEL_TX; cd->clrTextBk = COL_SEL_BG; }
                else     { cd->clrText = COL_TEXT;   cd->clrTextBk = (i & 1) ? COL_ROW_B : COL_ROW_A; }
                SelectObject(cd->nmcd.hdc, g_listFont);
                return CDRF_NEWFONT;
            }}
            return CDRF_DODEFAULT;
        }
        if (nh->idFrom == 1 && nh->code == NM_DBLCLK) {          // 줄을 두 번 누르면 그림
            LPNMITEMACTIVATE ia = (LPNMITEMACTIVATE)lp;
            LVITEMW it; ZeroMemory(&it, sizeof(it));
            if (ia->iItem >= 0) {
                it.mask = LVIF_PARAM; it.iItem = ia->iItem;
                if (SendMessageW(g_goodsList, LVM_GETITEMW, 0, (LPARAM)&it))
                    ShowGoodsPic(h, (int)it.lParam);
            }
            return 0;
        }
        if (nh->idFrom == 1 && nh->code == LVN_COLUMNCLICK) {
            LPNMLISTVIEW nlv = (LPNMLISTVIEW)lp;
            int col = nlv->iSubItem;
            if (col == g_gSortCol) g_gSortAsc ^= 1; else { g_gSortCol = col; g_gSortAsc = 1; }
            qsort(g_goods, g_goodsCount, sizeof(GoodsRow), GoodsCmp);
            PopulateGoods(g_goodsList);
            UpdateGoodsSB();
            if (g_goodsHdr) InvalidateRect(g_goodsHdr, NULL, TRUE);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb; pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        GetClientRect(h, &rc); cb = CloseRect(rc);
        if (PtInRect(&cb, pt)) { DestroyWindow(h); return 0; }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_gpicWnd) DestroyWindow(g_gpicWnd);       // 목록을 닫으면 그림도 같이 닫는다
        if (g_goodsHdr && g_goodsOrigHdr) SetWindowLongPtrW(g_goodsHdr, GWLP_WNDPROC, (LONG_PTR)g_goodsOrigHdr);
        if (g_goodsList && g_goodsListOrig) SetWindowLongPtrW(g_goodsList, GWLP_WNDPROC, (LONG_PTR)g_goodsListOrig);
        if (g_goodsFilterBr) { DeleteObject(g_goodsFilterBr); g_goodsFilterBr = NULL; }
        g_goodsFilterText[0] = 0; g_sbDrag = 0;
        g_goodsHdr = NULL; g_goodsOrigHdr = NULL; g_goodsWnd = NULL; g_goodsList = NULL;
        g_goodsFilter = NULL; g_goodsSB = NULL; g_goodsListOrig = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowTradeGoodsWindow(HWND owner)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT; RECT orc;
    if (g_goodsWnd) { SetForegroundWindow(g_goodsWnd); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = GoodsProc; wc.hInstance = g_hinst; wc.lpszClassName = WC_GOODS;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc);
        wc.lpfnWndProc = GoodsSBProc; wc.lpszClassName = WC_GOODSSB;   // fb32 커스텀 스크롤바
        RegisterClassW(&wc);
        reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - GWIN_W) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - GWIN_H) / 2;
        if (x < 0) x = 0; if (y < 0) y = 0;
    }
    g_goodsWnd = CreateWindowExW(0, WC_GOODS, L"교역품 관리", WS_POPUP, x, y, GWIN_W, GWIN_H, owner, NULL, g_hinst, NULL);
    if (g_goodsWnd) { ShowWindow(g_goodsWnd, SW_SHOW); UpdateWindow(g_goodsWnd); }
}

// ---------------- 워프 창 (단축키 W / 워프 메뉴 맨 위) ----------------
//
// 워프 자체는 메뉴에도 그대로 있다. 다만 226개를 지역 서브메뉴로 뒤지는 게 번거로워
// 도시 이름 몇 글자로 찾아 바로 가는 창을 따로 둔다. 가는 길은 같다(DoWarp).
// 문화권 탭 한 줄(전체 + 11) 아래에 검색칸, 그 아래가 목록이다. 도시 이름 옆에는
// 시세 일람과 같은 라이브 값(규모 · 시세 · 교역소 · 조선소 · 도서관 · 조합)을 붙인다 —
// 워프 index 가 곧 도시 번호라(kWarps 와 kCities 가 같은 차례다) 그대로 읽어 쓴다.
#define WC_WARP    L"TradeUtilKR_Warp"
#define WTAB_H     26
#define WTAB_COLS  6
#define WTAB_W     92
#define WWIN_W     (WTAB_COLS * (WTAB_W + 2) + 2*FRAME + 10)      // 574
#define WWIN_H     520
#define WTAB_Y     (FRAME + TITLE_H + 4)
#define WFILTER_Y  (WTAB_Y + 2*WTAB_H + 4)
#define WLIST_Y    (WFILTER_Y + FILTER_H)

#define WCOL_N 7
static const wchar_t* kWCols[WCOL_N] = { L"도시", L"규모", L"시세", L"교역소", L"조선소", L"도서관", L"조합" };
static const int      kWColW[WCOL_N] = { 150,     46,      50,      56,        56,        56,        50 };

static HWND    g_warpWnd = NULL, g_warpList = NULL, g_warpFilter = NULL;
static HBRUSH  g_warpFilterBr = NULL;
static WNDPROC g_warpEditOrig = NULL;
static wchar_t g_warpText[64] = L"";
static int     g_warpRegion = 0;         // 0 = 전체, 그 밖은 (지역 목록 색인 + 1)

// 워프 목록의 지역 이름 — kWarps 에 나온 차례대로 모은다(메뉴의 서브메뉴 순서와 같다).
#define WREGION_MAX 16
static const wchar_t* g_wregion[WREGION_MAX];
static int g_wregionN = 0;

static void BuildWarpRegions(void)
{
    int i, k;
    if (g_wregionN) return;
    for (i = 0; i < WARP_COUNT && g_wregionN < WREGION_MAX; i++) {
        const wchar_t* s = kWarps[i].region;
        if (!s || !s[0]) continue;
        for (k = 0; k < g_wregionN; k++) if (!lstrcmpW(g_wregion[k], s)) break;
        if (k == g_wregionN) g_wregion[g_wregionN++] = s;
    }
}

static RECT WTabRect(int i)
{
    RECT r;
    r.left = FRAME + 5 + (i % WTAB_COLS) * (WTAB_W + 2);
    r.right = r.left + WTAB_W;
    r.top = WTAB_Y + (i / WTAB_COLS) * WTAB_H;
    r.bottom = r.top + WTAB_H - 3;
    return r;
}

static void PopulateWarp(void)
{
    int i, row = 0;
    const wchar_t* f = g_warpText;
    if (!g_warpList) return;
    SendMessageW(g_warpList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_warpList, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < WARP_COUNT; i++) {
        LVITEMW it;
        wchar_t buf[16];
        if (g_warpRegion > 0 && lstrcmpW(kWarps[i].region, g_wregion[g_warpRegion - 1])) continue;
        if (f[0] && !WStrContainsCI(kWarps[i].city, f) && !WStrContainsCI(kWarps[i].region, f)) continue;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = row; it.iSubItem = 0;
        it.pszText = (LPWSTR)kWarps[i].city; it.lParam = (LPARAM)i;
        SendMessageW(g_warpList, LVM_INSERTITEMW, 0, (LPARAM)&it);
        // 도시 번호 = 워프 index. 라이브 값이라 세이브 전에는 "-" 로 나온다.
        { int v = ReadScale(i); if (v < 0) lstrcpyW(buf, L"-"); else wsprintfW(buf, L"%d", v); }
        SetText(g_warpList, row, 1, buf);
        { int v = ReadSise(i);  if (v < 0) lstrcpyW(buf, L"-"); else wsprintfW(buf, L"%d", v); }
        SetText(g_warpList, row, 2, buf);
        SetText(g_warpList, row, 3, BitMark(ReadBuildingBit(i, BIT_TRADE)));
        SetText(g_warpList, row, 4, BitMark(ReadBuildingBit(i, BIT_SHIPYARD)));
        SetText(g_warpList, row, 5, BitMark(ReadBuildingBit(i, BIT_LIBRARY)));
        SetText(g_warpList, row, 6, BitMark(ReadBuildingBit(i, BIT_GUILD)));
        row++;
    }
    SendMessageW(g_warpList, WM_SETREDRAW, TRUE, 0);
    if (row > 0) {   // 맨 윗줄을 골라 둔다 — 검색칸에서 엔터만 쳐도 거기로 간다
        LVITEMW s; ZeroMemory(&s, sizeof(s));
        s.mask = LVIF_STATE; s.state = LVIS_SELECTED | LVIS_FOCUSED;
        s.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(g_warpList, LVM_SETITEMSTATE, 0, (LPARAM)&s);
    }
}

// row 번째 줄의 도시로 간다. 갔으면 창을 닫는다(계속 열어 둘 이유가 없다).
static void WarpToRow(HWND h, int row)
{
    LVITEMW it; ZeroMemory(&it, sizeof(it));
    if (row < 0) return;
    it.mask = LVIF_PARAM; it.iItem = row;
    if (!SendMessageW(g_warpList, LVM_GETITEMW, 0, (LPARAM)&it)) return;
    DoWarp((int)it.lParam);
    DestroyWindow(h);
}

// 검색칸에서 바로 골라 가게 한다 — 엔터는 고른 줄로 워프, ↓ 는 목록으로, Esc 는 닫기.
// 게임 메시지 루프는 우리 창에 IsDialogMessage 를 돌려주지 않아 직접 받는다.
static LRESULT CALLBACK WarpEditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) {
            WarpToRow(GetParent(h), (int)SendMessageW(g_warpList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED));
            return 0;
        }
        if (w == VK_ESCAPE) { DestroyWindow(GetParent(h)); return 0; }
        if (w == VK_DOWN)   { SetFocus(g_warpList); return 0; }
    }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;   // 삑 소리 막기
    return CallWindowProcW(g_warpEditOrig, h, m, w, l);
}

static void WarpPaintFrame(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb, cf, tr; HBRUSH br; HFONT of;
    GetClientRect(h, &rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &rc, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);
    tb.left = FRAME; tb.top = FRAME; tb.right = rc.right - FRAME; tb.bottom = FRAME + TITLE_H;
    VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, tb, FALSE);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_titleFont); tr = tb; tr.left += 8;
    DrawTextW(dc, L"워프 — 문화권을 고르거나 이름을 쳐서 엔터", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    // 문화권 탭 — 맨 앞은 [전체]
    {
        int i;
        of = (HFONT)SelectObject(dc, g_listFont);
        for (i = 0; i <= g_wregionN; i++) {
            RECT t = WTabRect(i);
            BOOL on = (i == g_warpRegion);
            VGradient(dc, t, on ? COL_SEL_BG : COL_FACE_TOP, on ? COL_SEL_BG : COL_FACE_BOT);
            Bevel(dc, t, on);
            SetTextColor(dc, on ? COL_SEL_TX : COL_TEXT);
            DrawTextW(dc, i == 0 ? L"전체" : g_wregion[i - 1], -1, &t,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SetTextColor(dc, COL_TEXT);
        SelectObject(dc, of);
    }

    cb = CloseRect(rc);
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &cb, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &cb, br); DeleteObject(br);
    cf = cb; InflateRect(&cf, -2, -2); VGradient(dc, cf, COL_FACE_TOP, COL_FACE_BOT); Bevel(dc, cf, FALSE);
    DrawTextW(dc, L"×", -1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK WarpProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
    {
        int c;
        EnsureFonts();
        BuildWarpRegions();
        g_warpText[0] = 0;
        g_warpFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            FRAME + 5, WFILTER_Y, WWIN_W - 2*FRAME - 10, FILTER_H - 4,
            h, (HMENU)2, g_hinst, NULL);
        SendMessageW(g_warpFilter, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_warpFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"도시 이름 몇 글자 → 엔터");
        g_warpEditOrig = (WNDPROC)SetWindowLongPtrW(g_warpFilter, GWLP_WNDPROC, (LONG_PTR)WarpEditProc);
        g_warpList = CreateWindowExW(0, L"SysListView32", L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            FRAME, WLIST_Y, WWIN_W - 2*FRAME, WWIN_H - FRAME - WLIST_Y,
            h, (HMENU)1, g_hinst, NULL);
        SendMessageW(g_warpList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
        SendMessageW(g_warpList, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_warpList, LVM_SETBKCOLOR, 0, (LPARAM)COL_ROW_A);
        SendMessageW(g_warpList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_ROW_A);
        for (c = 0; c < WCOL_N; c++) AddCol(g_warpList, c, kWCols[c], kWColW[c]);
        { HWND hdr = (HWND)SendMessageW(g_warpList, LVM_GETHEADER, 0, 0);
          if (hdr) SendMessageW(hdr, WM_SETFONT, (WPARAM)g_hdrFont, TRUE); }
        PopulateWarp();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: WarpPaintFrame(h); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 2 && HIWORD(wp) == EN_CHANGE) {
            GetWindowTextW(g_warpFilter, g_warpText, 64);
            PopulateWarp();
            return 0;
        }
        return 0;
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, COL_TEXT);
        SetBkColor(dc, COL_LIGHT);
        if (!g_warpFilterBr) g_warpFilterBr = CreateSolidBrush(COL_LIGHT);
        return (LRESULT)g_warpFilterBr;
    }
    case WM_NOTIFY:
    {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->idFrom != 1) return 0;
        if (nh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                int i = (int)cd->nmcd.dwItemSpec;
                BOOL sel = (ListView_GetItemState(g_warpList, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                if (sel) { cd->clrText = COL_SEL_TX; cd->clrTextBk = COL_SEL_BG; }
                else     { cd->clrText = COL_TEXT;   cd->clrTextBk = (i & 1) ? COL_ROW_B : COL_ROW_A; }
                SelectObject(cd->nmcd.hdc, g_listFont);
                return CDRF_NEWFONT;
            }}
            return CDRF_DODEFAULT;
        }
        if (nh->code == NM_DBLCLK) {
            WarpToRow(h, ((LPNMITEMACTIVATE)lp)->iItem);
            return 0;
        }
        if (nh->code == LVN_KEYDOWN) {
            LPNMLVKEYDOWN kd = (LPNMLVKEYDOWN)lp;
            if (kd->wVKey == VK_RETURN)
                WarpToRow(h, (int)SendMessageW(g_warpList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED));
            else if (kd->wVKey == VK_ESCAPE) DestroyWindow(h);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb; int i;
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        GetClientRect(h, &rc); cb = CloseRect(rc);
        if (PtInRect(&cb, pt)) { DestroyWindow(h); return 0; }
        for (i = 0; i <= g_wregionN; i++) {          // 문화권 탭
            RECT t = WTabRect(i);
            if (!PtInRect(&t, pt)) continue;
            g_warpRegion = i;
            PopulateWarp();
            InvalidateRect(h, NULL, FALSE);
            SetFocus(g_warpFilter);
            return 0;
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_warpFilter && g_warpEditOrig)
            SetWindowLongPtrW(g_warpFilter, GWLP_WNDPROC, (LONG_PTR)g_warpEditOrig);
        if (g_warpFilterBr) { DeleteObject(g_warpFilterBr); g_warpFilterBr = NULL; }
        g_warpWnd = NULL; g_warpList = NULL; g_warpFilter = NULL; g_warpEditOrig = NULL;
        g_warpText[0] = 0;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowWarpWindow(HWND owner)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT; RECT orc;
    if (g_warpWnd) { SetForegroundWindow(g_warpWnd); SetFocus(g_warpFilter); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = WarpProc; wc.hInstance = g_hinst; wc.lpszClassName = WC_WARP;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc);
        reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - WWIN_W) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - WWIN_H) / 2;
        if (x < 0) x = 0; if (y < 0) y = 0;
    }
    g_warpWnd = CreateWindowExW(0, WC_WARP, L"워프", WS_POPUP, x, y, WWIN_W, WWIN_H, owner, NULL, g_hinst, NULL);
    if (g_warpWnd) {
        ShowWindow(g_warpWnd, SW_SHOW);
        UpdateWindow(g_warpWnd);
        SetForegroundWindow(g_warpWnd);
        SetFocus(g_warpFilter);          // 열자마자 바로 도시 이름을 칠 수 있게
    }
}

// ---------------- 도시 진입 (시세 일람에서 두 번 누르기) ----------------
//
// 게임은 배(또는 말)가 도시 옆을 지날 때 "[리스본]의 항구로 들어가겠습니까?" 를 묻고,
// 답이 YES 면 그 도시로 들여보낸다. 그 일을 하는 것이 근접 검사 함수 0x48D810 인데,
// 그 함수는 (1) 함대 좌표 둘레를 훑어 도시를 고르는 대목과 (2) 물어보고 들여보내는
// 대목이 한 몸이고, 항해 화면 객체(this)를 받는다. 우리는 도시를 이미 알고 있으므로
// (2) 만 — 0x48DB7F ~ 0x48DC55 를 — 도시 번호를 직접 받는 꼴로 옮겨 적었다.
// 새로 지어낸 절차가 아니라 게임이 하던 그대로다: 부르는 함수도, 쓰는 자리도 같다.
//
//   0x48DB7F  ebx = [0x5B61B4] ? "도시" : "항구"
//   0x48DB9D  esi = ([0x5B3950] >= 60) ? ebx : 조사(ebx)      ; 지친 문구용
//   0x48DBB5  eax = 조사(ebx, 0xA)                            ; "로" / "으로"
//   0x48DBC5  이름 = *(char**)(0x4D14B0 + 도시*136)           ; 게임 0x429980 의 결과
//   0x48DBE5  물음 = 0x49E3E0(2, 0, 서식, 이름, ebx, esi, eax)
//   0x48DBED  물음 == 2(YES) 면 아래 열 줄을 실행 → 게임이 다음 판에 도시 화면으로 넘어간다
#define GF_ASK      0x0049E3E0u  // __cdecl int(int 갈래, int, const char* 서식, ...) — YES 면 2
#define GF_JOSA     0x004281B0u  // __cdecl const char*(const char* 말, int 갈래) — 0xA 는 "로/으로"
#define GF_MEMBER   0x00473DC0u  // __thiscall void*(this=0x5B3928, int i) — 파티원 i (없으면 0)
#define GF_SETCITY  0x0044CA70u  // __thiscall void(파티원, int 도시) — 그 사람의 현재 도시(+0x60)
#define PARTY_OBJ   0x005B3928u
#define STR_CITY    0x00570868u  // "도시"
#define STR_PORT    0x00570870u  // "항구"
#define STR_TIRED   0x00570878u  // "[%s]의 %s입니다. 모두 지쳐 있으니 %s%s 들어갑시다."
#define STR_ASK     0x005708B0u  // "[%s]의 %s%s 들어가겠습니까?"
#define CUR_CITY    0x005B6154u  // 지금 있는 도시. -1 이면 항해 중
#define JUST_IN     0x005B6384u  // "방금 들어왔다" — 도시 화면이 보고 지운다
#define IN_MODE     0x005B6158u
#define ON_LAND     0x005B61B4u  // 0 이면 배, 아니면 육상(말)
#define CREW_TIRED  0x005B3950u  // 60 이상이면 문구가 "모두 지쳐 있으니 …" 로 바뀐다
#define SAIL_STATE  0x005A4D40u  // 들어갈 때 게임이 같이 0 으로 되돌리는 둘
#define SAIL_STATE2 0x005B39FCu
// 도시 구조체 0번. SISE_BASE 가 그 안의 +0x0C(시세)라 12를 뺀 자리다(게임 0x429950 과 같다).
#define CITY_STRUCT (SISE_BASE - 0x0Cu)

// 부를 함수가 정말 그 함수인지 앞머리 몇 바이트로 확인한다. 게임 실행 파일이 다르면
// (다른 판·다른 패치) 여기서 걸러 내고 아무 일도 안 한다 — 엉뚱한 자리를 부르느니 낫다.
static BOOL FnSig(unsigned addr, const unsigned char* sig, int n)
{
    const unsigned char* p = (const unsigned char*)addr;
    int i;
    if (IsBadReadPtr(p, (UINT_PTR)n)) return FALSE;
    for (i = 0; i < n; i++) if (p[i] != sig[i]) return FALSE;
    return TRUE;
}

static BOOL EnterFnsOk(void)
{
    static const unsigned char kAsk[]  = { 0x64,0xA1,0x00,0x00,0x00,0x00,0x55,0x8B,0xEC };
    static const unsigned char kJosa[] = { 0x81,0xEC,0xCC,0x06,0x00,0x00 };
    static const unsigned char kMem[]  = { 0x56,0x57,0x8B,0xF9,0x8B,0x74,0x24,0x0C };
    static const unsigned char kSet[]  = { 0x8B,0x44,0x24,0x04,0x89,0x41,0x60,0xC2,0x04,0x00 };
    return FnSig(GF_ASK,     kAsk,  sizeof(kAsk))
        && FnSig(GF_JOSA,    kJosa, sizeof(kJosa))
        && FnSig(GF_MEMBER,  kMem,  sizeof(kMem))
        && FnSig(GF_SETCITY, kSet,  sizeof(kSet));
}

// 지금 그 도시에 들어갈 수 있는가. 게임 0x48DAC7~0x48DAF1 의 판정 그대로다.
//   [도시+0x04] bit0 서 있어야 하고 bit2 는 서 있으면 안 된다(지도에 없거나 들를 수 없는 곳)
//   배로 갈 때는 [도시+0x1C] bit0(항구), 육상으로 갈 때는 [도시+0x1D] bit2 가 서 있어야 한다.
//   +0x1C 는 우리 BUILDING_OFF 와 같은 자리이고 bit0 이 BIT_PORT 다.
static BOOL CanEnterCity(int city)
{
    const unsigned char* c = (const unsigned char*)(CITY_STRUCT + (unsigned)city * CITY_STRIDE);
    if (city < 0 || city >= CITY_COUNT) return FALSE;
    if (IsBadReadPtr(c, 0x20)) return FALSE;
    if (!(c[4] & 1)) return FALSE;
    if (c[4] & 4)    return FALSE;
    return *(const int*)ON_LAND ? ((c[0x1D] & 4) != 0) : ((c[0x1C] & 1) != 0);
}

// 물어보고, YES 면 들여보낸다. 게임 스레드(게임 창 프로시저)에서만 부른다.
static void AskAndEnterCity(int city)
{
    typedef int         (__cdecl   *AskFn)(int, int, const char*, ...);
    typedef const char* (__cdecl   *JosaFn)(const char*, int);
    typedef void*       (__fastcall *MemberFn)(void*, void*, int);
    typedef void        (__fastcall *SetCityFn)(void*, void*, int);
    // __thiscall 은 C 에서 못 쓰므로 __fastcall 로 부른다 — 첫 인자가 ecx 로 가는 것은 같고,
    // 받는 쪽은 edx 를 안 본다. 둘 다 ret 4 라 스택도 __fastcall 과 맞는다.
    const char* name = *(const char* const*)(REGION_TABLE + (unsigned)city * REGION_STRIDE);
    const char* kind = *(const int*)ON_LAND ? (const char*)STR_CITY : (const char*)STR_PORT;
    const char* josa = ((JosaFn)GF_JOSA)(kind, 0xA);
    BOOL tired = (*(const int*)CREW_TIRED >= 60);
    const char* fmt  = tired ? (const char*)STR_TIRED : (const char*)STR_ASK;

    if (!name || IsBadReadPtr(name, 1)) return;
    if (((AskFn)GF_ASK)(2, 0, fmt, name, kind, tired ? kind : josa, josa) != 2) return;

    // ── 여기부터 게임의 YES 갈래(0x48DBF2~0x48DC55) 그대로 ──
    *(int*)JUST_IN  = 1;
    *(int*)CUR_CITY = city;
    if (*(const int*)ON_LAND != 0)
    {
        *(int*)IN_MODE = 0xA;
    }
    else
    {
        int i;
        for (i = 0; i < 8; i++)      // 배로 들어갈 때는 파티원 여덟의 현재 도시도 같이 옮긴다
        {
            void* m = ((MemberFn)GF_MEMBER)((void*)PARTY_OBJ, 0, i);
            if (m) ((SetCityFn)GF_SETCITY)(m, 0, city);
        }
        *(int*)IN_MODE = 0;
    }
    *(int*)SAIL_STATE    = 0;
    *(short*)SAIL_STATE2 = 0;
}

// 시세 일람에서 부친 심부름을 게임 스레드가 받아 처리한다.
// 항해 중일 때만 한다 — 도시 안에서는 게임이 출항할 때 정박 전 좌표를 되돌리므로
// (0x48B5C0) 워프도 진입도 뜻대로 되지 않는다. 그래서 좌표조차 건드리지 않는다.
static void GoCity(int city)
{
    if (city < 0 || city >= WARP_COUNT) return;
    if (*(const int*)CUR_CITY != -1)     // 도시 안이다 — 좌표도 건드리지 않는다
    {
        MessageBoxW(g_subHwnd, L"항해 중에만 도시로 갈 수 있습니다.\n"
                            L"도시에서 나온 뒤 다시 눌러 주세요.",
                    L"도시로 가기", MB_OK | MB_ICONINFORMATION);
        return;
    }
    DoWarp(city);
    if (!EnterFnsOk()) return;      // 게임 실행 파일이 우리가 아는 그것이 아니다
    if (!CanEnterCity(city)) return; // 지금 이 수단으로는 못 들어가는 곳 — 옮기기만 한다
    AskAndEnterCity(city);
}

// ---------------- 메뉴 통합 (서브클래싱) ----------------

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    // "최근 방문"을 펼치기 직전에 항목을 새로 깐다. 게임 UI 스레드에서만 메뉴를 건드린다.
    if (msg == WM_INITMENUPOPUP && g_recentMenu && (HMENU)wp == g_recentMenu)
        RebuildRecentMenu();
    // 시세 일람에서 두 번 누른 도시 — 여기가 게임 스레드다.
    if (g_msgGoCity && msg == g_msgGoCity) { GoCity((int)wp); return 0; }
    if (msg == WM_COMMAND && HIWORD(wp) == 0)
    {
        WORD id = LOWORD(wp);
        if (id == ID_TRADE_SISE)  { ShowSiseWindow(h); return 0; }
        if (id == ID_TRADE_GOODS) { ShowTradeGoodsWindow(h); return 0; }
        if (id == ID_TRADE_WARP)  { ShowWarpWindow(h); return 0; }
        if (id >= ID_WARP_BASE && id < ID_WARP_BASE + WARP_COUNT)
        {
            DoWarp(id - ID_WARP_BASE);
            return 0;
        }
    }
    if (msg == WM_NCDESTROY)
    {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_hwnd = NULL;
        return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
    return op ? CallWindowProcW(op, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0;
    (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h))
    {
        g_hwnd = h;
        return FALSE;
    }
    return TRUE;
}

static BOOL HasOurMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i;
    WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"워프") == 0)
            return TRUE;
    return FALSE;
}
// 최상위 메뉴바에서 "파일" 팝업 서브메뉴를 찾는다. 없으면 NULL.
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    // 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙으므로 접두어로 매칭한다.
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}
// "파일" 안에 KR 플러그인 항목(ID 0xB000~0xCFFF)이 이미 있는지 → 최초 설치 플러그인만 구분선 추가.
static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}
// 연속된 구분선을 1개로 접는다(변경했으면 TRUE). 세 플러그인 스레드 race 로 구분선이
// 2~3개 생겨도 다음 폴링에서 자동으로 하나로 수렴시킨다.
static BOOL CollapseSeparators(HMENU m)
{
    BOOL changed = FALSE; int i;
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) {
            RemoveMenu(m, (UINT)i, MF_BYPOSITION);
            changed = TRUE;
        }
    }
    return changed;
}

static DWORD WINAPI MonitorThread(LPVOID param)
{
    (void)param;
    OutputDebugStringW(L"[TradeUtilKR] monitor thread started.");
    for (;;)
    {
        HMENU bar;
        g_hwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_hwnd)
        {
            bar = GetMenu(g_hwnd);
            if (bar)
            {
                // "파일" 드롭다운이 있으면 그 안에, 없으면 예전처럼 최상위에 붙인다.
                HMENU fileMenu = FindFileMenu(bar);
                HMENU target = fileMenu ? fileMenu : bar;
                if (!HasOurMenu(target))
                {
                    HMENU warp; int i;
                    // 같은 지역이 떨어져 있어도 서브메뉴는 하나로 모은다.
                    // (도시 ID 순서로는 북유럽·지중해·중근동이 두 덩어리로 나뉘어 있다.)
                    const wchar_t* rname[WARP_REGION_MAX]; HMENU rmenu[WARP_REGION_MAX]; int rn = 0;
                    if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                        AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL); // 게임 원래 항목과 구분(최초 1회)
                    // 교역 · 교역품 창은 [정보] 창에서 연다 — 메뉴에 항목이 너무 많아졌다.
                    // 여기서는 워프만 단다. 창을 여는 커맨드(ID_TRADE_SISE/GOODS)는 그대로라
                    // [정보] 쪽에서 그 ID 로 WM_COMMAND 를 보내면 열린다.
                    // fb14: "워프" — 지역별 서브메뉴로 목적지 선택 → 클릭 시 순간이동.
                    warp = CreatePopupMenu();
                    // 맨 위는 찾아서 가는 창(단축키 W). 아래 지역 서브메뉴는 그대로 둔다.
                    AppendMenuW(warp, MF_STRING, ID_TRADE_WARP, L"찾아서 가기…");
                    AppendMenuW(warp, MF_SEPARATOR, 0, NULL);
                    // 최근 다녀온 곳을 맨 위에. 항목은 펼칠 때마다 새로 채운다(RebuildRecentMenu).
                    g_recentMenu = CreatePopupMenu();
                    AppendMenuW(g_recentMenu, MF_STRING | MF_GRAYED, 0, L"(아직 없음)");
                    AppendMenuW(warp, MF_POPUP, (UINT_PTR)g_recentMenu, L"최근 방문");
                    AppendMenuW(warp, MF_SEPARATOR, 0, NULL);
                    for (i = 0; i < WARP_COUNT; i++)
                    {
                        int r;
                        for (r = 0; r < rn; r++)
                            if (lstrcmpW(rname[r], kWarps[i].region) == 0) break;
                        if (r == rn)
                        {
                            if (rn >= WARP_REGION_MAX) continue;   // 표를 넘기면 그 도시는 건너뛴다
                            rmenu[rn] = CreatePopupMenu();
                            rname[rn] = kWarps[i].region;
                            AppendMenuW(warp, MF_POPUP, (UINT_PTR)rmenu[rn], kWarps[i].region);
                            rn++;
                        }
                        AppendMenuW(rmenu[r], MF_STRING, ID_WARP_BASE + i, kWarps[i].city);
                    }
                    AppendMenuW(target, MF_POPUP, (UINT_PTR)warp, L"워프");
                    DrawMenuBar(g_hwnd);
                    OutputDebugStringW(L"[TradeUtilKR] 교역/워프 menu (re)installed.");
                }
                if (fileMenu && CollapseSeparators(fileMenu)) DrawMenuBar(g_hwnd); // race 로 생긴 중복 구분선 정리
                if (g_subHwnd != g_hwnd)
                {
                    g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                    g_subHwnd = g_hwnd;
                    OutputDebugStringW(L"[TradeUtilKR] window subclassed.");
                }
            }
        }
        Sleep(1000);
    }
}

void TradeKR_Init(HINSTANCE hinst)
{
    INITCOMMONCONTROLSEX icc;
    HANDLE t;
    g_hinst = hinst;
    icc.dwSize = sizeof(icc); icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    // 게임 창에 부칠 우리 전용 메시지. 다른 플러그인도 같은 창을 서브클래싱하므로
    // WM_APP+n 을 눈대중으로 고르지 않고 시스템에서 겹치지 않는 번호를 받아 쓴다.
    g_msgGoCity = RegisterWindowMessageW(L"TradeUtilKR_GoCity");
    OutputDebugStringW(L"[TradeUtilKR] init.");
    t = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
