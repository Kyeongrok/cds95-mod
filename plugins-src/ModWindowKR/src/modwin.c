#include <windows.h>
#include "modwin.h"
#include "uikit.h"     // CharacterUtilKR/src — 세피아 색표와 위젯
#include "gameskin.h"  // ButtonMakerKR/src — 단추를 게임 껍데기(MISC.CDS 파트 4)로 그린다

// 플러그인이 스무 개를 넘으면서 "파일 > 모드" 가 세로로 한없이 길어졌다. 그래서 메뉴를
// 펼치는 대신 창을 띄우고 그 안에 단추를 늘어놓는다 — 다른 KR 창들과 같은 껍데기다.
//
// 단추가 하는 일은 **게임 창에 WM_COMMAND 를 보내는 것뿐**이다. 메뉴 항목의 ID 를 그대로
// 보내므로 각 플러그인의 서브클래스 프로시저가 평소처럼 받는다. 우리가 아는 것은 ID 와
// 이름뿐이고 무엇을 하는 항목인지는 알 필요가 없다 — 그래서 앞으로 무엇이 새로 붙어도
// 저절로 단추가 하나 는다.

#define WC_MODWIN   L"CDS95_ModWindowKR"
#define MAX_ITEMS   48
#define BTN_W       164
#define BTN_H       GAMESKIN_H       // 게임 띠의 제 높이(24)
#define BTN_GAP     3
#define COL_GAP     14               // 두 무리 사이
#define PAD         12
#define TITLE_BAR   28
#define HEAD_H      20               // 무리 이름이 앉는 자리
#define FOOT_GAP    14               // 열과 아래 가로줄 사이

// 바탕은 게임 것과 같은 짙은 자주갈색(#311818)이다 — MarketUtilKR 의 흥정 판이 오리지널
// 화면에서 집어낸 그 색과 같다. 어두운 바탕이라 글자는 크림색으로 얹는다.
#define COL_WIN_BG    RGB(0x31, 0x18, 0x18)
#define COL_HEAD_TX   RGB(226, 214, 189)

// 항목을 네 무리로 갈라 왼쪽부터 세운다.
//   일반 — 배를 몰면서 여는 것(정보·지도·매매·저장 …)
//   편집 — 게임 속을 고치는 것(패치·대사·퀘스트 …)
//   도구 — 만들고 들여다보는 것(버튼 만들기·도시그림·단축키 …)
//   개발 — 판을 대놓고 주무르는 것(플레이어 수정 …)
//   기능 — 창을 여는 것이 아니라 그 자리에서 일이 끝나는 것(저장·중단). 열로 세우지 않고
//          아래에 가로줄 하나로 따로 앉힌다 — 성격이 달라 섞이면 눈에 안 들어온다.
//
// 편집과 도구를 가른 잣대는 "판이 바뀌는가" 다. 편집 쪽은 눌러 고치면 게임이 달라지고,
// 도구 쪽은 만들거나 보기만 한다. 개발은 그중에서도 놀이의 앞뒤를 건너뛰는 것들이라
// 따로 세워 둔다 — 손이 미끄러져 눌리는 자리에 있으면 곤란하다.
#define GRP_PLAY    0
#define GRP_EDIT    1
#define GRP_TOOL    2
#define GRP_DEV     3
#define GRP_FUNC    4
#define GRP_N       5
#define GRP_COLS    4              // 세로 열로 세우는 무리 수. 나머지(기능)는 아래 가로줄이다

static const wchar_t* kGroupName[GRP_N] = { L"일반", L"편집", L"도구", L"개발", L"기능" };

// 값은 common/modmenu.h 의 ID 표를 본다.
static const UINT kPlayIds[] = {
    0xB101u,   // 교역시세
    0xB102u,   // 교역품
    0xB103u,   // 워프
    0xB301u,   // 정보
    0xB600u,   // 지도
    0xBA00u,   // 피로도
    0xBC00u,   // 힌트
    0xBD00u,   // 매매
    0xC200u,   // 기능수련
    0xC300u,   // 서적
    0xC400u,   // 선체
    0xC700u,   // 풍향 화살표
};

static const UINT kToolIds[] = {
    0xB700u,   // 플러그인 관리
    0xBB00u,   // 단축키
    0xBF00u,   // 도시그림
    0xC500u,   // 버튼 만들기
};

static const UINT kDevIds[] = {
    0xB701u,   // 플레이어 수정
};

static const UINT kFuncIds[] = {
    0xBE00u,   // 저장
    0xBE01u,   // 중단
};

// 두 표 어디에도 없으면 편집으로 본다 — 새로 붙는 것은 대개 게임 속을 손보는 쪽이다.

typedef struct {
    UINT    id;       // WM_COMMAND 로 보낼 값. 하위 메뉴가 딸린 항목이면 0
    HMENU   sub;      // 하위 메뉴가 딸린 항목이면 그 메뉴(워프 같은 것)
    BOOL    checked;  // 켜고 끄는 항목(풍향 화살표)은 눌린 모양으로 보인다
    BOOL    gray;
    int     group;
    wchar_t label[64];
} ModItem;

static HWND      g_wnd;
static HWND      g_owner;
static ModItem   g_items[MAX_ITEMS];
static int       g_count, g_rows;
static int       g_start[GRP_N], g_num[GRP_N];   // g_items 안에서 무리별 구간
static int       g_hot = -1;      // 마우스가 얹힌 단추
static int       g_down = -1;     // 누르고 있는 단추
static DWORD     g_shownTick;     // 뜬 시각 — 뜨자마자 닫히는 것을 막는다

int ModWin_IsOpen(void) { return g_wnd != NULL; }

// 이 항목이 어느 무리인가. 하위가 딸린 것은 그 첫 항목의 ID 로 가린다(워프가 그렇다).
static int GroupOf(UINT id, HMENU sub)
{
    int i;
    if (sub && !id) id = GetMenuItemID(sub, 0);
    for (i = 0; i < (int)(sizeof(kPlayIds) / sizeof(kPlayIds[0])); i++)
        if (kPlayIds[i] == id) return GRP_PLAY;
    for (i = 0; i < (int)(sizeof(kToolIds) / sizeof(kToolIds[0])); i++)
        if (kToolIds[i] == id) return GRP_TOOL;
    for (i = 0; i < (int)(sizeof(kDevIds) / sizeof(kDevIds[0])); i++)
        if (kDevIds[i] == id) return GRP_DEV;
    for (i = 0; i < (int)(sizeof(kFuncIds) / sizeof(kFuncIds[0])); i++)
        if (kFuncIds[i] == id) return GRP_FUNC;
    return GRP_EDIT;
}

// 니모닉(&)과 단축키 꼬리(탭 뒤)를 떼어 낸다. 단추에는 글자만 세운다.
static void CleanLabel(const wchar_t* src, wchar_t* dst, int cap)
{
    int i = 0;
    while (*src && i < cap - 1) {
        if (*src == 9) break;              // 탭 — 그 뒤는 단축키 표시다
        if (*src == L'&') { src++; continue; }
        dst[i++] = *src++;
    }
    dst[i] = 0;
}

// 등록부를 훑어 단추 목록을 만든다. 구분선은 건너뛴다.
static void Collect(HMENU m)
{
    int n, i;
    g_count = 0;
    if (!m) return;
    n = GetMenuItemCount(m);
    for (i = 0; i < n && g_count < MAX_ITEMS; i++) {
        UINT state = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        wchar_t raw[128];
        ModItem* it;

        if (state == (UINT)-1 || (state & MF_SEPARATOR)) continue;
        if (GetMenuStringW(m, (UINT)i, raw, 128, MF_BYPOSITION) <= 0) continue;

        it = &g_items[g_count++];
        it->sub = GetSubMenu(m, (UINT)i);
        it->id = it->sub ? 0 : GetMenuItemID(m, (UINT)i);
        it->checked = (state & MF_CHECKED) ? TRUE : FALSE;
        it->gray = (state & (MF_GRAYED | MF_DISABLED)) ? TRUE : FALSE;
        it->group = GroupOf(it->id, it->sub);
        CleanLabel(raw, it->label, 64);
    }

    // 무리끼리 모아 둔다. 같은 무리 안에서는 들어온 차례를 지킨다(제자리 정렬).
    {
        ModItem sorted[MAX_ITEMS];
        int n = 0, g, i;
        for (g = 0; g < GRP_N; g++) {
            g_num[g] = 0;
            g_start[g] = n;
            for (i = 0; i < g_count; i++)
                if (g_items[i].group == g) { sorted[n++] = g_items[i]; g_num[g]++; }
        }
        for (i = 0; i < n; i++) g_items[i] = sorted[i];
        g_count = n;
    }

    {
        int g;
        g_rows = 0;
        for (g = 0; g < GRP_COLS; g++) if (g_num[g] > g_rows) g_rows = g_num[g];
    }
    if (g_rows < 1) g_rows = 1;
}

static int WinW(void) { return PAD * 2 + GRP_COLS * BTN_W + (GRP_COLS - 1) * COL_GAP; }

// 열 아래 가로줄이 시작하는 자리(그 줄의 이름이 앉는 높이).
static int FootY(void)
{
    return TITLE_BAR + PAD + HEAD_H + g_rows * BTN_H + (g_rows - 1) * BTN_GAP + FOOT_GAP;
}

static int WinH(void)
{
    if (g_num[GRP_FUNC] <= 0) return FootY() - FOOT_GAP + PAD;
    return FootY() + HEAD_H + BTN_H + PAD;
}

// 무리 g 의 왼쪽 끝.
static int ColX(int g) { return PAD + g * (BTN_W + COL_GAP); }

static int GroupAt(int k)
{
    int i;
    for (i = 0; i < GRP_N; i++)
        if (k >= g_start[i] && k < g_start[i] + g_num[i]) return i;
    return GRP_N - 1;
}

static RECT BtnRect(int k)
{
    RECT r;
    int g = GroupAt(k);
    int idx = k - g_start[g];

    if (g == GRP_FUNC) {
        // 아래 가로줄 — 왼쪽부터 나란히 눕는다.
        r.left = PAD + idx * (BTN_W + BTN_GAP);
        r.top  = FootY() + HEAD_H;
    } else {
        r.left = ColX(g);
        r.top  = TITLE_BAR + PAD + HEAD_H + idx * (BTN_H + BTN_GAP);
    }
    r.right = r.left + BTN_W;
    r.bottom = r.top + BTN_H;
    return r;
}

static int HitTest(int x, int y)
{
    int k;
    for (k = 0; k < g_count; k++) {
        RECT r = BtnRect(k);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return k;
    }
    return -1;
}

static void Paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc, r;
    UiBuf b;
    HDC dc;
    int k;

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&b, hdc, rc.right, rc.bottom);

    { HBRUSH br = CreateSolidBrush(COL_WIN_BG); FillRect(dc, &rc, br); DeleteObject(br); }
    r = rc; r.bottom = TITLE_BAR;
    GameSkin_Title(dc, r, L"모드");

    for (k = 0; k < GRP_COLS; k++) {
        RECT h2;
        h2.left = ColX(k); h2.right = h2.left + BTN_W;
        h2.top = TITLE_BAR + PAD; h2.bottom = h2.top + HEAD_H;
        UI_Text(dc, h2, kGroupName[k], g_font, COL_HEAD_TX, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_num[GRP_FUNC] > 0) {
        RECT h2;
        h2.left = PAD; h2.right = h2.left + BTN_W;
        h2.top = FootY(); h2.bottom = h2.top + HEAD_H;
        UI_Text(dc, h2, kGroupName[GRP_FUNC], g_font, COL_HEAD_TX, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    for (k = 0; k < g_count; k++) {
        RECT br = BtnRect(k);
        // 켜져 있는 항목과 지금 가리키거나 누르고 있는 것은 도드라지게 그린다.
        UI_Button(dc, br, g_items[k].label, g_items[k].checked || k == g_down || k == g_hot);
    }

    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

// 단추를 눌렀다. 하위 메뉴가 딸린 것은 그 자리에 펼치고, 아니면 게임 창에 그대로 보낸다.
static void Fire(HWND h, int k)
{
    ModItem* it;
    if (k < 0 || k >= g_count) return;
    it = &g_items[k];
    if (it->gray) return;

    if (it->sub) {
        RECT br = BtnRect(k);
        POINT p; UINT cmd;
        p.x = br.right; p.y = br.top;
        ClientToScreen(h, &p);
        // TPM_RETURNCMD 라 고른 ID 를 돌려준다 — 그것만 게임에 넘기면 된다.
        cmd = (UINT)TrackPopupMenu(it->sub, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                   p.x, p.y, 0, h, NULL);
        if (cmd) { PostMessageW(g_owner, WM_COMMAND, (WPARAM)cmd, 0); DestroyWindow(h); }
        return;
    }
    if (it->id) {
        PostMessageW(g_owner, WM_COMMAND, (WPARAM)it->id, 0);
        DestroyWindow(h);
    }
}

static LRESULT CALLBACK ModProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_PAINT: Paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT t;
        int k = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        if (k != g_hot) { g_hot = k; InvalidateRect(h, NULL, FALSE); }
        t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = h; t.dwHoverTime = 0;
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hot != -1) { g_hot = -1; InvalidateRect(h, NULL, FALSE); }
        return 0;
    case WM_LBUTTONDOWN:
        g_down = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int k = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        int was = g_down;
        g_down = -1;
        ReleaseCapture();
        InvalidateRect(h, NULL, FALSE);
        if (k >= 0 && k == was) Fire(h, k);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        break;
    case WM_ACTIVATE:
        // 게임 쪽을 누르면 조용히 닫는다. 메뉴에서 튀어나온 창이니 그편이 자연스럽다.
        // 하위 메뉴를 펼치는 동안에는 활성이 옮겨가지 않으므로 그때 닫히지는 않는다.
        //
        // 다만 뜬 직후 한동안은 무시한다 — 메뉴 루프가 끝나며 포커스가 게임으로 한 번
        // 돌아가는데, 그것까지 "바깥을 눌렀다" 로 받으면 창이 뜨자마자 닫힌다.
        if (LOWORD(wp) == WA_INACTIVE && GetTickCount() - g_shownTick > 400) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_wnd = NULL; g_hot = -1; g_down = -1;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void ModWin_Show(HWND owner, HMENU modMenu, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w, hh;
    RECT orc, want;

    if (g_wnd) { SetForegroundWindow(g_wnd); return; }

    g_owner = owner;
    Collect(modMenu);
    if (g_count <= 0) return;

    UI_CreateFonts();
    // 단추를 게임 껍데기로 그린다. MISC.CDS 를 못 읽으면 0 을 돌려주므로 저절로
    // 세피아 기본 모양으로 물러난다.
    UI_SetButtonDraw(GameSkin_Button);

    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = ModProc; wc.hInstance = hinst; wc.lpszClassName = WC_MODWIN;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc); reg = TRUE;
    }

    // 테두리를 뺀 알맹이가 계산한 크기 그대로가 되게 창 크기를 되짚어 잡는다.
    want.left = 0; want.top = 0; want.right = WinW(); want.bottom = WinH();
    AdjustWindowRectEx(&want, WS_POPUP | WS_BORDER, FALSE, 0);
    w = want.right - want.left;
    hh = want.bottom - want.top;

    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - w) / 2;
        y = orc.top + ((orc.bottom - orc.top) - hh) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }

    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, WC_MODWIN, L"모드",
                            WS_POPUP | WS_BORDER, x, y, w, hh, owner, NULL, hinst, NULL);
    if (g_wnd) {
        g_shownTick = GetTickCount();
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
        SetForegroundWindow(g_wnd);
        SetFocus(g_wnd);
    }
}
