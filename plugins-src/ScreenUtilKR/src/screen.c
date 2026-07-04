// ScreenUtilKR — 인프로세스 캡처 + 입력 주입 (w5/w7)
//
// 캡처 원리: 이 게임의 DirectDraw 래퍼(ddraw.dll/DDrawWrapper.plugin)는 GDI 기반이라
//   프레임버퍼 DIB를 StretchDIBits/StretchBlt/BitBlt 로 게임 창에 present 한다.
//   게임은 "비활성"이면 렌더를 멈춰 창이 흰색이 되지만, present 되던 프레임은 실제 화면이다.
//   → 그 present blit 을 MinHook 으로 훅해 "마지막 프레임"을 g_frame 버퍼에 복사.
//   capture 명령은 g_frame(마지막 실제 프레임)을 BMP로 덤프 → 포커스 무관 캡처.
//
// 입력: 게임 창 메시지큐에 PostMessage(WM_*BUTTON*/MOUSEMOVE) — client 좌표.
// 제어: %TEMP%\cds_ctrl_cmd.txt 명령 → 실행 → %TEMP%\cds_ctrl_ack.txt 결과.

#include <windows.h>
#include <stdio.h>
#include <MinHook.h>

// ---------------- 마지막 프레임 버퍼 ----------------
static CRITICAL_SECTION g_lock;
static BYTE* g_frame = NULL;     // top-down 24bpp
static int   g_fw = 0, g_fh = 0;
static int   g_frameCount = 0;   // present 훅이 몇 번 잡혔는지(진단)

static HWND  g_game = NULL;
static BOOL CALLBACK enumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { *(HWND*)l = h; return FALSE; }
    return TRUE;
}
static HWND FindGame(void)
{
    HWND h;
    if (g_game && IsWindow(g_game)) return g_game;   // 캐시 (GetActiveWindow 훅 핫패스)
    h = NULL; EnumWindows(enumProc, (LPARAM)&h);
    if (h) g_game = h;
    return h ? h : g_game;
}

// 프레임 버퍼에 저장 (top-down 24bpp로 통일)
static void StoreFrame(const void* srcBits, const BITMAPINFO* bmi)
{
    int w, h, srcTopDown, bpp, srcStride, dstStride, y;
    const BYTE* s; BYTE* d;
    if (!srcBits || !bmi) return;
    w = bmi->bmiHeader.biWidth;
    h = bmi->bmiHeader.biHeight;
    bpp = bmi->bmiHeader.biBitCount;
    srcTopDown = (h < 0);
    if (h < 0) h = -h;
    if (w < 64 || h < 64 || w > 4096 || h > 4096) return;   // 프레임버퍼로 보이는 것만
    if (bpp != 24 && bpp != 32) return;                      // 팔레트/16bpp는 이번 PoC 제외
    EnterCriticalSection(&g_lock);
    if (g_fw != w || g_fh != h || !g_frame)
    {
        if (g_frame) HeapFree(GetProcessHeap(), 0, g_frame);
        g_frame = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h * 3);
        g_fw = w; g_fh = h;
    }
    if (g_frame)
    {
        srcStride = ((w * (bpp / 8)) + 3) & ~3;
        dstStride = w * 3;
        for (y = 0; y < h; y++)
        {
            const BYTE* srow = (const BYTE*)srcBits + (srcTopDown ? y : (h - 1 - y)) * srcStride;
            BYTE* drow = g_frame + y * dstStride;   // g_frame 은 top-down
            if (bpp == 24) { memcpy(drow, srow, dstStride); }
            else { int x; for (x = 0; x < w; x++) { drow[x*3]=srow[x*4]; drow[x*3+1]=srow[x*4+1]; drow[x*3+2]=srow[x*4+2]; } }
        }
        g_frameCount++;
    }
    LeaveCriticalSection(&g_lock);
}

// present blit 이 게임 창을 향하는지(=화면 present) 판정
static BOOL IsGamePresent(HDC hdcDest)
{
    HWND w = WindowFromDC(hdcDest);
    return w && (w == FindGame());
}

// ---- 훅: StretchDIBits (DIB → 창) ----
typedef int (WINAPI *StretchDIBits_t)(HDC,int,int,int,int,int,int,int,int,const void*,const BITMAPINFO*,UINT,DWORD);
static StretchDIBits_t oStretchDIBits = NULL;
static int WINAPI hStretchDIBits(HDC hdc,int xd,int yd,int dw,int dh,int xs,int ys,int sw,int sh,const void* bits,const BITMAPINFO* bmi,UINT usage,DWORD rop)
{
    int r = oStretchDIBits(hdc,xd,yd,dw,dh,xs,ys,sw,sh,bits,bmi,usage,rop);
    if (bits && bmi && IsGamePresent(hdc)) StoreFrame(bits, bmi);
    return r;
}

// ---- 훅: StretchBlt / BitBlt (메모리DC → 창) ----
static void GrabFromSrcDC(HDC hdcSrc, int w, int h)
{
    HBITMAP hbm; BITMAPINFO bmi; BYTE* buf; int stride;
    if (w < 64 || h < 64) return;
    hbm = (HBITMAP)GetCurrentObject(hdcSrc, OBJ_BITMAP);
    if (!hbm) return;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w; bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 24; bmi.bmiHeader.biCompression = BI_RGB;
    stride = ((w * 3) + 3) & ~3;
    buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)stride * h);
    if (buf && GetDIBits(hdcSrc, hbm, 0, h, buf, &bmi, DIB_RGB_COLORS))
        StoreFrame(buf, &bmi);
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
}
typedef BOOL (WINAPI *StretchBlt_t)(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);
static StretchBlt_t oStretchBlt = NULL;
static BOOL WINAPI hStretchBlt(HDC hd,int xd,int yd,int dw,int dh,HDC hs,int xs,int ys,int sw,int sh,DWORD rop)
{
    BOOL r = oStretchBlt(hd,xd,yd,dw,dh,hs,xs,ys,sw,sh,rop);
    if (IsGamePresent(hd)) GrabFromSrcDC(hs, sw, sh);
    return r;
}
typedef BOOL (WINAPI *BitBlt_t)(HDC,int,int,int,int,HDC,int,int,DWORD);
static BitBlt_t oBitBlt = NULL;
static BOOL WINAPI hBitBlt(HDC hd,int xd,int yd,int w,int h,HDC hs,int xs,int ys,DWORD rop)
{
    BOOL r = oBitBlt(hd,xd,yd,w,h,hs,xs,ys,rop);
    if (IsGamePresent(hd)) GrabFromSrcDC(hs, w, h);
    return r;
}

static void InstallCaptureHooks(void)
{
    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    if (!gdi) return;
    if (MH_CreateHook(GetProcAddress(gdi,"StretchDIBits"), &hStretchDIBits, (void**)&oStretchDIBits) == MH_OK) MH_EnableHook(GetProcAddress(gdi,"StretchDIBits"));
    if (MH_CreateHook(GetProcAddress(gdi,"StretchBlt"),    &hStretchBlt,    (void**)&oStretchBlt)    == MH_OK) MH_EnableHook(GetProcAddress(gdi,"StretchBlt"));
    if (MH_CreateHook(GetProcAddress(gdi,"BitBlt"),        &hBitBlt,        (void**)&oBitBlt)        == MH_OK) MH_EnableHook(GetProcAddress(gdi,"BitBlt"));
}

// ---- 버퍼된 마지막 프레임을 24bpp BMP로 저장 ----
static BOOL SaveLastFrame(const wchar_t* path)
{
    BITMAPINFOHEADER bi; BITMAPFILEHEADER fh; int w, h, stride, imgSize, y; BYTE* out; HANDLE f; DWORD wr; BOOL ok = FALSE;
    EnterCriticalSection(&g_lock);
    if (!g_frame || g_fw <= 0 || g_fh <= 0) { LeaveCriticalSection(&g_lock); return FALSE; }
    w = g_fw; h = g_fh;
    stride = (w * 3 + 3) & ~3; imgSize = stride * h;
    out = (BYTE*)HeapAlloc(GetProcessHeap(), 0, imgSize);
    if (out)
    {
        for (y = 0; y < h; y++)   // BMP 는 bottom-up; g_frame 은 top-down → 뒤집기
            memcpy(out + (h - 1 - y) * stride, g_frame + y * (w * 3), w * 3);
    }
    LeaveCriticalSection(&g_lock);
    if (!out) return FALSE;
    ZeroMemory(&bi, sizeof(bi));
    bi.biSize = sizeof(bi); bi.biWidth = w; bi.biHeight = h; bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    ZeroMemory(&fh, sizeof(fh));
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(bi); fh.bfSize = fh.bfOffBits + imgSize;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        WriteFile(f, &fh, sizeof(fh), &wr, NULL);
        WriteFile(f, &bi, sizeof(bi), &wr, NULL);
        WriteFile(f, out, imgSize, &wr, NULL);
        CloseHandle(f); ok = TRUE;
    }
    HeapFree(GetProcessHeap(), 0, out);
    return ok;
}

// ---------------- 입력 주입 (폴링 훅) ----------------
// 게임은 GetCursorPos(스크린) + GetAsyncKeyState/GetKeyState(버튼)를 폴링하고, GetActiveWindow로
// 활성 여부를 검사(비활성이면 입력 무시 + 렌더 중단→흰화면). 그래서:
//   · GetActiveWindow → 항상 게임창 반환 (항상 활성으로 인식: 렌더 유지 + 입력 처리)
//   · GetCursorPos/GetAsyncKeyState/GetKeyState → 클릭 주입 중엔 가짜 좌표/버튼 반환
static volatile LONG g_injX = 0, g_injY = 0;    // 주입 커서(스크린 좌표)
static volatile DWORD g_cursorUntil = 0;        // 이 tick 까지 가짜 커서 반환
static volatile DWORD g_btnUntil = 0;           // 이 tick 까지 좌버튼 눌림 반환

typedef HWND (WINAPI *GetActiveWindow_t)(void);
static GetActiveWindow_t oGetActiveWindow = NULL;
static HWND WINAPI hGetActiveWindow(void) { HWND g = FindGame(); return g ? g : oGetActiveWindow(); }

typedef BOOL (WINAPI *GetCursorPos_t)(LPPOINT);
static GetCursorPos_t oGetCursorPos = NULL;
static BOOL WINAPI hGetCursorPos(LPPOINT p)
{
    if (p && (int)(g_cursorUntil - GetTickCount()) > 0) { p->x = g_injX; p->y = g_injY; return TRUE; }
    return oGetCursorPos(p);
}

typedef SHORT (WINAPI *GetAsyncKeyState_t)(int);
static GetAsyncKeyState_t oGetAsyncKeyState = NULL;
static SHORT WINAPI hGetAsyncKeyState(int vk)
{
    if (vk == VK_LBUTTON && (int)(g_btnUntil - GetTickCount()) > 0) return (SHORT)0x8001;
    return oGetAsyncKeyState(vk);
}
typedef SHORT (WINAPI *GetKeyState_t)(int);
static GetKeyState_t oGetKeyState = NULL;
static SHORT WINAPI hGetKeyState(int vk)
{
    if (vk == VK_LBUTTON && (int)(g_btnUntil - GetTickCount()) > 0) return (SHORT)0x8001;
    return oGetKeyState(vk);
}

static void InstallInputHooks(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    if (MH_CreateHook(GetProcAddress(u,"GetActiveWindow"),  &hGetActiveWindow,  (void**)&oGetActiveWindow)  == MH_OK) MH_EnableHook(GetProcAddress(u,"GetActiveWindow"));
    if (MH_CreateHook(GetProcAddress(u,"GetCursorPos"),     &hGetCursorPos,     (void**)&oGetCursorPos)     == MH_OK) MH_EnableHook(GetProcAddress(u,"GetCursorPos"));
    if (MH_CreateHook(GetProcAddress(u,"GetAsyncKeyState"), &hGetAsyncKeyState, (void**)&oGetAsyncKeyState) == MH_OK) MH_EnableHook(GetProcAddress(u,"GetAsyncKeyState"));
    if (MH_CreateHook(GetProcAddress(u,"GetKeyState"),      &hGetKeyState,      (void**)&oGetKeyState)      == MH_OK) MH_EnableHook(GetProcAddress(u,"GetKeyState"));
}

// client 좌표 클릭을 주입: 커서를 그 위치로 두고(hold_ms) 좌버튼을 down_ms 동안 눌렀다 뗌.
static void InjectClick(HWND game, int cx, int cy, int dbl)
{
    POINT p; p.x = cx; p.y = cy; ClientToScreen(game, &p);
    InterlockedExchange(&g_injX, p.x); InterlockedExchange(&g_injY, p.y);
    g_cursorUntil = GetTickCount() + 500;      // 0.5s 동안 커서 그 위치
    Sleep(80);                                  // 게임이 커서 위치를 먼저 폴링하게
    g_btnUntil = GetTickCount() + 160;          // 160ms 버튼 down (폴링 여러 번 잡히게)
    Sleep(220);
    if (dbl) { g_btnUntil = GetTickCount() + 160; Sleep(220); }
}

static void WriteAck(const wchar_t* p, const char* s)
{
    DWORD wr; HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, s, lstrlenA(s), &wr, NULL); CloseHandle(f); }
}

static DWORD WINAPI Worker(LPVOID param)
{
    wchar_t tmp[MAX_PATH], cmdPath[MAX_PATH], ackPath[MAX_PATH];
    (void)param;
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(cmdPath, L"%scds_ctrl_cmd.txt", tmp);
    wsprintfW(ackPath, L"%scds_ctrl_ack.txt", tmp);
    OutputDebugStringW(L"[ScreenUtilKR] worker started.");
    for (;;)
    {
        HANDLE f; char buf[512]; DWORD rd = 0; char cmd[32], rest[400], ack[256]; int a = 0, b = 0; HWND game;
        Sleep(120);
        f = CreateFileW(cmdPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL); CloseHandle(f);
        DeleteFileW(cmdPath);
        if (rd == 0) continue;
        buf[rd] = 0; ack[0] = 0; cmd[0] = 0;
        game = FindGame();
        if (sscanf(buf, "%31s", cmd) != 1) { lstrcpyA(ack, "ERR parse"); }
        else if (!lstrcmpiA(cmd, "capture") && sscanf(buf, "%*s %399[^\r\n]", rest) == 1)
        {
            wchar_t wp[MAX_PATH]; BOOL ok;
            MultiByteToWideChar(CP_ACP, 0, rest, -1, wp, MAX_PATH);
            ok = SaveLastFrame(wp);
            wsprintfA(ack, ok ? "OK capture frames=%d %s" : "ERR capture(no frame yet, frames=%d) %s", g_frameCount, rest);
        }
        else if (!game) { lstrcpyA(ack, "ERR no game window"); }
        else if (!lstrcmpiA(cmd, "click")    && sscanf(buf, "%*s %d %d", &a, &b) == 2) { InjectClick(game,a,b,0); wsprintfA(ack,"OK click %d %d",a,b); }
        else if (!lstrcmpiA(cmd, "dblclick") && sscanf(buf, "%*s %d %d", &a, &b) == 2) { InjectClick(game,a,b,1); wsprintfA(ack,"OK dblclick %d %d",a,b); }
        else if (!lstrcmpiA(cmd, "move")     && sscanf(buf, "%*s %d %d", &a, &b) == 2)
        {
            POINT p; p.x=a; p.y=b; ClientToScreen(game,&p);
            InterlockedExchange(&g_injX,p.x); InterlockedExchange(&g_injY,p.y); g_cursorUntil=GetTickCount()+500;
            wsprintfA(ack,"OK move %d %d",a,b);
        }
        else lstrcpyA(ack, "ERR bad cmd");
        WriteAck(ackPath, ack);
    }
    return 0;
}

void ScreenUtil_Start(void)
{
    HANDLE t;
    InitializeCriticalSection(&g_lock);
    if (MH_Initialize() == MH_OK) { InstallCaptureHooks(); InstallInputHooks(); }
    t = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
