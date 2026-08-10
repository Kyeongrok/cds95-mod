#include <windows.h>
#include "hotkey.h"
#include "actions.h"
#include "hkjson.h"   // hotkeys.json 읽기 — 인물 창도 같은 것을 쓴다
#include "uikit.h"    // CharacterUtilKR/src — 세피아 색표와 위젯을 그대로 나눠 쓴다
#include <windowsx.h>

// HotkeyUtilKR — 글자 한 개로 KR 플러그인 창을 연다. (hotkey.h 의 설명 참고)
//
// 키를 어떻게 잡나 — 게임 UI 스레드에 WH_KEYBOARD 훅을 건다. 스레드 훅이라 게임 창이든
// 플러그인 창이든 그 스레드가 받는 키는 다 지나간다(창마다 따로 서브클래싱할 것 없이).
// 우리 키였으면 1 을 돌려 게임에는 안 넘긴다.
//
// 안 잡는 자리 —
//   · Ctrl / Alt 가 눌린 조합 (Alt+F 로 메뉴 여는 길을 막지 않는다)
//   · 초점이 글자 입력칸(EDIT/COMBOBOX)에 있을 때 (교역품 창 검색칸 같은 것)
//   · 단축키 창 자신 (거기서 누르는 키는 "이 키로 바꿔라"는 뜻이다)
//   · [단축키 켜기] 를 꺼 뒀을 때 — 게임이 글자키를 받는 자리(이름 적기 같은 것)에서는
//     이걸 끄면 된다. 끈 상태도 hotkeys.json 에 남는다.

#define ID_HOTKEY_OPEN 0xBB00u   // Trade=0xB101/0xB102, Char=0xB301/0xB310+, Ship=0xB410,
                                 // Patch=0xB500, Map=0xB600, Mod=0xB700, QMod=0xB800,
                                 // Upd=0xB900, Fatigue=0xBA00, Hint=0xBC00, Market=0xBD00 과 안 겹치게.

#define WC_HOTKEY  L"HotkeyUtilKR_Window"

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;
static HHOOK     g_hook = NULL;
static DWORD     g_hookTid = 0;

static int  g_key[ACT_N];        // 기능마다 걸린 가상키. 0 이면 안 걸림
static int  g_enabled = 1;
static int  g_loaded = 0;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// ------------------------------------------------------------------ 키 이름

// 걸 수 있는 키만 받는다 — 알파벳 · 숫자 · F1~F12.
static int KeyOk(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return 1;
    if (vk >= '0' && vk <= '9') return 1;
    if (vk >= VK_F1 && vk <= VK_F12) return 1;
    return 0;
}

static void KeyName(int vk, wchar_t* out, int cch)
{
    if (!vk) { lstrcpynW(out, L"—", cch); return; }
    if (vk >= VK_F1 && vk <= VK_F12) wsprintfW(out, L"F%d", vk - VK_F1 + 1);
    else wsprintfW(out, L"%c", vk);
}

// "I" · "7" · "F3" 를 가상키로. 못 알아보면 0.
static int KeyFromName(const wchar_t* s)
{
    if (!s || !s[0]) return 0;
    if ((s[0] == L'F' || s[0] == L'f') && s[1]) {
        int n = 0, i;
        for (i = 1; s[i]; i++) {
            if (s[i] < L'0' || s[i] > L'9') return 0;
            n = n * 10 + (s[i] - L'0');
        }
        return (n >= 1 && n <= 12) ? VK_F1 + n - 1 : 0;
    }
    if (s[1]) return 0;
    if (s[0] >= L'a' && s[0] <= L'z') return (int)(s[0] - L'a' + L'A');
    if ((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'0' && s[0] <= L'9')) return (int)s[0];
    return 0;
}

// ------------------------------------------------------------------ hotkeys.json
// 읽기는 hkjson.c 가 한다(인물 창도 같은 파일을 읽어야 해서 따로 뺐다). 쓰기는 여기서만 한다.

static void LoadDefaults(void)
{
    int i;
    for (i = 0; i < ACT_N; i++) g_key[i] = kActions[i].defKey;
    g_enabled = 1;
}

// 파일이 있으면 그대로, 없으면 기본값. 파일이 없었으면 0 을 돌려준다(부른 쪽이 한 벌 써 둔다 —
// 인물 창이 탭에 키를 적으려면 이 파일이 있어야 한다).
static int LoadJson(void)
{
    wchar_t* buf;
    wchar_t val[16];
    int i;

    LoadDefaults();
    g_loaded = 1;
    buf = HkJson_Read(g_hinst);
    if (!buf) return 0;

    g_enabled = HkJson_Enabled(buf);
    for (i = 0; i < ACT_N; i++)
        if (HkJson_KeyOf(buf, kActions[i].name, val, 16))
            g_key[i] = KeyFromName(val);          // 적혀 있으면 그 값(빈 값이면 떼 놓은 것 = 0)
    HkJson_Free(buf);
    return 1;
}

static void AppendUtf8(char* buf, int cap, int* len, const wchar_t* s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 1 || *len + n >= cap) return;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf + *len, cap - *len, NULL, NULL);
    *len += n - 1;
}

static void SaveJson(void)
{
    wchar_t path[MAX_PATH], line[128], key[16];
    char buf[8192];
    int len = 0, i;
    HANDLE h;
    DWORD wr = 0;

    AppendUtf8(buf, sizeof(buf), &len, L"{\r\n");
    wsprintfW(line, L"  \"Enabled\": %s,\r\n", g_enabled ? L"true" : L"false");
    AppendUtf8(buf, sizeof(buf), &len, line);
    AppendUtf8(buf, sizeof(buf), &len, L"  \"Keys\": {\r\n");
    for (i = 0; i < ACT_N; i++) {
        KeyName(g_key[i], key, 16);
        wsprintfW(line, L"    \"%s\": \"%s\"%s\r\n", kActions[i].name,
                  g_key[i] ? key : L"", i == ACT_N - 1 ? L"" : L",");
        AppendUtf8(buf, sizeof(buf), &len, line);
    }
    AppendUtf8(buf, sizeof(buf), &len, L"  }\r\n}\r\n");

    HkJson_Path(g_hinst, path, MAX_PATH);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { LogW(L"[HotkeyUtilKR] hotkeys.json 을 쓰지 못했습니다."); return; }
    WriteFile(h, buf, (DWORD)len, &wr, NULL);
    CloseHandle(h);
}

// ------------------------------------------------------------------ 키 잡기

static int IsTypingWindow(HWND f)
{
    wchar_t cls[32];
    if (!f) return 0;
    if (!GetClassNameW(f, cls, 32)) return 0;
    // 글자를 적는 칸만 비켜 준다. 목록(ListBox/ListView)의 글자 이동은 검색칸이 따로 있어 안 쓴다.
    return !lstrcmpiW(cls, L"Edit") || !lstrcmpiW(cls, L"ComboBox")
        || !lstrcmpiW(cls, L"RichEdit") || !lstrcmpiW(cls, L"RichEdit20W");
}

// 우리 키였으면 게임 창에 그 기능을 던지고 1. 아니면 0(게임으로 그대로 넘어간다).
static int Fire(int vk)
{
    HWND f;
    int i;
    if (!g_enabled || !g_gameHwnd) return 0;
    if (!KeyOk(vk)) return 0;
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return 0;
    f = GetFocus();
    if (f && (GetAncestor(f, GA_ROOT) == g_wnd || IsTypingWindow(f))) return 0;
    for (i = 0; i < ACT_N; i++) {
        if (g_key[i] != vk) continue;
        PostMessageW(g_gameHwnd, WM_COMMAND, MAKEWPARAM(kActions[i].id, 0), 0);
        return 1;
    }
    return 0;
}

static LRESULT CALLBACK KeyHook(int code, WPARAM wp, LPARAM lp)
{
    // 눌린 순간만 본다 — 뗄 때(bit31)와 눌린 채 반복(bit30)은 흘려보낸다.
    if (code == HC_ACTION && !(lp & 0x80000000) && !(lp & 0x40000000) && Fire((int)wp))
        return 1;
    return CallNextHookEx(NULL, code, wp, lp);
}

// ------------------------------------------------------------------ 창
//
// 게임 위에 뜨는 다른 KR 창들과 같은 세피아 판이다 — CharacterUtilKR 의 uikit.c 를 그대로
// 같이 빌드해 색·글꼴·위젯을 나눠 쓴다. 자식 컨트롤(LISTBOX/BUTTON)은 쓰지 않고 직접
// 그린다(게임 DirectDraw 화면 위에서 자식 컨트롤이 불안정했다 — uikit.h 참고).
// 창 자신이 초점을 들고 있어 누른 키는 WM_KEYDOWN 으로 바로 받는다.

#define HK_ROW_H   22
#define HK_LIST_Y  (FRAME + TITLE_H + 42)
#define HK_LIST_H  (HK_ROW_H * ACT_N)
#define CLIENT_W   360
#define CLIENT_H   (HK_LIST_Y + HK_LIST_H + 78)

static int     g_sel = 0;                 // 고른 줄
static wchar_t g_status[160] = L"";

static RECT RcList(void)
{ RECT r; r.left=FRAME+8; r.right=CLIENT_W-FRAME-8; r.top=HK_LIST_Y; r.bottom=r.top+HK_LIST_H; return r; }
static RECT RcRowAt(int i)
{ RECT r = RcList(); r.top = HK_LIST_Y + i*HK_ROW_H; r.bottom = r.top + HK_ROW_H; return r; }
static RECT RcEnable(void)
{ RECT r; r.left=FRAME+8; r.right=r.left+130; r.top=HK_LIST_Y+HK_LIST_H+12; r.bottom=r.top+24; return r; }
static RECT RcReset(void)
{ RECT r; r.right=CLIENT_W-FRAME-8; r.left=r.right-100; r.top=HK_LIST_Y+HK_LIST_H+12; r.bottom=r.top+24; return r; }

static void Status(const wchar_t* s)
{
    lstrcpynW(g_status, s, 160);
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
}

// 고른 줄에 키를 건다. 다른 줄이 이미 쓰던 키면 그쪽에서 뗀다(한 키에 한 기능).
static void Bind(int row, int vk)
{
    wchar_t msg[160], key[16];
    int i, stolen = -1;
    if (row < 0 || row >= ACT_N) return;
    if (vk && !KeyOk(vk)) { Status(L"알파벳 · 숫자 · F1~F12 만 걸 수 있습니다."); return; }
    if (vk) for (i = 0; i < ACT_N; i++)
        if (i != row && g_key[i] == vk) { g_key[i] = 0; stolen = i; }
    g_key[row] = vk;
    SaveJson();
    KeyName(vk, key, 16);
    if (!vk)              wsprintfW(msg, L"[%s] 단축키를 뗐습니다.", kActions[row].name);
    else if (stolen >= 0) wsprintfW(msg, L"[%s] = %s.  [%s] 에서 가져왔습니다.",
                                    kActions[row].name, key, kActions[stolen].name);
    else                  wsprintfW(msg, L"[%s] = %s.", kActions[row].name, key);
    Status(msg);
}

static void HotkeyPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    UiBuf b;
    RECT rc, cb, r, box;
    HDC dc;
    HBRUSH br;
    int i;
    wchar_t key[16];

    GetClientRect(h, &rc);
    dc = UI_BufBegin(&b, hdc, rc.right, rc.bottom);

    br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
    UI_WindowFrame(dc, rc, L"단축키", &cb);

    r.left = FRAME + 10; r.right = CLIENT_W - FRAME - 10;
    r.top = FRAME + TITLE_H + 4; r.bottom = r.top + 36;
    UI_Text(dc, r, L"줄을 고르고 키를 누르면 그 키로 바뀝니다. Del 은 떼기.\n"
                   L"Ctrl · Alt 조합과 글자 입력칸은 건드리지 않습니다.",
            g_smallFont, COL_TEXT, DT_LEFT|DT_TOP|DT_NOPREFIX|DT_WORDBREAK);

    // 목록 — 눌린 판 위에 줄을 직접 그린다(20줄이 다 들어가 스크롤이 없다)
    box = RcList();
    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &box, br); DeleteObject(br);
    for (i = 0; i < ACT_N; i++) {
        RECT row = RcRowAt(i), t;
        COLORREF tx = COL_TEXT;
        if (i == g_sel)  { br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &row, br); DeleteObject(br); tx = COL_LIGHT; }
        else if (i & 1)  { br = CreateSolidBrush(COL_ROW_ALT); FillRect(dc, &row, br); DeleteObject(br); }
        t = row; t.left += 10; t.right = t.left + 190;
        UI_Text(dc, t, kActions[i].name, g_font, tx, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        KeyName(g_key[i], key, 16);
        t = row; t.right -= 14; t.left = t.right - 60;
        UI_Text(dc, t, key, g_font, tx, DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    UI_Bevel(dc, box, TRUE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br);

    // [단축키 켜기] — 체크박스 컨트롤 대신 눌린 네모에 표시를 찍는다
    {
        RECT e = RcEnable(), sq, t;
        sq.left = e.left; sq.right = sq.left + 16;
        sq.top = (e.top + e.bottom) / 2 - 8; sq.bottom = sq.top + 16;
        br = CreateSolidBrush(g_enabled ? COL_SEL_BG : COL_LIGHT); FillRect(dc, &sq, br); DeleteObject(br);
        UI_Bevel(dc, sq, TRUE);
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &sq, br); DeleteObject(br);
        if (g_enabled) UI_Text(dc, sq, L"V", g_font, COL_LIGHT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        t = e; t.left = sq.right + 8;
        UI_Text(dc, t, L"단축키 켜기", g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    UI_Button(dc, RcReset(), L"기본값으로", FALSE);

    r.left = FRAME + 10; r.right = CLIENT_W - FRAME - 10;
    r.top = HK_LIST_Y + HK_LIST_H + 42; r.bottom = r.top + 26;
    UI_Text(dc, r, g_status[0] ? g_status : L"고친 값은 CDS95Util\\hotkeys.json 에 바로 저장됩니다.",
            g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);

    UI_BufEnd(&b);
    EndPaint(h, &ps);
}

static LRESULT CALLBACK HotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        UI_CreateFonts();
        g_status[0] = 0;
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: HotkeyPaint(h); return 0;
    case WM_LBUTTONDOWN:
    {
        POINT pt; RECT rc, cb, r;
        pt.x = GET_X_LPARAM(l); pt.y = GET_Y_LPARAM(l);
        GetClientRect(h, &rc);
        cb.right = rc.right - FRAME - 4; cb.left = cb.right - 22;
        cb.top = FRAME + 4; cb.bottom = cb.top + 18;
        if (PtInRect(&cb, pt)) { ShowWindow(h, SW_HIDE); return 0; }
        r = RcEnable();
        if (PtInRect(&r, pt)) {
            g_enabled = !g_enabled;
            SaveJson();
            Status(g_enabled ? L"단축키를 켰습니다."
                             : L"단축키를 껐습니다. 게임에서 글자를 적을 때 이렇게 둡니다.");
            return 0;
        }
        r = RcReset();
        if (PtInRect(&r, pt)) {
            LoadDefaults();
            SaveJson();
            Status(L"기본 단축키로 되돌렸습니다.");
            return 0;
        }
        r = RcList();
        if (PtInRect(&r, pt)) {
            int i = (pt.y - HK_LIST_Y) / HK_ROW_H;
            if (i >= 0 && i < ACT_N) { g_sel = i; InvalidateRect(h, NULL, FALSE); }
            return 0;
        }
        if (pt.y < FRAME + TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        switch (w) {
        case VK_UP:     if (g_sel > 0) { g_sel--; InvalidateRect(h, NULL, FALSE); } return 0;
        case VK_DOWN:   if (g_sel < ACT_N - 1) { g_sel++; InvalidateRect(h, NULL, FALSE); } return 0;
        case VK_HOME:   g_sel = 0; InvalidateRect(h, NULL, FALSE); return 0;
        case VK_END:    g_sel = ACT_N - 1; InvalidateRect(h, NULL, FALSE); return 0;
        case VK_ESCAPE: ShowWindow(h, SW_HIDE); return 0;
        case VK_DELETE: case VK_BACK: Bind(g_sel, 0); return 0;
        default:
            // Ctrl · Alt 조합은 걸지 않는다 — 그런 키는 게임 몫으로 둔다.
            if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) break;
            Bind(g_sel, (int)w);
            return 0;
        }
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY:
        UI_DestroyFonts();
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowHotkeyWindow(void)
{
    static BOOL reg = FALSE;
    RECT orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = HotkeyProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = NULL;
            wc.lpszClassName = WC_HOTKEY;
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
        g_wnd = CreateWindowExW(0, WC_HOTKEY, L"단축키", WS_POPUP,
                    x, y, CLIENT_W, CLIENT_H, g_gameHwnd, NULL, g_hinst, NULL);
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        UpdateWindow(g_wnd);
        SetForegroundWindow(g_wnd);
        SetFocus(g_wnd);          // 누른 키를 창이 직접 받는다
    }
}

// ------------------------------------------------------------------ 메뉴 붙이기 · 훅 걸기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_HOTKEY_OPEN) {
        ShowHotkeyWindow();
        return 0;
    }
    if (m == WM_NCDESTROY && h == g_subHwnd) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0;
    (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}

static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i;
    WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

static BOOL MenuHasId(HMENU m, UINT id)
{
    int n, i;
    if (!m) return FALSE;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        if (sub) { if (MenuHasId(sub, id)) return TRUE; continue; }
        if (GetMenuItemID(m, (UINT)i) == id) return TRUE;
    }
    return FALSE;
}

static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}

static DWORD WINAPI MenuThread(LPVOID p)
{
    (void)p;
    LogW(L"[HotkeyUtilKR] menu monitor started.");
    if (!g_loaded && !LoadJson()) SaveJson();   // 없으면 기본값으로 한 벌 써 둔다 —
    // 인물 창이 탭에 "스폰서(P)" 를 적으려면 이 파일을 읽어야 한다.
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_HOTKEY_OPEN)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_HOTKEY_OPEN, L"단축키");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[HotkeyUtilKR] \"단축키\" 메뉴 설치.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
            }
            // 게임 UI 스레드에 키보드 훅을 건다. 창을 다시 만들면 스레드가 바뀔 수 있어 확인한다.
            {
                DWORD tid = GetWindowThreadProcessId(g_gameHwnd, NULL);
                if (tid && tid != g_hookTid) {
                    if (g_hook) UnhookWindowsHookEx(g_hook);
                    g_hook = SetWindowsHookExW(WH_KEYBOARD, KeyHook, g_hinst, tid);
                    g_hookTid = g_hook ? tid : 0;
                    LogW(g_hook ? L"[HotkeyUtilKR] 키보드 훅 설치."
                                : L"[HotkeyUtilKR] 키보드 훅 실패 — 단축키가 안 먹습니다.");
                }
            }
        }
        Sleep(1000);
    }
}

void HotkeyKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    LogW(L"[HotkeyUtilKR] init.");
    // hotkeys.json 은 여기서 읽지 않는다 — DllMain 안에서 파일을 건드리지 않으려고
    // 아래 스레드로 미룬다(다른 KR 플러그인과 같은 방식). 훅도 그 뒤에 걸린다.
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
