#include <windows.h>
#include "save.h"
#include "modmenu.h"   // common/ — 모드 창 등록부(걷어 간 항목을 여기서 본다)

// SaveUtilKR — 어디서든 저장 · 중단.
//
// 게임 코드에서 확정한 자리(cds_95.exe, 로드 베이스 0x400000):
//
//   0x004A2800  저장  — 확인창("데이터를 겹쳐 쓰겠습니다. 좋습니까?") 을 띄우고
//                       [예] 면 0x00478E80 (SAVEDATA.CDS 쓰기) 을 부른 뒤
//                       "데이터를 겹쳐 썼습니다" 를 띄운다.
//   0x004A27D0  중단  — 확인창("이 시점에서 데이터를 저장하고 게임을 중단하겠습니다.")
//                       을 띄우고 [예] 면 0x004791D0 (SAVEDATA.TMP 쓰기) 뒤
//                       0x0044AF70 으로 넘어가 게임을 끝낸다.
//
// 둘 다 인자 없는 cdecl 이다. 자택·여관 메뉴 핸들러(0x004A28A0)가 이 둘을 부를 뿐이라,
// "자택·여관에서만" 은 메뉴에 붙은 조건이지 저장 함수의 조건이 아니다.
//
// 부르는 자리 — 게임 창의 WM_COMMAND 에서 부른다. 그 자리는 게임이 제 메시지 펌프에서
// 꺼내 주는 곳이라 프레임 사이의 안전한 지점이다(키보드 훅 안에서 바로 부르면 게임이
// 그림을 그리는 도중일 수 있어 확인창이 겹친다).
//
// ★ 원래 게임은 자택·여관에서만 저장한다. 항해 중에 저장한 데이터를 불러오는 길은
//   게임이 검증한 적 없는 길이다 — 도시에 정박했을 때 쓰기를 권한다. 창에도 그렇게 적었다.

#define ID_SAVE_NOW   0xBE00u    // Trade=0xB10x, Char=0xB301/0xB310+, Ship=0xB410, Patch=0xB500,
#define ID_SAVE_QUIT  0xBE01u    // Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900,
                                 // Fatigue=0xBA00, Hotkey=0xBB00, Hint=0xBC00, Market=0xBD00 과 안 겹치게.

#define RVA_SAVE   0x000A2800u   // 저장(확인창 포함)
#define RVA_QUIT   0x000A27D0u   // 중단(확인창 + 저장 + 종료)

// 그 자리가 정말 그 함수인지 앞머리 바이트로 확인한다. 다른 빌드에 엉뚱한 call 을 놓으면
// 게임이 그 자리에서 죽는다 — 안 맞으면 아무것도 안 하는 편이 낫다.
//   0x4A2800: 68 B8 8C 56 00  6A 02  E8 ...   (push 0x568CB8; push 2; call 확인창)
//   0x4A27D0: 68 80 8C 56 00  6A 02  E8 ...   (push 0x568C80; push 2; call 확인창)
static const unsigned char kSaveSig[] = { 0x68, 0xB8, 0x8C, 0x56, 0x00, 0x6A, 0x02, 0xE8 };
static const unsigned char kQuitSig[] = { 0x68, 0x80, 0x8C, 0x56, 0x00, 0x6A, 0x02, 0xE8 };

static HINSTANCE g_hinst = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

static unsigned char* Base(void)
{
    static unsigned char* b = NULL;
    if (!b) b = (unsigned char*)GetModuleHandleW(NULL);
    return b;
}

static int Readable(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

// 앞머리가 맞으면 그 함수 주소를, 아니면 NULL 을 돌려준다.
static void* GameFn(unsigned rva, const unsigned char* sig, unsigned n)
{
    unsigned char* p;
    unsigned i;
    if (!Base()) return NULL;
    p = Base() + rva;
    if (!Readable(p, n)) return NULL;
    for (i = 0; i < n; i++) if (p[i] != sig[i]) return NULL;
    return p;
}

// 게임의 저장 · 중단을 그대로 부른다. 확인창도 결과 메시지도 게임 것이 그대로 나온다.
static void CallGame(unsigned rva, const unsigned char* sig, unsigned n, const wchar_t* what)
{
    typedef void (__cdecl *VoidFn)(void);
    VoidFn fn = (VoidFn)GameFn(rva, sig, n);
    wchar_t buf[160];
    if (!fn) {
        wsprintfW(buf, L"[SaveUtilKR] %s: 함수 앞머리가 안 맞아 부르지 않았습니다 (RVA 0x%X).", what, rva);
        LogW(buf);
        MessageBoxW(g_gameHwnd,
                    L"이 게임 실행 파일에서는 저장 함수를 찾지 못했습니다.\n"
                    L"자택이나 여관에서 저장해 주세요.",
                    L"저장", MB_OK | MB_ICONWARNING);
        return;
    }
    wsprintfW(buf, L"[SaveUtilKR] %s 호출 0x%p", what, (void*)fn);
    LogW(buf);
    fn();
}

// ------------------------------------------------------------------ 메뉴 붙이기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0) {
        if (LOWORD(w) == ID_SAVE_NOW) {
            CallGame(RVA_SAVE, kSaveSig, sizeof(kSaveSig), L"저장");
            return 0;
        }
        if (LOWORD(w) == ID_SAVE_QUIT) {
            CallGame(RVA_QUIT, kQuitSig, sizeof(kQuitSig), L"중단");
            return 0;
        }
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
    LogW(L"[SaveUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_SAVE_NOW) && !ModMenu_HasId(g_gameHwnd, ID_SAVE_NOW)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_SAVE_NOW,  L"저장");
                AppendMenuW(target, MF_STRING, ID_SAVE_QUIT, L"중단");
                DrawMenuBar(g_gameHwnd);
                LogW(L"[SaveUtilKR] \"저장\" · \"중단\" 메뉴 설치.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
            }
        }
        Sleep(1000);
    }
}

void SaveUtilKR_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    (void)g_hinst;
    LogW(L"[SaveUtilKR] init.");
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
