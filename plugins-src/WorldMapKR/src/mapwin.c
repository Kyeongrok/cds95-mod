#include "mapwin.h"
#include "world.h"
#include "uikit.h"
#include "citydb.h"        // 도시 좌표/이름 (CDS95Util\cities.json, 없으면 내장 표)
#include <windowsx.h>

// WorldMapKR — WORLD.CDS 를 그린 세계지도 위에 지금 함대 위치를 찍어 보여주는 창.
// 읽기만 한다(게임 메모리에 쓰지 않는다).
//
// 현재 좌표는 실행 중인 게임 메모리에서 읽는다. 주소는 ce/CDS_95.CT 의
//   경도 CDS_95.EXE+1B63B0 (0=서경180, 20000=0, 40000=동경180)
//   위도 CDS_95.EXE+1B63B4 (0=북위90, 10000=0, 20000=남위90)
// 둘 다 .data 의 초기화되지 않은 뒷부분이라 EXE 파일에는 없고 실행 중에만 있다.
// (cds-helper 는 화면 OCR 로 읽지만, 플러그인은 게임 프로세스 안에 있으니 곧바로 읽으면 된다.)

#define WC_MAP   L"WorldMapKR_Window"

#define POS_LON_RVA 0x1B63B0u
#define POS_LAT_RVA 0x1B63B4u
#define LON_RAW_MAX 40000
#define LAT_RAW_MAX 20000

#define MAP_W  900
#define MAP_H  450                        // 경도 360 : 위도 180 = 2 : 1
#define MAP_X  (FRAME + 8)
#define MAP_Y  (FRAME + TITLE_H + 6)
#define INFO_H 24
#define WM_W   (MAP_X * 2 + MAP_W)
#define WM_H   (MAP_Y + MAP_H + INFO_H + FRAME)

#define TIMER_ID  1
#define TIMER_MS  500

// 휠로 오갈 배율. 1 = 세계 전체. 8 배면 한 칸이 화면 3픽셀쯤이라 더 키워 봐야 계단만 커진다.
static const int kZoom[] = { 1, 2, 3, 4, 6, 8 };
#define ZOOM_N     ((int)(sizeof(kZoom)/sizeof(kZoom[0])))
#define ZOOM_START 3       // 창을 열 때 배율(kZoom[3] = 4배). 함대 자리를 가운데 놓고 연다

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static RECT      g_closeRect;
static int       g_lon = -1, g_lat = -1;   // 마지막으로 읽은 원본값. -1 = 못 읽음

static int  g_zi = 0;                      // kZoom 색인
static int  g_vx = 0, g_vy = 0;            // 보이는 영역의 좌상단(셀 좌표)
static int  g_cities = 1;                  // 도시 표시 켜짐
static int  g_drag = 0;                    // 지도를 끌고 있는 중인지
static POINT g_dragPt;                     // 끌기 시작점(클라이언트 좌표)
static int  g_dragVx, g_dragVy;            // 끌기 시작 시점의 g_vx/g_vy

static int ViewW(void) { return WORLD_UNFOLD_W / kZoom[g_zi]; }
static int ViewH(void) { return WORLD_CELL_H  / kZoom[g_zi]; }

// 보이는 영역이 지도 밖으로 나가지 않게 한다(경도는 잇지 않고 그냥 막는다).
static void ClampView(void)
{
    int mx = WORLD_UNFOLD_W - ViewW();
    int my = WORLD_CELL_H  - ViewH();
    if (g_vx < 0) g_vx = 0;
    if (g_vx > mx) g_vx = mx;
    if (g_vy < 0) g_vy = 0;
    if (g_vy > my) g_vy = my;
}

static void Redraw(HWND h)
{
    ClampView();
    World_RenderView(MAP_W, MAP_H, g_vx, g_vy, ViewW(), ViewH());
    InvalidateRect(h, NULL, FALSE);
}

// 그 셀이 화면 한가운데 오도록 보이는 영역을 맞춘다.
static void CenterOn(int cellX, int cellY)
{
    g_vx = cellX - ViewW() / 2;
    g_vy = cellY - ViewH() / 2;
    ClampView();
}

// 커서 밑에 있던 지점이 제자리에 남도록 배율을 바꾼다(지도 프로그램의 기본 동작).
static void ZoomAt(HWND h, int dir, POINT pt)
{
    int zi = g_zi + dir;
    int cellX, cellY, mx, my;

    if (zi < 0) zi = 0;
    if (zi >= ZOOM_N) zi = ZOOM_N - 1;
    if (zi == g_zi) return;

    mx = pt.x - MAP_X; my = pt.y - MAP_Y;
    if (mx < 0) mx = 0; if (mx > MAP_W) mx = MAP_W;
    if (my < 0) my = 0; if (my > MAP_H) my = MAP_H;

    cellX = g_vx + mx * ViewW() / MAP_W;
    cellY = g_vy + my * ViewH() / MAP_H;
    g_zi = zi;
    g_vx = cellX - mx * ViewW() / MAP_W;
    g_vy = cellY - my * ViewH() / MAP_H;
    Redraw(h);
}

static int Readable(const void* p, SIZE_T n)
{
    const unsigned char* q = (const unsigned char*)p;
    const unsigned char* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

// 지금 함대 좌표(원본값). 성공 1.
static int ReadPos(int* lon, int* lat)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    int lo, la;

    if (!base) return 0;
    if (!Readable(base + POS_LON_RVA, sizeof(int) * 2)) return 0;
    lo = *(const int*)(base + POS_LON_RVA);
    la = *(const int*)(base + POS_LAT_RVA);
    if (lo < 0 || lo > LON_RAW_MAX) return 0;
    if (la < 0 || la > LAT_RAW_MAX) return 0;
    // 둘 다 0 은 "서경180 북위90"(날짜변경선 위의 북극)이라 게임에서 나올 수 없다.
    // 아직 항해를 시작하지 않아 값이 비어 있는 상태로 본다.
    if (lo == 0 && la == 0) return 0;
    *lon = lo; *lat = la;
    return 1;
}

static void PosText(int lon, int lat, wchar_t* out)
{
    // 0.1도 단위로 계산한다(부동소수 없이).
    int lonT = (lon - LON_RAW_MAX / 2) * 1800 / (LON_RAW_MAX / 2);
    int latT = (LAT_RAW_MAX / 2 - lat) * 900 / (LAT_RAW_MAX / 2);
    const wchar_t* ns = latT >= 0 ? L"북위" : L"남위";
    const wchar_t* ew = lonT >= 0 ? L"동경" : L"서경";
    if (latT < 0) latT = -latT;
    if (lonT < 0) lonT = -lonT;
    wsprintfW(out, L"%s %d.%d°   %s %d.%d°", ns, latT / 10, latT % 10, ew, lonT / 10, lonT % 10);
}

// 셀 좌표를 화면 좌표로. 보이는 영역 밖이면 0.
static int CellToScreen(int cellX, int cellY, int* sx, int* sy)
{
    if (cellX < g_vx || cellX >= g_vx + ViewW()) return 0;
    if (cellY < g_vy || cellY >= g_vy + ViewH()) return 0;
    *sx = MAP_X + (cellX - g_vx) * MAP_W / ViewW();
    *sy = MAP_Y + (cellY - g_vy) * MAP_H / ViewH();
    return 1;
}

// 바다 위에서도 읽히도록 어두운 그림자를 한 픽셀 깔고 밝은 글씨를 얹는다.
static void ShadowText(HDC dc, int x, int y, const wchar_t* s)
{
    RECT r;
    r.left = x + 1; r.top = y + 1; r.right = x + 200; r.bottom = y + 16;
    UI_Text(dc, r, s, g_smallFont, RGB(30, 22, 16), DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
    OffsetRect(&r, -1, -1);
    UI_Text(dc, r, s, g_smallFont, RGB(245, 238, 220), DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);
}

// 도시 표시 — 밝은 테두리 안의 어두운 점. 좌표를 모르는 도시는 건너뛴다.
// 이름은 어느 정도 확대해야 붙인다(전체보기에서 200개 넘게 다 쓰면 글씨가 서로 덮는다).
static void DrawCities(HDC dc)
{
    int i, sx, sy;
    int labels = (kZoom[g_zi] >= 3);

    for (i = 0; i < CITYDB_MAX; i++) {
        const CityPt* c = CityDb_At(i);
        RECT r;
        if (c->lonRaw == CITYDB_NONE) continue;
        // 함대 마커와 같은 단위/같은 식으로 찍는다.
        if (!CellToScreen((int)((long long)c->lonRaw * WORLD_UNFOLD_W / LON_RAW_MAX),
                          (int)((long long)c->latRaw * WORLD_CELL_H  / LAT_RAW_MAX), &sx, &sy)) continue;

        // 도서관이 있는 도시는 한 칸 크게, 초록으로 찍는다. 색만 다르게 하면 3px 짜리
        // 점에서는 갈색과 잘 안 갈려서 크기도 같이 바꾼다.
        // 함대 표시(빨강)와는 어느 쪽도 헷갈리지 않는다.
        { int rad = c->lib ? 3 : 2;
          r.left = sx - rad; r.top = sy - rad; r.right = sx + rad + 1; r.bottom = sy + rad + 1; }
        { HBRUSH b = CreateSolidBrush(RGB(245, 238, 220)); FillRect(dc, &r, b); DeleteObject(b); }
        InflateRect(&r, -1, -1);
        { HBRUSH b = CreateSolidBrush(c->lib ? RGB(25, 95, 55) : RGB(60, 45, 35));
          FillRect(dc, &r, b); DeleteObject(b); }

        if (labels && c->name[0]) ShadowText(dc, sx + (c->lib ? 6 : 5), sy - 7, c->name);
    }
}

// 지도 위 함대 표시 — 빨간 동그라미 + 십자선. 보이는 영역 밖이면 그리지 않는다.
static void DrawMarker(HDC dc, int lon, int lat)
{
    int px, py;
    HPEN pen, old;
    HBRUSH ob;

    if (!CellToScreen((int)((long long)lon * WORLD_UNFOLD_W / LON_RAW_MAX),
                      (int)((long long)lat * WORLD_CELL_H  / LAT_RAW_MAX), &px, &py)) return;

    pen = CreatePen(PS_SOLID, 1, RGB(210, 40, 40));
    old = (HPEN)SelectObject(dc, pen);
    ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));

    MoveToEx(dc, px - 10, py, NULL); LineTo(dc, px - 4, py);
    MoveToEx(dc, px + 5,  py, NULL); LineTo(dc, px + 11, py);
    MoveToEx(dc, px, py - 10, NULL); LineTo(dc, px, py - 4);
    MoveToEx(dc, px, py + 5,  NULL); LineTo(dc, px, py + 11);
    Ellipse(dc, px - 4, py - 4, px + 5, py + 5);

    SelectObject(dc, ob);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc, mr, ir;
    wchar_t buf[96];
    UiBuf ub; HDC dc;

    GetClientRect(h, &rc);
    // 메모리에 다 그린 뒤 한 번에 옮긴다. 끌어서 움직일 때 깜빡이지 않도록.
    dc = UI_BufBegin(&ub, hdc, rc.right, rc.bottom);
    UI_WindowFrame(dc, rc, L"세계지도", &g_closeRect);

    mr.left = MAP_X; mr.top = MAP_Y; mr.right = MAP_X + MAP_W; mr.bottom = MAP_Y + MAP_H;
    if (World_Pixels()) {
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = World_W();
        bi.bmiHeader.biHeight = -World_H();     // 음수 = 위에서 아래로
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 24;
        bi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, MAP_X, MAP_Y, World_W(), World_H(), 0, 0, 0, World_H(),
                          World_Pixels(), &bi, DIB_RGB_COLORS);
    } else {
        HBRUSH br = CreateSolidBrush(COL_DISP_BG);
        FillRect(dc, &mr, br); DeleteObject(br);
        UI_Text(dc, mr, L"게임 폴더에서 WORLD.CDS 를 읽지 못했습니다.",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    { HBRUSH br = CreateSolidBrush(COL_DARK); FrameRect(dc, &mr, br); DeleteObject(br); }

    if (World_Pixels()) {
        if (g_cities) DrawCities(dc);
        if (g_lon >= 0) DrawMarker(dc, g_lon, g_lat);   // 함대는 도시 위에 얹는다
    }

    ir.left = MAP_X; ir.right = MAP_X + MAP_W;
    ir.top = MAP_Y + MAP_H + 2; ir.bottom = ir.top + INFO_H - 4;
    if (g_lon >= 0) PosText(g_lon, g_lat, buf);
    else            lstrcpyW(buf, L"항해 중이 아닙니다(좌표를 읽지 못했습니다).");
    UI_Text(dc, ir, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    wsprintfW(buf, L"x%d · 도시 %d%s (초록=도서관) · 휠 확대 / 끌기 이동 / 우클릭 전체 / C 도시 / R 다시읽기",
              kZoom[g_zi], CityDb_Marked(), CityDb_FromFile() ? L"" : L"(내장)");
    UI_Text(dc, ir, buf, g_smallFont, COL_TEXT, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

    UI_BufEnd(&ub);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK MapProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        g_zi = ZOOM_START; g_drag = 0;
        CityDb_Load(g_hinst);
        if (!ReadPos(&g_lon, &g_lat)) { g_lon = -1; g_lat = -1; }
        // 확대한 채로 여니 어디를 보여줄지가 중요하다. 함대 자리를 가운데 놓고,
        // 좌표를 못 읽었으면(항해 전) 지도 한복판을 보여준다.
        if (g_lon >= 0)
            CenterOn((int)((long long)g_lon * WORLD_UNFOLD_W / LON_RAW_MAX),
                     (int)((long long)g_lat * WORLD_CELL_H  / LAT_RAW_MAX));
        else
            CenterOn(WORLD_UNFOLD_W / 2, WORLD_CELL_H / 2);
        if (World_Load()) World_RenderView(MAP_W, MAP_H, g_vx, g_vy, ViewW(), ViewH());
        SetTimer(h, TIMER_ID, TIMER_MS, NULL);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_TIMER: {
        // 위치가 바뀐 프레임에만 다시 그린다. 가만히 있는데 0.5초마다 지도를 다시 찍으면
        // 게임 화면 위에서 깜빡인다.
        int lo = -1, la = -1;
        if (!ReadPos(&lo, &la)) { lo = -1; la = -1; }
        if (lo != g_lon || la != g_lat) { g_lon = lo; g_lat = la; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // 휠의 lParam 은 화면 좌표라 클라이언트 좌표로 옮겨야 커서 밑을 기준으로 확대된다.
        POINT pt; pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        ScreenToClient(h, &pt);
        if (World_Pixels()) ZoomAt(h, GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1, pt);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; RECT mr;
        pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);
        SetFocus(h);
        if (PtInRect(&g_closeRect, pt)) { DestroyWindow(h); return 0; }
        mr.left = MAP_X; mr.top = MAP_Y; mr.right = MAP_X + MAP_W; mr.bottom = MAP_Y + MAP_H;
        if (World_Pixels() && PtInRect(&mr, pt)) {   // 지도를 끌어서 옮긴다
            g_drag = 1; g_dragPt = pt; g_dragVx = g_vx; g_dragVy = g_vy;
            SetCapture(h);
            return 0;
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_drag) {
            int dx = GET_X_LPARAM(lp) - g_dragPt.x;
            int dy = GET_Y_LPARAM(lp) - g_dragPt.y;
            g_vx = g_dragVx - dx * ViewW() / MAP_W;
            g_vy = g_dragVy - dy * ViewH() / MAP_H;
            Redraw(h);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_drag) { g_drag = 0; ReleaseCapture(); }
        return 0;
    case WM_RBUTTONDOWN:                     // 전체보기로 되돌리기
        if (World_Pixels() && g_zi != 0) { g_zi = 0; g_vx = 0; g_vy = 0; Redraw(h); }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == 'C') { g_cities = !g_cities; InvalidateRect(h, NULL, FALSE); return 0; }
        // cities.json 을 고치고 창을 닫았다 열 것 없이 여기서 바로 다시 읽는다.
        if (wp == 'R') { CityDb_Load(g_hinst); InvalidateRect(h, NULL, FALSE); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_drag) { g_drag = 0; ReleaseCapture(); }
        KillTimer(h, TIMER_ID);
        World_Free();
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void MapWin_Show(HWND owner, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT orc;

    g_hinst = hinst;
    if (g_wnd) { SetForegroundWindow(g_wnd); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = MapProc; wc.hInstance = hinst; wc.lpszClassName = WC_MAP;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc); reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - WM_W) / 2;
        y = orc.top  + ((orc.bottom - orc.top) - WM_H) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    g_wnd = CreateWindowExW(0, WC_MAP, L"세계지도", WS_POPUP, x, y, WM_W, WM_H, owner, NULL, hinst, NULL);
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd); SetFocus(g_wnd); }
}
