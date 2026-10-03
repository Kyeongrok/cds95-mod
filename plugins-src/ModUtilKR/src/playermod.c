#include <windows.h>
#include <windowsx.h>
#include "playermod.h"
#include "uikit.h"       // CharacterUtilKR/src — 세피아 판과 위젯을 그대로 나눠 쓴다
#include "gameskin.h"    // ButtonMakerKR/src — 단추를 게임 띠로 갈아 끼운다
#include "inventory.h"   // CharacterUtilKR/src — Inv_Money / Inv_SetMoney
#include "livechar.h"    // CharacterUtilKR/src — Player_Fame / Player_AddFame

// 자리와 까닭은 playermod.h 에 적어 뒀다. 여기는 창만 그린다.

#define WC_PLAYERMOD  L"ModUtilKR_PlayerWindow"

// 한 줄에 두 단위씩 — 바깥이 큰 걸음, 안쪽이 잔걸음이다.
#define STEP_N    2
static const int kMoneyStep[STEP_N] = { 100000, 10000 };
static const int kFameStep [STEP_N] = {  10000,   500 };

// 줄 배치 — 라벨 / [큰 빼기][잔 빼기] / 지금 값 / [잔 더하기][큰 더하기]
#define ROW_H     GAMESKIN_H            // 띠의 제 높이(24). 이보다 높이면 헐렁해 보인다
#define LBL_X     (FRAME + 14)
#define LBL_W     70
#define BTN_W     104
#define VAL_W     140
#define GAP       8
#define CELL_N    5
#define CELL_VAL  2                     // 가운데가 지금 값
#define COL0_X    (LBL_X + LBL_W + 8)
#define COL1_X    (COL0_X + BTN_W + GAP)
#define COL2_X    (COL1_X + BTN_W + GAP)
#define COL3_X    (COL2_X + VAL_W + GAP)
#define COL4_X    (COL3_X + BTN_W + GAP)
#define CLIENT_W  (COL4_X + BTN_W + 14 + FRAME)

#define HEAD_Y    (FRAME + TITLE_H + 10)    // 이름 요약 한 줄
#define ROW1_Y    (HEAD_Y + 28)             // 소지금
#define ROW2_Y    (ROW1_Y + ROW_H + 10)     // 명성

// 특기 격자 — 기능 13종 / 언어 14종을 묶음 둘로 나눠 3열 5행씩 늘어놓는다.
// 칸마다 이름 + [0][1][2][3], 지금 레벨이 눌린 모양이다(CharacterUtilKR 항해사 탭과 같은 꼴).
#define SK_COLS    3
#define SK_ROWS    5
#define SK_ITEM_H  20
#define SK_ITEM_W  ((CLIENT_W - 2 * LBL_X) / SK_COLS)
#define SK_NAME_W  110
#define SK_LV_W    24
#define SK_LBL_H   20                       // 묶음 머리("기능" / "언어")
#define SK_SEC_H   (SK_LBL_H + SK_ROWS * SK_ITEM_H + 8)
#define SK_SEC_N   2
#define SK_GEN_N   13                       // id 1~13 기능, 14~27 언어
#define SK_Y0      (ROW2_Y + ROW_H + 12)
typedef char PlayerSkillGridFits[(SK_COLS * SK_ROWS >= LIVECHAR_SKILL_N - SK_GEN_N) ? 1 : -1];

#define MSG_Y     (SK_Y0 + SK_SEC_N * SK_SEC_H + 2)   // 알림 한 줄
#define CLIENT_H  (MSG_Y + 22 + FRAME + 6)

#define ROW_N     2
#define ROW_MONEY 0
#define ROW_FAME  1

// 순서는 특기 id 그대로다(CharacterUtilKR/src/savedata.c 의 kSkillName 과 같은 줄).
static const wchar_t* kSkillName[LIVECHAR_SKILL_N + 1] = {
    L"",
    L"항해술", L"운용술", L"검술", L"포술", L"사격술", L"의학", L"웅변술",
    L"측량술", L"역사학", L"회계", L"조선술", L"신학", L"과학",
    L"스페인어", L"포르투갈어", L"로망스어", L"게르만어", L"슬라브어",
    L"아랍어", L"페르시아어", L"중국어", L"힌두어", L"위그르어",
    L"아프리카어", L"아메리카어", L"동남아시아어", L"동아시아어",
};

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL, g_gameHwnd = NULL;
static wchar_t   g_msg[160] = L"";

static int SkillSec(int id) { return id > SK_GEN_N; }
static int SecTop(int sec)  { return SK_Y0 + sec * SK_SEC_H; }

// 특기 한 칸(이름 + 레벨 넷). 묶음 안에서는 위에서 아래로 채우고 다음 열로 넘어간다.
static RECT RcSkill(int id)
{
    int sec = SkillSec(id), i = id - 1 - sec * SK_GEN_N;
    RECT r;
    r.left   = LBL_X + (i / SK_ROWS) * SK_ITEM_W;
    r.right  = r.left + SK_ITEM_W;
    r.top    = SecTop(sec) + SK_LBL_H + (i % SK_ROWS) * SK_ITEM_H;
    r.bottom = r.top + SK_ITEM_H;
    return r;
}

static RECT RcSkillLv(int id, int lv)
{
    RECT it = RcSkill(id), b;
    b.left = it.left + SK_NAME_W + 6 + lv * SK_LV_W;
    b.right = b.left + SK_LV_W - 2;
    b.top = it.top + 1; b.bottom = it.bottom - 1;
    return b;
}

static int RowY(int row) { return row == ROW_MONEY ? ROW1_Y : ROW2_Y; }

// col 0 큰 빼기 · 1 잔 빼기 · 2 지금 값 · 3 잔 더하기 · 4 큰 더하기
static RECT RcCell(int row, int col)
{
    static const int kx[CELL_N] = { COL0_X, COL1_X, COL2_X, COL3_X, COL4_X };
    RECT r;
    r.left  = kx[col];
    r.right = r.left + ((col == CELL_VAL) ? VAL_W : BTN_W);
    r.top    = RowY(row);
    r.bottom = r.top + ROW_H;
    return r;
}

// 그 칸이 값에 얼마를 더하는가. 값 칸이면 0.
static int CellDelta(int row, int col)
{
    const int* step = (row == ROW_MONEY) ? kMoneyStep : kFameStep;
    switch (col) {
    case 0: return -step[0];
    case 1: return -step[1];
    case 3: return +step[1];
    case 4: return +step[0];
    default: return 0;
    }
}

// 1234567 -> "1,234,567". 세 자리마다 끊는다.
static void Comma(int v, wchar_t* out)
{
    wchar_t tmp[24];
    int neg = (v < 0), n, i, k = 0;
    unsigned u = (unsigned)(neg ? -v : v);
    n = 0;
    do { tmp[n++] = (wchar_t)(L'0' + u % 10); u /= 10; } while (u);
    if (neg) out[k++] = L'-';
    for (i = n - 1; i >= 0; i--) {
        out[k++] = tmp[i];
        if (i && (i % 3) == 0) out[k++] = L',';
    }
    out[k] = 0;
}

// 값을 못 읽었으면 -1. 세이브를 안 불러왔을 때다.
static int RowValue(int row)
{
    return (row == ROW_MONEY) ? Inv_Money() : Player_Fame();
}

static void RowLabel(int row, wchar_t* out)
{
    lstrcpyW(out, row == ROW_MONEY ? L"소지금" : L"명성");
}

static void CellText(int row, int col, wchar_t* out)
{
    int d = CellDelta(row, col);
    wchar_t num[24];
    Comma(d < 0 ? -d : d, num);
    wsprintfW(out, d < 0 ? L"-%s" : L"+%s", num);
}

static int Live(void) { return Inv_Money() >= 0 || Player_Fame() >= 0; }

// 한 줄을 그만큼 더한다(음수면 뺀다). 결과를 알림 줄에 적는다.
static void Bump(int row, int delta)
{
    wchar_t num[24];
    if (row == ROW_MONEY) {
        int v = Inv_Money(), nv;
        if (v < 0) { lstrcpyW(g_msg, L"소지금을 읽지 못했습니다 — 세이브를 불러온 뒤에 열어 주세요."); return; }
        nv = v + delta;
        if (nv < 0) nv = 0;
        if (nv > INV_MONEY_MAX) nv = INV_MONEY_MAX;
        if (!Inv_SetMoney(nv)) { lstrcpyW(g_msg, L"소지금을 고치지 못했습니다"); return; }
        Comma(nv, num);
        wsprintfW(g_msg, L"소지금 %s닢 이 되었습니다", num);
    } else {
        int v = Player_AddFame(delta);
        if (v < 0) { lstrcpyW(g_msg, L"명성을 고치지 못했습니다 — 세이브를 불러온 뒤에 열어 주세요."); return; }
        Comma(v, num);
        wsprintfW(g_msg, L"명성 %s 이 되었습니다", num);
    }
}

static void Paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    UiBuf b;
    HDC dc;
    RECT rc, cb, r;
    HBRUSH br;
    int row, col;
    wchar_t buf[160], name[64];

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&b, hdc, rc.right, rc.bottom);
    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    UI_WindowFrame(dc, rc, L"플레이어 수정", &cb);

    // 이름 요약 — 지금 누구를 고치는지 보이게.
    name[0] = 0;
    Player_Name(name, 64);
    if (name[0]) {
        int age = Player_Age();
        if (age > -9999) wsprintfW(buf, L"%s · %d세", name, age);
        else             lstrcpyW(buf, name);
    } else {
        lstrcpyW(buf, L"세이브를 불러오면 주인공이 나옵니다");
    }
    r.left = LBL_X; r.right = rc.right - FRAME - 10;
    r.top = HEAD_Y; r.bottom = HEAD_Y + 20;
    UI_Text(dc, r, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    for (row = 0; row < ROW_N; row++) {
        int v = RowValue(row);
        RECT vr;

        r.left = LBL_X; r.right = LBL_X + LBL_W;
        r.top = RowY(row); r.bottom = r.top + ROW_H;
        RowLabel(row, buf);
        UI_Text(dc, r, buf, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        for (col = 0; col < CELL_N; col++) {
            if (col == CELL_VAL) continue;
            CellText(row, col, buf);
            UI_Button(dc, RcCell(row, col), buf, FALSE);
        }

        // 가운데는 지금 값 — 누르는 칸이 아니라 눌린 상자로 그린다.
        vr = RcCell(row, CELL_VAL);
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &vr, br); DeleteObject(br);
        UI_Bevel(dc, vr, TRUE);
        if (v >= 0) Comma(v, buf); else lstrcpyW(buf, L"—");
        vr.right -= 8;
        UI_Text(dc, vr, buf, g_font, v >= 0 ? COL_TEXT : COL_DARK,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    // 특기 — 묶음 머리 둘, 그 아래 칸마다 이름과 레벨 넷.
    for (row = 0; row < SK_SEC_N; row++) {
        r.left = LBL_X; r.right = rc.right - FRAME - 10;
        r.top = SecTop(row); r.bottom = r.top + SK_LBL_H;
        UI_Text(dc, r, row == 0 ? L"기능" : L"언어", g_font, COL_TEXT,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    for (row = 1; row <= LIVECHAR_SKILL_N; row++) {
        int lv = Player_Skill(row);
        r = RcSkill(row);
        r.left += 4; r.right = r.left + SK_NAME_W;
        UI_Text(dc, r, kSkillName[row], g_smallFont,
                lv > 0 ? (SkillSec(row) ? COL_LANG_TX : COL_TEXT) : COL_DARK,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        for (col = 0; col <= LIVECHAR_SKILL_MAX; col++) {
            RECT lb = RcSkillLv(row, col);
            br = CreateSolidBrush(col == lv ? COL_SEL_BG : COL_DISP_BG); FillRect(dc, &lb, br); DeleteObject(br);
            UI_Bevel(dc, lb, col == lv);
            wsprintfW(buf, L"%d", col);
            UI_Text(dc, lb, buf, g_smallFont, col == lv ? RGB(250,244,228) : COL_TEXT,
                    DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
    }

    r.left = LBL_X; r.right = rc.right - FRAME - 10;
    r.top = MSG_Y; r.bottom = MSG_Y + 20;
    UI_Text(dc, r,
            g_msg[0] ? g_msg
                     : (Live() ? L"실행 중인 게임에 바로 들어갑니다. 게임에서 저장하면 남습니다."
                               : L"세이브를 불러온 뒤에 열어 주세요 — 아직 값이 없습니다."),
            g_smallFont, g_msg[0] ? COL_WARN_TX : COL_DARK,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK PlayerProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        UI_SetButtonDraw(GameSkin_Button);   // 게임 껍데기를 못 읽으면 저절로 기본 모양이 된다
        Inv_Load();
        Player_Load();
        SetTimer(h, 1, 700, NULL);           // 게임에서 값이 바뀌어도 따라간다
        return 0;
    case WM_TIMER:
        // 창을 열어 둔 채 세이브를 불러오면 그때 자리가 생긴다 — 아직이면 계속 두드린다.
        if (!Inv_Ready())    Inv_Load();
        if (!Player_Ready()) Player_Load();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(h); return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb, r;
        int row;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        GetClientRect(h, &rc);
        cb.right = rc.right - FRAME - 4; cb.left = cb.right - 22;
        cb.top = FRAME + 4; cb.bottom = cb.top + 18;
        if (PtInRect(&cb, pt)) { ShowWindow(h, SW_HIDE); return 0; }

        for (row = 0; row < ROW_N; row++) {
            int col;
            for (col = 0; col < CELL_N; col++) {
                if (col == CELL_VAL) continue;
                r = RcCell(row, col);
                if (!PtInRect(&r, pt)) continue;
                Bump(row, CellDelta(row, col));
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
        }
        for (row = 1; row <= LIVECHAR_SKILL_N; row++) {
            int lv;
            for (lv = 0; lv <= LIVECHAR_SKILL_MAX; lv++) {
                r = RcSkillLv(row, lv);
                if (!PtInRect(&r, pt)) continue;
                if (Player_SetSkill(row, lv)) wsprintfW(g_msg, L"%s 레벨 %d 이 되었습니다", kSkillName[row], lv);
                else lstrcpyW(g_msg, L"특기를 고치지 못했습니다 — 세이브를 불러온 뒤에 열어 주세요.");
                InvalidateRect(h, NULL, FALSE);
                return 0;
            }
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { ShowWindow(h, SW_HIDE); return 0; }
        if (w == VK_F5) { Inv_Load(); Player_Load(); g_msg[0] = 0; InvalidateRect(h, NULL, FALSE); return 0; }
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void PlayerMod_Show(HINSTANCE hinst, HWND gameHwnd)
{
    static BOOL reg = FALSE;
    RECT orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;

    g_hinst = hinst;
    g_gameHwnd = gameHwnd;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = PlayerProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszClassName = WC_PLAYERMOD;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        // 게임이 전체화면이라 게임 창을 주인으로 걸어야 위에 뜬다.
        if (g_gameHwnd && GetWindowRect(g_gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - CLIENT_W) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - CLIENT_H) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_PLAYERMOD, L"플레이어 수정", WS_POPUP,
                    x, y, CLIENT_W, CLIENT_H, g_gameHwnd, NULL, g_hinst, NULL);
    } else {
        Inv_Load();                 // 그 사이 세이브를 불러왔을 수 있다
        Player_Load();
        g_msg[0] = 0;
        InvalidateRect(g_wnd, NULL, FALSE);
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
        SetForegroundWindow(g_wnd);
        SetFocus(g_wnd);
    }
}
