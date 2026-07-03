#include "trade.h"
#include <commctrl.h>
#include <windowsx.h>
#include <string.h>
#include "cities_data.h"
#include "item_names.h"
#include "warp_data.h"

// TradeUtilKR — 한국어판 전용 "교역" 메뉴 + 시세 일람 창.
// 게임 메뉴바에 항목을 추가하고(서브클래싱으로 클릭 가로챔), 클릭 시 전 도시 목록을 표시한다.
//
// Phase 2a: 도시명/문화권/시설(임베드 cities_data.h)로 리스트뷰를 채운다.
// Phase 2b(예정): 국적/시세/규모/투자액/방문·발견을 게임 메모리에서 읽어 컬럼 추가.

#define ID_TRADE_SISE 0xB101
#define ID_WARP_BASE  0xC000        // 워프 메뉴 항목 ID = ID_WARP_BASE + kWarps 인덱스
#define WC_SISE       L"TradeUtilKR_Sise"
#define CITY_COUNT    (int)(sizeof(kCities)/sizeof(kCities[0]))
#define WARP_COUNT    (int)(sizeof(kWarps)/sizeof(kWarps[0]))

// fb14: 순간이동(워프). ce/CDS_95.CT "순간이동용" = 현재 위치를 담는 16바이트 @ 0x005B63A8.
//   목적지 도시의 16바이트를 여기에 쓰면 그 도시로 이동한다(현재값이 목록의 현위치와 일치함을 확인).
#define WARP_ADDR     0x005B63A8u

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
}

// 게임 라이브 메모리: 도시별 시세 배열 (2026-07-03 배열 시그니처 스캔으로 확정).
//   시세 = u16 @ (SISE_BASE + 도시ID * CITY_STRIDE),  도시0=리스본.
//   226/226 슬롯이 90~105 범위, 현재도시(리스본)=100 으로 검증. 도시 구조체 크기=92바이트.
// 플러그인은 cds_95.exe 프로세스 내부에서 실행되므로 절대주소를 직접 역참조한다.
#define SISE_BASE     0x005863B4u
#define CITY_STRIDE   92

// 도시 구조체(stride 92) 내 필드 오프셋 — SISE_BASE(=시세) 기준. ce/CDS_95.CT 라벨로 확정(2026-07-03).
//   규모(0~7)    : -4   (i32)
//   시세         :  0   (i32; 값이 작아 u16 로도 읽힘)
//   플래그 비트필드: +8   (byte) — bit4=조합(guild). 나머지 비트는 방문/발견/건설 후보(미확정).
//   시장 아이템1~8: +20~+48 (i32 ×8) — 값 = item_names.h 인덱스, 빈 슬롯은 -1(로드시)/0.
#define SCALE_OFF     (-4)
#define FLAGS_OFF     8
#define GUILD_BIT     4
#define MKT_ITEM_OFF  20
#define MKT_ITEM_MAX  8

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

// 도시 i 의 라이브 시세. 매핑 안 돼 있으면 -1.
static int ReadSise(int i)
{
    int v; return ReadI32(CityField(i, 0), &v) ? v : -1;
}

// 도시 i 의 규모(0~7). 매핑 안 돼 있으면 -1.
static int ReadScale(int i)
{
    int v; return ReadI32(CityField(i, SCALE_OFF), &v) ? v : -1;
}

// 도시 i 의 플래그 비트. 매핑 안 돼 있으면 -1.
static int ReadFlagBit(int i, int bit)
{
    const unsigned char* p = (const unsigned char*)CityField(i, FLAGS_OFF);
    if (IsBadReadPtr(p, 1)) return -1;
    return (int)((*p >> bit) & 1);
}

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
static HWND    g_hdr = NULL;       // 리스트뷰 헤더(오너드로우용 서브클래스)
static WNDPROC g_origHdr = NULL;
static HFONT   g_titleFont = NULL;
static HFONT   g_hdrFont = NULL;
static HFONT   g_listFont = NULL;

// ---------------- 시세 일람 창 (여관 다이얼로그와 같은 세피아/브론즈 오너드로우) ----------------

// 창 레이아웃 (컬럼 총폭보다 좁으면 리스트뷰가 가로 스크롤)
#define WIN_W    620
#define WIN_H    560
#define FRAME    3        // 갈색 외곽 프레임 두께
#define TITLE_H  26       // 커스텀 타이틀바 높이

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
#define COL_COUNT 10
static const wchar_t* kCols[COL_COUNT] = {
    L"번호", L"도시명", L"문화권", L"규모", L"시세", L"시장", L"도서관", L"조선소", L"조합", L"시장아이템"
};
static const int kColW[COL_COUNT] = { 40, 104, 78, 40, 46, 40, 52, 52, 44, 240 };

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
            if (i < COL_COUNT) DrawTextW(dc, kCols[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

static void PopulateList(HWND lv)
{
    int i;
    for (i = 0; i < CITY_COUNT; i++)
    {
        LVITEMW it; wchar_t num[8], sbuf[12], mkt[256];
        int sise, scale, guild, nItem;
        wsprintfW(num, L"%d", i);
        it.mask = LVIF_TEXT; it.iItem = i; it.iSubItem = 0; it.pszText = num;
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        SetText(lv, i, 1, kCities[i].name);
        SetText(lv, i, 2, kCities[i].sphere);
        scale = ReadScale(i);
        if (scale < 0) wsprintfW(sbuf, L"-"); else wsprintfW(sbuf, L"%d", scale);
        SetText(lv, i, 3, sbuf);
        sise = ReadSise(i);
        if (sise < 0) wsprintfW(sbuf, L"-"); else wsprintfW(sbuf, L"%d", sise);
        SetText(lv, i, 4, sbuf);
        nItem = BuildMarketItems(i, mkt, 256);       // 시장아이템 목록 + 유무 판정
        SetText(lv, i, 5, nItem > 0 ? L"○" : L"×");  // 시장 유무
        SetText(lv, i, 6, kCities[i].lib   ? L"○" : L"×");
        SetText(lv, i, 7, kCities[i].ship  ? L"○" : L"×");
        guild = ReadFlagBit(i, GUILD_BIT);   // 조합: 라이브 플래그 비트(정적 데이터는 부정확)
        SetText(lv, i, 8, guild == 1 ? L"○" : (guild == 0 ? L"×" : L"-"));
        SetText(lv, i, 9, mkt);              // 시장아이템 이름 나열
    }
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
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK SiseProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m)
    {
    case WM_CREATE:
        g_titleFont = CreateFontW(-16, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
        g_hdrFont   = CreateFontW(-13, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
        g_listFont  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, 0, 0, L"바탕");
        g_list = CreateWindowExW(0, L"SysListView32", L"",
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
                                 FRAME, FRAME + TITLE_H,
                                 WIN_W - 2 * FRAME, WIN_H - 2 * FRAME - TITLE_H,
                                 h, (HMENU)1, g_hinst, NULL);
        SendMessageW(g_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
        SendMessageW(g_list, WM_SETFONT, (WPARAM)g_listFont, TRUE);
        SendMessageW(g_list, LVM_SETBKCOLOR, 0, (LPARAM)COL_ROW_A);
        SendMessageW(g_list, LVM_SETTEXTBKCOLOR, 0, (LPARAM)COL_ROW_A);
        {
            int c;
            for (c = 0; c < COL_COUNT; c++) AddCol(g_list, c, kCols[c], kColW[c]);
        }
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
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb;
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        GetClientRect(h, &rc); cb = CloseRect(rc);
        if (PtInRect(&cb, pt)) { DestroyWindow(h); return 0; }
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
        if (g_titleFont) { DeleteObject(g_titleFont); g_titleFont = NULL; }
        if (g_hdrFont)   { DeleteObject(g_hdrFont);   g_hdrFont = NULL; }
        if (g_listFont)  { DeleteObject(g_listFont);  g_listFont = NULL; }
        g_hdr = NULL; g_origHdr = NULL; g_siseWnd = NULL; g_list = NULL;
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

// ---------------- 메뉴 통합 (서브클래싱) ----------------

static LRESULT CALLBACK SubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC op = g_origProc;
    if (msg == WM_COMMAND && HIWORD(wp) == 0)
    {
        WORD id = LOWORD(wp);
        if (id == ID_TRADE_SISE) { ShowSiseWindow(h); return 0; }
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
                    HMENU warp, sub = NULL; const wchar_t* region = NULL; int i;
                    // fb13: "교역"을 드롭다운이 아니라 클릭 즉시 시세 일람이 뜨는 커맨드 항목으로.
                    // (최상위 메뉴바의 MF_STRING 항목은 클릭 시 WM_COMMAND 를 보낸다)
                    AppendMenuW(bar, MF_STRING, ID_TRADE_SISE, L"교역");
                    // fb14: "워프" — 지역별 서브메뉴로 목적지 선택 → 클릭 시 순간이동.
                    warp = CreatePopupMenu();
                    for (i = 0; i < WARP_COUNT; i++)
                    {
                        if (!region || lstrcmpW(region, kWarps[i].region) != 0)
                        {
                            sub = CreatePopupMenu();
                            AppendMenuW(warp, MF_POPUP, (UINT_PTR)sub, kWarps[i].region);
                            region = kWarps[i].region;
                        }
                        AppendMenuW(sub, MF_STRING, ID_WARP_BASE + i, kWarps[i].city);
                    }
                    AppendMenuW(bar, MF_POPUP, (UINT_PTR)warp, L"워프");
                    DrawMenuBar(g_hwnd);
                    OutputDebugStringW(L"[TradeUtilKR] 교역/워프 menu (re)installed.");
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
