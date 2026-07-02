#include "trade.h"
#include <commctrl.h>
#include "cities_data.h"

// TradeUtilKR — 한국어판 전용 "교역" 메뉴 + 시세 일람 창.
// 게임 메뉴바에 항목을 추가하고(서브클래싱으로 클릭 가로챔), 클릭 시 전 도시 목록을 표시한다.
//
// Phase 2a: 도시명/문화권/시설(임베드 cities_data.h)로 리스트뷰를 채운다.
// Phase 2b(예정): 국적/시세/규모/투자액/방문·발견을 게임 메모리에서 읽어 컬럼 추가.

#define ID_TRADE_SISE 0xB101
#define WC_SISE       L"TradeUtilKR_Sise"
#define CITY_COUNT    (int)(sizeof(kCities)/sizeof(kCities[0]))

// 게임 라이브 메모리: 도시별 시세 배열 (2026-07-03 배열 시그니처 스캔으로 확정).
//   시세 = u16 @ (SISE_BASE + 도시ID * CITY_STRIDE),  도시0=리스본.
//   226/226 슬롯이 90~105 범위, 현재도시(리스본)=100 으로 검증. 도시 구조체 크기=92바이트.
// 플러그인은 cds_95.exe 프로세스 내부에서 실행되므로 절대주소를 직접 역참조한다.
#define SISE_BASE     0x005863B4u
#define CITY_STRIDE   92

// 시세 필드(SISE_BASE) 기준 +8 바이트 = 도시 플래그 비트필드 (2026-07-03 패턴 스캔으로 특정).
//   bit4 = 조합(guild) 보유 여부 — 알려진 6도시 패턴과 유일 일치로 확정.
//   나머지 비트(0~3,5~7)는 방문/발견/건설 등 후보(의미 미확정).
#define FLAGS_OFF     8
#define GUILD_BIT     4

// 도시 i 의 라이브 시세를 읽는다. 주소가 매핑 안 돼 있으면 -1 반환(방어).
static int ReadSise(int i)
{
    const unsigned short* p = (const unsigned short*)(SISE_BASE + (unsigned)i * CITY_STRIDE);
    if (IsBadReadPtr(p, sizeof(*p))) return -1;
    return (int)*p;
}

// 도시 i 의 플래그 바이트에서 지정 비트를 읽는다. 매핑 안 돼 있으면 -1.
static int ReadFlagBit(int i, int bit)
{
    const unsigned char* p = (const unsigned char*)(SISE_BASE + (unsigned)i * CITY_STRIDE + FLAGS_OFF);
    if (IsBadReadPtr(p, 1)) return -1;
    return (int)((*p >> bit) & 1);
}

static HINSTANCE g_hinst = NULL;
static HWND    g_hwnd = NULL;      // 게임 메인 창
static HWND    g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static HWND    g_siseWnd = NULL;   // 시세 일람 창
static HWND    g_list = NULL;

// ---------------- 시세 일람 창 ----------------

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

static void PopulateList(HWND lv)
{
    int i;
    for (i = 0; i < CITY_COUNT; i++)
    {
        LVITEMW it; wchar_t num[8];
        wsprintfW(num, L"%d", i);
        int sise, guild; wchar_t sbuf[8];
        it.mask = LVIF_TEXT; it.iItem = i; it.iSubItem = 0; it.pszText = num;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        SetText(lv, i, 1, kCities[i].name);
        SetText(lv, i, 2, kCities[i].sphere);
        sise = ReadSise(i);
        if (sise < 0) wsprintfW(sbuf, L"-"); else wsprintfW(sbuf, L"%d", sise);
        SetText(lv, i, 3, sbuf);
        SetText(lv, i, 4, kCities[i].lib   ? L"○" : L"×");
        SetText(lv, i, 5, kCities[i].ship  ? L"○" : L"×");
        guild = ReadFlagBit(i, GUILD_BIT);   // 조합: 라이브 플래그 비트(정적 데이터는 부정확)
        SetText(lv, i, 6, guild == 1 ? L"○" : (guild == 0 ? L"×" : L"-"));
    }
}

static LRESULT CALLBACK SiseProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
        g_list = CreateWindowExW(0, L"SysListView32", L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                 0, 0, 0, 0, h, (HMENU)1, g_hinst, NULL);
        SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                     LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddCol(g_list, 0, L"번호", 44);
        AddCol(g_list, 1, L"도시명", 130);
        AddCol(g_list, 2, L"문화권", 90);
        AddCol(g_list, 3, L"시세", 56);
        AddCol(g_list, 4, L"도서관", 56);
        AddCol(g_list, 5, L"조선소", 56);
        AddCol(g_list, 6, L"조합", 50);
        PopulateList(g_list);
        return 0;
    case WM_SIZE:
        if (g_list) MoveWindow(g_list, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_siseWnd = NULL; g_list = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowSiseWindow(HWND owner)
{
    static BOOL reg = FALSE;
    if (g_siseWnd) { SetForegroundWindow(g_siseWnd); return; }
    if (!reg)
    {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = SiseProc;
        wc.hInstance = g_hinst;
        wc.lpszClassName = WC_SISE;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        reg = TRUE;
    }
    g_siseWnd = CreateWindowExW(0, WC_SISE, L"시세 일람",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 512, 480,
                                owner, NULL, g_hinst, NULL);
    if (g_siseWnd) { ShowWindow(g_siseWnd, SW_SHOW); UpdateWindow(g_siseWnd); }
}

// ---------------- 메뉴 통합 (서브클래싱) ----------------

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && LOWORD(wp) == ID_TRADE_SISE && HIWORD(wp) == 0)
    {
        ShowSiseWindow(h);
        return 0;
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
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"교역") == 0)
            return TRUE;
    return FALSE;
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
                if (!HasOurMenu(bar))
                {
                    HMENU pop = CreatePopupMenu();
                    AppendMenuW(pop, MF_STRING, ID_TRADE_SISE, L"시세 일람");
                    AppendMenuW(bar, MF_POPUP, (UINT_PTR)pop, L"교역");
                    DrawMenuBar(g_hwnd);
                    OutputDebugStringW(L"[TradeUtilKR] 교역 menu (re)installed.");
                }
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
    OutputDebugStringW(L"[TradeUtilKR] init.");
    t = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
