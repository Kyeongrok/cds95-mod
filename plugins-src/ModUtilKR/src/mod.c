#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "mod.h"

#define MAX_MODS 64
// "파일 > 플러그인 관리" 커맨드. 게임 창 하나를 여러 플러그인이 같이 서브클래싱하므로 ID 가 겹치면
// 먼저 가로챈 쪽이 대신 열린다(0xB600 을 쓰다가 WorldMapKR 의 지도가 떴다).
// 쓰이는 값: Trade=0xB101/0xB102/0xC0xx, Char=0xB301, Ship=0xB410, Patch=0xB500, Map=0xB600.
#define ID_MOD_OPEN 0xB700u

typedef struct {
    wchar_t file[MAX_PATH];      // 확장자 뺀 플러그인 파일명
    wchar_t desc[256];
    wchar_t dir[MAX_PATH];       // 이 파일이 있는 폴더
    wchar_t author[64];          // plugins\<만든이>\ 에서 왔으면 그 이름. 루트면 빈 값
    int     locked;              // 끄면 안 되는 것(로더 / 이 창 자신)
    int     on;                  // 1 = .plugin / 0 = .plugin.off
} Plug;

// 플러그인 설명. 목록에 파일명만 있으면 무엇인지 알 수 없어서 아는 것만 붙인다.
static const struct { const wchar_t* file; const wchar_t* desc; } kDesc[] = {
    { L"DDrawWrapper",    L"플러그인 로더 + DirectDraw 에뮬레이션. 끄면 나머지가 다 안 뜬다." },
    { L"ModUtilKR",       L"이 창. 어떤 플러그인을 쓸지 고른다." },
    { L"QuestModKR",      L"퀘스트 모드 — mods 폴더의 퀘스트 파일 묶음을 골라 깐다." },
    { L"UpdateUtilKR",    L"업데이트 — GitHub 릴리즈를 받아 깐다. 옛 판으로 되돌릴 수도 있다." },
    { L"HotelUtilKR",     L"여관 숙박 일수를 직접 입력한다." },
    { L"TradeUtilKR",     L"교역 메뉴 — 시세 일람 / 교역품 관리 / 워프." },
    { L"CharacterUtilKR", L"정보 창 — 항해사 찾기 / 퀘스트 / 소지품 / 여급 / 스폰서 / 도감." },
    { L"WorldMapKR",      L"세계지도 — 도시·발견물 마커, 우클릭 워프." },
    { L"ShipSkinKR",      L"함선 스킨 + 성능(등장시기 포함) 편집." },
    { L"PatchUtilKR",     L"patches.json 의 메모리 패치를 켜고 끈다." },
    { L"HotkeyUtilKR",    L"단축키 — 글자 한 개로 위 창들을 연다. hotkeys.json 에 남는다." },
    { L"HintUtilKR",      L"힌트 — 발견물 274개를 분류·가치·상태로 본다." },
    { L"FatigueUtilKR",   L"피로도 — 쌓인 함대 피로도를 원하는 만큼 덜어낸다." },
    { L"MarketUtilKR",    L"매매 — 교역소 사고팔기를 그림·짐칸 막대와 함께 한 화면에서." },
    // 원본 SaveUtil 과 헷갈리기 쉽다 — 파일 메뉴의 "저장 · 중단" 은 이쪽이다.
    { L"SaveUtilKR",      L"저장 · 중단 — 자택·여관까지 안 가고 그 자리에서 저장한다." },
    { L"CDROMUtil",       L"(원본) CD-ROM 접근을 하드디스크로 돌린다." },
    { L"CPUPatch",        L"(원본) CPU 점유율을 낮춘다." },
    { L"MemoryFix",       L"(원본) 게임의 메모리 버그를 고친다." },
    { L"SaveUtil",        L"(원본) 세이브 백업." },
};

static HINSTANCE g_hinst = NULL;
static Plug      g_plugs[MAX_MODS];
static int       g_nplug = 0;
static HWND      g_wnd = NULL, g_list = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;
static int       g_populating = 0;

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

static void PluginDir(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
}

static void JoinPath(wchar_t* out, const wchar_t* a, const wchar_t* b)
{
    lstrcpyW(out, a);
    if (out[0] && out[lstrlenW(out) - 1] != L'\\') lstrcatW(out, L"\\");
    lstrcatW(out, b);
}

// CDS95Util 폴더의 *.plugin / *.plugin.off 를 훑는다. 이 폴더가 곧 목록이다.
// 폴더 하나를 훑어 목록에 잇는다. author 가 비면 CDS95Util 루트다.
static void ScanDir(const wchar_t* dir, const wchar_t* author)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int pass;

    for (pass = 0; pass < 2; pass++) {                 // 0 = 켜진 것, 1 = 꺼진 것
        JoinPath(pat, dir, pass ? L"*.plugin.off" : L"*.plugin");
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            Plug* p;
            wchar_t base[MAX_PATH];
            int k, len;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (g_nplug >= MAX_MODS) break;
            lstrcpynW(base, fd.cFileName, MAX_PATH);
            len = lstrlenW(base);
            // "*.plugin" 검색은 "*.plugin.old-..." 같은 것도 걸리므로 끝을 직접 확인한다.
            if (pass == 0) {
                if (len < 8 || lstrcmpiW(base + len - 7, L".plugin") != 0) continue;
                base[len - 7] = 0;
            } else {
                if (len < 12 || lstrcmpiW(base + len - 11, L".plugin.off") != 0) continue;
                base[len - 11] = 0;
            }
            p = &g_plugs[g_nplug++];
            ZeroMemory(p, sizeof(*p));
            lstrcpynW(p->file, base, MAX_PATH);
            lstrcpynW(p->dir, dir, MAX_PATH);
            if (author) lstrcpynW(p->author, author, 64);
            p->on = (pass == 0);
            p->locked = (lstrcmpiW(base, L"DDrawWrapper") == 0 || lstrcmpiW(base, L"ModUtilKR") == 0);
            for (k = 0; k < (int)(sizeof(kDesc)/sizeof(kDesc[0])); k++)
                if (lstrcmpiW(base, kDesc[k].file) == 0) { lstrcpynW(p->desc, kDesc[k].desc, 256); break; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// CDS95Util 루트 + plugins\<만든이>\ 를 모두 훑는다.
//
// 로더(ddraw.dll)는 루트의 *.plugin 만 불러온다 — 우리 소스가 아니라 고칠 수 없다.
// 그래서 하위 폴더 것은 이 창이 LoadLibrary 로 직접 불러온다(LoadSubPlugins).
// 루트에도 그대로 둘 수 있으니 둘 다 목록에 낸다.
static void Scan(void)
{
    wchar_t dir[MAX_PATH], sub[MAX_PATH], pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    g_nplug = 0;
    PluginDir(dir, MAX_PATH);
    ScanDir(dir, NULL);

    JoinPath(sub, dir, L"plugins");
    JoinPath(pat, sub, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t one[MAX_PATH];
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            JoinPath(one, sub, fd.cFileName);
            ScanDir(one, fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    LogW(L"[ModUtilKR] 플러그인 %d개", g_nplug);
}

// 켜기/끄기 = 확장자 바꾸기. 다음 실행부터 반영된다.
static int Toggle(Plug* p, int on)
{
    wchar_t a[MAX_PATH], b[MAX_PATH];
    JoinPath(a, p->dir, p->file); lstrcatW(a, L".plugin");
    lstrcpyW(b, a);            lstrcatW(b, L".off");
    if (p->locked) {
        wchar_t msg[512];
        wsprintfW(msg, L"[%s]\n\n이건 끌 수 없습니다.\n\n%s", p->file, p->desc);
        MessageBoxW(g_wnd, msg, L"ModUtilKR", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (!MoveFileW(on ? b : a, on ? a : b)) {
        wchar_t msg[512];
        wsprintfW(msg, L"[%s]\n\n파일 이름을 바꾸지 못했습니다.\n다른 프로그램이 잡고 있는지 확인하세요.", p->file);
        MessageBoxW(g_wnd, msg, L"ModUtilKR", MB_OK | MB_ICONWARNING);
        return 0;
    }
    p->on = on;
    LogW(L"[ModUtilKR] %s %s (다음 실행부터)", p->file, on ? L"켬" : L"끔");
    return 1;
}

// plugins\<만든이>\*.plugin 을 직접 불러온다.
//
// 로더(ddraw.dll)는 CDS95Util 루트만 훑는다. 그 파일은 2019년 원본이라 고칠 길이 없어서,
// 하위 폴더에 둔 것은 이 창이 대신 불러온다. 플러그인은 그냥 DLL 이고 할 일을 DllMain 에서
// 하므로 LoadLibrary 만 하면 루트에 둔 것과 똑같이 돈다.
// 이미 올라온 이름은 건너뛴다 — 루트와 하위 폴더에 같은 것이 있으면 메뉴가 두 개 붙는다.
static void LoadSubPlugins(void)
{
    wchar_t dir[MAX_PATH], sub[MAX_PATH], pat[MAX_PATH], one[MAX_PATH], file[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;

    PluginDir(dir, MAX_PATH);
    JoinPath(sub, dir, L"plugins");
    JoinPath(pat, sub, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        WIN32_FIND_DATAW f2;
        HANDLE h2;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        JoinPath(one, sub, fd.cFileName);
        JoinPath(pat, one, L"*.plugin");
        h2 = FindFirstFileW(pat, &f2);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do {
            int len = lstrlenW(f2.cFileName);
            if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (len < 8 || lstrcmpiW(f2.cFileName + len - 7, L".plugin") != 0) continue;
            if (GetModuleHandleW(f2.cFileName)) {
                LogW(L"[ModUtilKR] %s 는 이미 올라와 있어 건너뜀", f2.cFileName);
                continue;
            }
            JoinPath(file, one, f2.cFileName);
            // 경로 구분자는 역슬래시 두 개다 — `\%` 는 escape 로 접혀 `%` 가 되면서
            // 폴더와 파일명이 구분 없이 붙어 나왔다(C4129).
            if (LoadLibraryW(file)) { n++; LogW(L"[ModUtilKR] %s\\%s 불러옴", fd.cFileName, f2.cFileName); }
            else LogW(L"[ModUtilKR] %s\\%s 못 불러옴 (오류 %lu)", fd.cFileName, f2.cFileName, GetLastError());
        } while (FindNextFileW(h2, &f2));
        FindClose(h2);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (n) LogW(L"[ModUtilKR] 하위 폴더 플러그인 %d개 불러옴", n);
}

// ------------------------------------------------------------------ 창
#define WC_MOD    L"ModUtilKR_Window"
#define ID_LIST   1001
#define ID_RELOAD 1002
#define ID_FOLDER 1003

static void StateText(const Plug* p, wchar_t* out)
{
    if (p->locked) lstrcpyW(out, L"항상 켬");
    else           lstrcpyW(out, p->on ? L"켬 (다음 실행)" : L"끔 (다음 실행)");
}

static void FillList(void)
{
    int i;
    g_populating = 1;
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_nplug; i++) {
        Plug* p = &g_plugs[i];
        LVITEMW it;
        wchar_t st[32];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.pszText = p->file; it.lParam = i;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        StateText(p, st);
        { LVITEMW s; s.iSubItem = 1; s.pszText = p->author[0] ? p->author : L"(루트)";
          SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        { LVITEMW s; s.iSubItem = 2; s.pszText = st;      SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        { LVITEMW s; s.iSubItem = 3; s.pszText = p->desc; SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        ListView_SetCheckState(g_list, i, p->on);
    }
    g_populating = 0;
}

static LRESULT CALLBACK ModProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        const wchar_t* titles[4] = { L"플러그인", L"만든이", L"상태", L"설명" };
        int widths[4] = { 170, 110, 120, 380 };
        LVCOLUMNW c;
        int i;
        g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                    0, 0, 10, 10, h, (HMENU)ID_LIST, g_hinst, NULL);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 4; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; SendMessageW(g_list, LVM_INSERTCOLUMNW, i, (LPARAM)&c); }
        CreateWindowExW(0, L"BUTTON", L"다시 읽기", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
        CreateWindowExW(0, L"BUTTON", L"폴더 열기", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_FOLDER, g_hinst, NULL);
        Scan();
        FillList();
        return 0;
    }
    case WM_SIZE: {
        int cw = LOWORD(l), ch = HIWORD(l), bh = 30;
        MoveWindow(g_list, 0, 0, cw, ch - bh, TRUE);
        MoveWindow(GetDlgItem(h, ID_RELOAD), 6, ch-bh+3, 110, 24, TRUE);
        MoveWindow(GetDlgItem(h, ID_FOLDER), 122, ch-bh+3, 110, 24, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_RELOAD) { Scan(); FillList(); }
        else if (LOWORD(w) == ID_FOLDER) {
            wchar_t dir[MAX_PATH], sub[MAX_PATH];
            PluginDir(dir, MAX_PATH);
            JoinPath(sub, dir, L"plugins");
            CreateDirectoryW(sub, NULL);      // 만든이별로 넣는 자리를 보여 준다
            ShellExecuteW(h, L"open", sub, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)l;
        if (nh->idFrom == ID_LIST && nh->code == LVN_ITEMCHANGED && !g_populating) {
            NMLISTVIEW* nm = (NMLISTVIEW*)l;
            if (nm->uChanged & LVIF_STATE) {
                BOOL was = ((nm->uOldState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                BOOL is  = ((nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                if (was != is && nm->iItem >= 0 && nm->iItem < g_nplug) {
                    Plug* p = &g_plugs[nm->iItem];
                    wchar_t st[32];
                    Toggle(p, is ? 1 : 0);
                    StateText(p, st);
                    { LVITEMW s; s.iSubItem = 2; s.pszText = st; SendMessageW(g_list, LVM_SETITEMTEXTW, nm->iItem, (LPARAM)&s); }
                    // 이름 바꾸기가 실패했을 수 있으므로 체크를 실제 상태로 맞춘다.
                    g_populating = 1;
                    ListView_SetCheckState(g_list, nm->iItem, p->on);
                    g_populating = 0;
                }
            }
        }
        return 0;
    }
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY: g_wnd = NULL; g_list = NULL; return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void ShowModWindow(void)
{
    static BOOL reg = FALSE;
    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = ModProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_MOD;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        g_wnd = CreateWindowExW(0, WC_MOD, L"플러그인 관리 — 쓸 플러그인 고르기 (다음 실행부터 반영)",
                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 420,
                    NULL, NULL, g_hinst, NULL);
    } else {
        Scan();
        FillList();
    }
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); SetForegroundWindow(g_wnd); }
}

// ------------------------------------------------------------------ 메뉴 붙이기
static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && LOWORD(w) == ID_MOD_OPEN) { ShowModWindow(); return 0; }
    if (m == WM_DESTROY && h == g_subHwnd) {
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
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
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0]==L'파' && s[1]==L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

// "파일 > 모드" 서브메뉴를 찾거나 만든다.
//
// 플러그인 관리 · 퀘스트 모드 · 패치가 각자 파일 메뉴에 항목을 달면 목록이 너무 길어진다.
// 셋을 이 하나 아래로 모은다. 서로를 모르는 별개 DLL 이라 먼저 뜬 쪽이 만들고 나머지는
// 찾아 붙는다. 겹쳐 생긴 빈 "모드" 는 보이는 대로 치운다(동시에 만들면 둘이 될 수 있다).
static HMENU FindOrCreateModMenu(HMENU fileMenu, BOOL mayCreate)
{
    int i;
    WCHAR s[64];
    HMENU first = NULL, sub;
    if (!fileMenu) return NULL;
    for (i = GetMenuItemCount(fileMenu) - 1; i >= 0; i--) {
        if (GetMenuStringW(fileMenu, (UINT)i, s, 64, MF_BYPOSITION) <= 0) continue;
        if (lstrcmpW(s, L"모드") != 0) continue;
        sub = GetSubMenu(fileMenu, (UINT)i);
        if (first && sub && GetMenuItemCount(sub) == 0) { RemoveMenu(fileMenu, (UINT)i, MF_BYPOSITION); continue; }
        first = sub;
    }
    if (first || !mayCreate) return first;
    sub = CreatePopupMenu();
    if (!sub) return NULL;
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)sub, L"모드");
    return sub;
}

// 이 메뉴(하위 메뉴까지)에 우리 항목이 이미 있나.
// 항목을 "모드" 서브메뉴로 옮긴 뒤로 파일 메뉴만 훑으면 늘 "없다" 가 나와서, 1초마다 또
// 달아 메뉴가 끝없이 늘어났다. 그래서 아래로 내려가며 본다.
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

static DWORD WINAPI MenuThread(LPVOID p)
{
    (void)p;
    LogW(L"[ModUtilKR] menu thread started.");
    for (;;) {
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd) {
            HMENU bar = GetMenu(g_gameHwnd);
            if (bar) {
                HMENU fileMenu = FindFileMenu(bar);
                HMENU target = fileMenu ? fileMenu : bar;
                if (!MenuHasId(target, ID_MOD_OPEN)) {
                    {   // 파일 메뉴가 아니라 "모드" 아래에 붙인다
                    HMENU modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, TRUE);
                    AppendMenuW(modMenu ? modMenu : target, MF_STRING, ID_MOD_OPEN, L"플러그인 관리");
                }
                    DrawMenuBar(g_gameHwnd);
                    LogW(L"[ModUtilKR] \"플러그인 관리\" 메뉴 설치.");
                }
                if (g_subHwnd != g_gameHwnd) {
                    g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                    g_subHwnd = g_gameHwnd;
                }
            }
        }
        Sleep(1000);
    }
}

void ModKR_Init(HINSTANCE hinst)
{
    INITCOMMONCONTROLSEX ic;
    HANDLE t;
    g_hinst = hinst;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&ic);
    LogW(L"[ModUtilKR] init.");
    LoadSubPlugins();          // plugins\<만든이>\ 것을 대신 불러온다(로더가 루트만 보므로)
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
