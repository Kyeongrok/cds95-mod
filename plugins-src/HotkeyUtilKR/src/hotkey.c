#include <windows.h>
#include "hotkey.h"
#include "actions.h"

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
                                 // Upd=0xB900, Fatigue=0xBA00 과 안 겹치게.

#define ID_LIST    1101
#define ID_ENABLE  1102
#define ID_RESET   1103
#define ID_STATUS  1104
#define ID_HELP    1105

#define WC_HOTKEY  L"HotkeyUtilKR_Window"
#define CLIENT_W   392
#define CLIENT_H   470
#define LIST_H     318

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL, g_list = NULL;
static HFONT     g_font = NULL;
static WNDPROC   g_listProc = NULL;
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

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다
// (charstate.c · questjson.c 와 같은 관용구 — 만든이별로 폴더를 나눠도 자료는 한 자리다).
static void UpToDataDir(wchar_t* dir)
{
    wchar_t tmp[MAX_PATH];
    int n, i, cut2 = -1, cut1 = -1;
    lstrcpynW(tmp, dir, MAX_PATH);
    n = lstrlenW(tmp);
    if (n && tmp[n-1] == L'\\') tmp[--n] = 0;
    for (i = n - 1; i >= 0; i--) {
        if (tmp[i] != L'\\') continue;
        if (cut2 < 0) cut2 = i;
        else { cut1 = i; break; }
    }
    if (cut1 < 0 || cut2 <= cut1) return;
    tmp[cut2] = 0;
    if (lstrcmpiW(tmp + cut1 + 1, L"plugins") != 0) return;
    tmp[cut1 + 1] = 0;
    lstrcpyW(dir, tmp);
}

static void JsonPath(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"hotkeys.json");
}

// 파일을 통째로 읽어 와이드 문자열로. 없으면 NULL. 부른 쪽이 HeapFree.
static wchar_t* ReadWholeW(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0;
    char* raw; wchar_t* w; int n; const char* p;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 256u * 1024) { CloseHandle(h); return NULL; }
    raw = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!raw) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, raw, sz, &got, NULL)) { HeapFree(GetProcessHeap(), 0, raw); CloseHandle(h); return NULL; }
    raw[got] = 0;
    CloseHandle(h);
    p = raw;
    if (got >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                                   // UTF-8 BOM
    n = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
    w = n > 0 ? (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(wchar_t)) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, p, -1, w, n);
    HeapFree(GetProcessHeap(), 0, raw);
    return w;
}

// p 가 가리키는 곳에서 다음 "따옴표 문자열"을 읽어 out 에 담고, 닫는 따옴표 다음을 돌려준다.
// 문자열이 더 없으면 NULL. 이스케이프는 다루지 않는다 — 기능 이름과 키 이름뿐이라 쓸 일이 없다.
static const wchar_t* NextString(const wchar_t* p, wchar_t* out, int cch)
{
    int i = 0;
    while (*p && *p != L'"') { if (*p == L'}') return NULL; p++; }
    if (!*p) return NULL;
    p++;
    while (*p && *p != L'"') { if (i < cch - 1) out[i++] = *p; p++; }
    out[i] = 0;
    return *p ? p + 1 : NULL;
}

static void LoadDefaults(void)
{
    int i;
    for (i = 0; i < ACT_N; i++) g_key[i] = kActions[i].defKey;
    g_enabled = 1;
}

static void LoadJson(void)
{
    wchar_t path[MAX_PATH], name[64], val[16];
    wchar_t* buf;
    const wchar_t* p;
    int i;

    LoadDefaults();
    g_loaded = 1;
    JsonPath(path, MAX_PATH);
    buf = ReadWholeW(path);
    if (!buf) return;                              // 파일이 없으면 기본값 그대로

    // "Enabled": true / false
    for (p = buf; *p; p++) {
        wchar_t k[16]; int j = 0; const wchar_t* q;
        if (*p != L'"') continue;
        q = p + 1;
        while (*q && *q != L'"' && j < 15) k[j++] = *q++;
        k[j] = 0;
        if (lstrcmpiW(k, L"Enabled")) continue;
        while (*q && *q != L':') q++;
        while (*q == L':' || *q == L' ' || *q == L'\t' || *q == L'\r' || *q == L'\n') q++;
        g_enabled = (*q == L'f' || *q == L'F' || *q == L'0') ? 0 : 1;
        break;
    }

    // "Keys" 다음의 { } 안에서 "이름": "키" 짝을 훑는다.
    for (p = buf; *p; p++) {
        wchar_t k[16]; int j = 0; const wchar_t* q;
        if (*p != L'"') continue;
        q = p + 1;
        while (*q && *q != L'"' && j < 15) k[j++] = *q++;
        k[j] = 0;
        if (!lstrcmpiW(k, L"Keys")) { p = q; break; }
    }
    if (!*p) { HeapFree(GetProcessHeap(), 0, buf); return; }
    while (*p && *p != L'{') p++;
    if (*p) p++;

    for (;;) {
        p = NextString(p, name, 64);
        if (!p) break;
        p = NextString(p, val, 16);
        if (!p) break;
        for (i = 0; i < ACT_N; i++)
            if (!lstrcmpW(kActions[i].name, name)) { g_key[i] = KeyFromName(val); break; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
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

    JsonPath(path, MAX_PATH);
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

static void Refresh(void)
{
    wchar_t line[128], key[16];
    int i, sel;
    if (!g_list) return;
    sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < ACT_N; i++) {
        KeyName(g_key[i], key, 16);
        wsprintfW(line, L"%s\t%s", kActions[i].name, key);
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)(sel >= 0 && sel < ACT_N ? sel : 0), 0);
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void Status(const wchar_t* s)
{
    if (g_wnd) SetDlgItemTextW(g_wnd, ID_STATUS, s);
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
    Refresh();
    KeyName(vk, key, 16);
    if (!vk)          wsprintfW(msg, L"[%s] 단축키를 뗐습니다.", kActions[row].name);
    else if (stolen >= 0) wsprintfW(msg, L"[%s] = %s.  [%s] 에서 가져왔습니다.",
                                    kActions[row].name, key, kActions[stolen].name);
    else              wsprintfW(msg, L"[%s] = %s.", kActions[row].name, key);
    Status(msg);
}

// 목록 창은 글자키를 제 것(맨 앞 글자로 건너뛰기)으로 쓰므로 가로채야 한다.
static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN) {
        int vk = (int)w;
        switch (vk) {
        case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
        case VK_HOME: case VK_END: case VK_TAB: case VK_ESCAPE:
            break;                                   // 목록 이동은 그대로 둔다
        case VK_DELETE: case VK_BACK:
            Bind((int)SendMessageW(h, LB_GETCURSEL, 0, 0), 0);
            return 0;
        default:
            if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) break;
            Bind((int)SendMessageW(h, LB_GETCURSEL, 0, 0), vk);
            return 0;
        }
    }
    if (m == WM_CHAR) return 0;                      // 삑 소리 · 글자 건너뛰기 막기
    return CallWindowProcW(g_listProc, h, m, w, l);
}

static HWND MakeCtl(HWND h, const wchar_t* cls, const wchar_t* text, DWORD style,
                    int x, int y, int cw, int ch, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, cw, ch, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
    if (c && g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK HotkeyProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE: {
        int tabs[1];
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        MakeCtl(h, L"STATIC",
                L"줄을 고르고 키를 누르면 그 키로 바뀝니다. Del 은 떼기.\n"
                L"알파벳 · 숫자 · F1~F12 를 걸 수 있고, Ctrl · Alt 조합은 게임 몫으로 둡니다.",
                0, 12, 10, CLIENT_W - 24, 40, ID_HELP);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_USETABSTOPS,
                    12, 56, CLIENT_W - 24, LIST_H, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
        if (g_list) {
            if (g_font) SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);
            tabs[0] = 120;                            // 이름칸 폭. 대화상자 단위(4 = 글자 한 칸) 라 240px 쯤이다
            SendMessageW(g_list, LB_SETTABSTOPS, 1, (LPARAM)tabs);
            g_listProc = (WNDPROC)SetWindowLongPtrW(g_list, GWLP_WNDPROC, (LONG_PTR)ListProc);
        }
        MakeCtl(h, L"BUTTON", L"단축키 켜기", WS_TABSTOP | BS_AUTOCHECKBOX,
                12, 56 + LIST_H + 12, 120, 24, ID_ENABLE);
        MakeCtl(h, L"BUTTON", L"기본값으로", WS_TABSTOP | BS_PUSHBUTTON,
                CLIENT_W - 12 - 110, 56 + LIST_H + 10, 110, 28, ID_RESET);
        MakeCtl(h, L"STATIC", L"", 0, 12, 56 + LIST_H + 46, CLIENT_W - 24, 40, ID_STATUS);
        CheckDlgButton(h, ID_ENABLE, g_enabled ? BST_CHECKED : BST_UNCHECKED);
        Refresh();
        Status(L"고친 값은 CDS95Util\\hotkeys.json 에 바로 저장됩니다.");
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_COMMAND:
        if (LOWORD(w) == ID_ENABLE) {
            g_enabled = (IsDlgButtonChecked(h, ID_ENABLE) == BST_CHECKED) ? 1 : 0;
            SaveJson();
            Status(g_enabled ? L"단축키를 켰습니다."
                             : L"단축키를 껐습니다. 게임에서 글자를 적을 때 이렇게 둡니다.");
            return 0;
        }
        if (LOWORD(w) == ID_RESET) {
            LoadDefaults();
            SaveJson();
            CheckDlgButton(h, ID_ENABLE, BST_CHECKED);
            Refresh();
            Status(L"기본 단축키로 되돌렸습니다.");
            return 0;
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (g_list && g_listProc) SetWindowLongPtrW(g_list, GWLP_WNDPROC, (LONG_PTR)g_listProc);
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        g_wnd = NULL; g_list = NULL; g_listProc = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowHotkeyWindow(void)
{
    static BOOL reg = FALSE;
    RECT r, orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, ww, wh;

    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = HotkeyProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_HOTKEY;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        ww = r.right - r.left; wh = r.bottom - r.top;
        // 게임이 전체화면이라 게임 창을 주인으로 걸어야 위에 뜬다(FatigueUtilKR 과 같다).
        if (g_gameHwnd && GetWindowRect(g_gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - ww) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - wh) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_HOTKEY, L"단축키 — 줄을 고르고 키를 누르세요",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                    x, y, ww, wh, g_gameHwnd, NULL, g_hinst, NULL);
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
        if (g_list) SetFocus(g_list);
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
    if (!g_loaded) LoadJson();
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
