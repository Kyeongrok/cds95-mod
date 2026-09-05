#include <windows.h>
#include <MinHook.h>
#include <string.h>
#include "gameaddr.h"
#include "arrow.h"
#include "wind.h"
#include "hook.h"
#include "discovl.h"
#include "ovlflag.h"   // common/ — 규율 오버레이 켬/끔은 게임 창 프로퍼티에 있다

// 해상 화면 렌더러(0x48A1E0)가 제 일을 다 하고 돌아온 자리에서, 화면 왼쪽 위에 화살표
// 두 개(바람·해류)와 규율 숫자를 얹는다. 게임이 같은 자리에 글자로 쓰는 그 값이다 —
// 다만 글자는 [화면효과를 사용한다] 를 **꺼야** 나오고, 이 화살표는 늘 나온다.
// 그리는 일 자체는 게임 함수 0x48AC30 에 맡긴다 — 배와 말을 찍는 그 함수다. 그래서
//   · 화면효과 켬(DirectDraw Blt) / 끔(소프트 복사) 두 갈래를 우리가 안 가려도 되고
//   · 색인 0 이 투명으로 빠지고
//   · 화면 밖 클리핑과 상단 띠 32점 보정이 공짜로 붙는다.
//
// 렌더러가 함대·구름·상태표시까지 다 그린 뒤에 얹는 것이라 얹은 것이 그 위에 온다.
//
// 규율 숫자는 FatigueUtilKR 의 값이지만 그리는 것은 여기다 — 같은 함수를 DLL 둘이
// 저마다 훅하면 트램폴린이 얽히므로 훅은 한 곳에만 둔다(common/ovlflag.h 참고).

// 왼쪽 위 여백(점). y 는 그리는 함수가 +0x20 을 더해 주므로 상태 띠 아래로 알아서 내려간다.
#define MARGIN_X   6
#define MARGIN_Y   6
#define ARROW_GAP  2       // 바람과 해류 사이를 띄우는 점
#define DISC_GAP   4       // 화살표와 규율 사이

typedef void (__fastcall* Render_t)(void* thisptr, void* edx, int a1);
typedef void (__fastcall* DrawSprite_t)(void* thisptr, void* edx,
                                        void* surface, const void* bitmap, int frame,
                                        int x, int y, int w, int h);
typedef int  (__cdecl* MakeSurface_t)(void* desc, void** out, int zero);

// IDirectDrawSurface 가상함수표에서 우리가 쓰는 자리.
#define VT_LOCK    0x64
#define VT_UNLOCK  0x80
typedef HRESULT (__stdcall* Lock_t)(void* surf, void* rect, void* desc, DWORD flags, HANDLE ev);
typedef HRESULT (__stdcall* Unlock_t)(void* surf, void* data);

// DDSURFACEDESC 에서 우리가 채우고 읽는 자리(전체 0x6C 바이트).
#define DD_SIZE     0x00
#define DD_FLAGS    0x04
#define DD_HEIGHT   0x08
#define DD_WIDTH    0x0C
#define DD_PITCH    0x10
#define DD_SURFACE  0x24
#define DD_CAPS     0x68
#define DDSD_BYTES  0x6C
#define DDSD_FLAGS_CAPS_WH  7u        // CAPS | HEIGHT | WIDTH
#define DDSCAPS_PLAIN_SYSMEM 0x840u   // OFFSCREENPLAIN | SYSTEMMEMORY

HWND ArrowMenu_GameWnd(void);         // menu.c — 1초 폴링으로 찾아 둔 게임 창

static Render_t g_origRender;
static void*    g_surface;            // 화살표 아틀라스를 담은 서피스
static int      g_surfaceFails;       // 실패가 이어지면 그만둔다. 첫 실패로 포기하지는 않는다
static void*    g_discSurface;        // 규율 숫자를 담은 서피스(값이 바뀌면 다시 올린다)
static int      g_discFails;
static int      g_discSerial = -1;
static int      g_on;

// 서피스 만들기를 몇 번까지 다시 해 보는가. WorldMapKR 이 "한 번 실패하면 영영 안 붙는"
// 함정에 빠진 적이 있다(OCEAN.CDS 늦게 붙기) — 게임이 아직 덜 선 프레임에서 한 번
// 어긋났다고 그 판 내내 포기하면 증상이 "됐다 안 됐다" 로 보인다.
#define SURFACE_MAX_FAILS 60

int  Overlay_Enabled(void)      { return g_on; }
void Overlay_SetEnabled(int on) { g_on = on ? 1 : 0; }

// 게임이 배·말 아틀라스를 만들 때와 똑같이 만든다(0x489F00 부근). 실패하면 NULL.
static void* MakeSurface(int w, int h)
{
    MakeSurface_t makeSurface = (MakeSurface_t)GA(RVA_MAKESURFACE);
    unsigned char desc[DDSD_BYTES];
    void* surf = NULL;

    ZeroMemory(desc, sizeof(desc));
    *(unsigned*)(desc + DD_SIZE) = DDSD_BYTES;
    *(unsigned*)(desc + DD_FLAGS) = DDSD_FLAGS_CAPS_WH;
    *(unsigned*)(desc + DD_HEIGHT) = (unsigned)h;
    *(unsigned*)(desc + DD_WIDTH) = (unsigned)w;
    *(unsigned*)(desc + DD_CAPS) = DDSCAPS_PLAIN_SYSMEM;
    if (makeSurface(desc, &surf, 0) != 0) return NULL;
    return surf;
}

// 8bpp 색인 그림을 서피스에 옮긴다. 시스템 메모리 서피스라 줄 간격(pitch)이 폭보다
// 넓을 수 있으니 통째로 옮기지 않고 줄 단위로 옮긴다. 성공 1.
static int UploadSurface(void* surf, const unsigned char* px, int w, int h)
{
    unsigned char desc[DDSD_BYTES];
    unsigned char* dst;
    Lock_t lock; Unlock_t unlock;
    void** vtbl;
    int pitch, y;

    if (!surf || !px) return 0;
    vtbl = *(void***)surf;
    lock = (Lock_t)vtbl[VT_LOCK / 4];
    unlock = (Unlock_t)vtbl[VT_UNLOCK / 4];

    ZeroMemory(desc, sizeof(desc));
    *(unsigned*)(desc + DD_SIZE) = DDSD_BYTES;
    if (lock(surf, NULL, desc, 0, NULL) != 0) return 0;

    pitch = *(int*)(desc + DD_PITCH);
    dst = *(unsigned char**)(desc + DD_SURFACE);
    for (y = 0; y < h; y++)
        CopyMemory(dst + (size_t)y * pitch, px + (size_t)y * w, (size_t)w);

    unlock(surf, dst);
    return 1;
}

// 화살표 96장을 서피스로 올린다. 게임의 DirectDraw 가 서야 되는 일이라 첫 프레임에서 한다.
static int EnsureSurface(void)
{
    const unsigned char* atlas;

    if (g_surface) return 1;
    if (g_surfaceFails >= SURFACE_MAX_FAILS) return 0;
    g_surfaceFails++;

    atlas = Arrow_Atlas();
    if (!atlas) return 0;

    g_surface = MakeSurface(ARROW_W, ARROW_H * ARROW_FRAMES);
    if (!g_surface) return 0;
    if (!UploadSurface(g_surface, atlas, ARROW_W, ARROW_H * ARROW_FRAMES)) { g_surface = NULL; return 0; }

    g_surfaceFails = 0;
    OutputDebugStringW(L"[WindArrowKR] 화살표 서피스 준비 완료.");
    return 1;
}

// 규율 숫자 서피스. 값이 바뀌면 그림이 새로 구워지므로 그때마다 다시 올린다.
static int EnsureDiscSurface(const unsigned char* px)
{
    if (!g_discSurface) {
        if (g_discFails >= SURFACE_MAX_FAILS) return 0;
        g_discFails++;
        g_discSurface = MakeSurface(DISCOVL_W, DISCOVL_H);
        if (!g_discSurface) return 0;
        g_discFails = 0;
        g_discSerial = -1;
        OutputDebugStringW(L"[WindArrowKR] 규율 서피스 준비 완료.");
    }
    if (g_discSerial != DiscOvl_Serial()) {
        if (!UploadSurface(g_discSurface, px, DISCOVL_W, DISCOVL_H)) return 0;
        g_discSerial = DiscOvl_Serial();
    }
    return 1;
}

// 화살표 하나. 흐름이 없으면(세기 0) 아무것도 안 그리고 0 을 돌려준다.
static int DrawOne(void* mapObj, int kind, const Flow* f, int x, int y)
{
    DrawSprite_t drawSprite = (DrawSprite_t)GA(RVA_DRAWSPRITE);
    const unsigned char* atlas = Arrow_Atlas();
    int level = Arrow_Level(f->speed);
    int frame;

    if (level < 0) return 0;
    frame = ARROW_FRAME(kind, level, f->dir);
    drawSprite(mapObj, NULL, g_surface, atlas + (size_t)frame * ARROW_FRAME_SZ,
               frame, x, y, ARROW_W, ARROW_H);
    return 1;
}

static void DrawDiscipline(void* mapObj, int y)
{
    DrawSprite_t drawSprite = (DrawSprite_t)GA(RVA_DRAWSPRITE);
    const unsigned char* px = DiscOvl_Bitmap();

    if (!px) return;                        // 세이브를 아직 안 불러왔다
    if (!EnsureDiscSurface(px)) return;
    drawSprite(mapObj, NULL, g_discSurface, px, 0, MARGIN_X, y, DISCOVL_W, DISCOVL_H);
}

static void DrawOverlay(void* mapObj)
{
    int y = MARGIN_Y;

    if (!mapObj) return;
    // 지도를 안 그리는 갈래면 얹을 것도 없다(렌더러 0x48A213 과 같은 조건).
    if ((*(unsigned char*)GA(GA_SCREEN_MODE - GA_BASE) & SCREEN_MODE_MAP) == 0) return;

    if (g_on && EnsureSurface()) {
        Flow wind, current;
        Wind_Now(&wind, &current);
        // 바람이 위, 해류가 아래다. 바람 없는 칸은 없지만 해류는 18% 가 무해류라 자리가
        // 빈다 — 그때 바람 화살표가 아래로 내려오지 않게 자리는 늘 같게 둔다.
        DrawOne(mapObj, 0, &wind, MARGIN_X, y);
        y += ARROW_H + ARROW_GAP;
        DrawOne(mapObj, 1, &current, MARGIN_X, y);
        y += ARROW_H + DISC_GAP;
    }

    // 규율은 화살표를 껐으면 맨 위로 올라온다.
    if (OvlDisc_Get(ArrowMenu_GameWnd())) DrawDiscipline(mapObj, y);
}

static void __fastcall Hooked_Render(void* thisptr, void* edx, int a1)
{
    g_origRender(thisptr, edx, a1);
    // 우리가 얹다가 넘어져도 게임은 계속 돌게 한다 — 여기는 게임이 화면을 그리는 한복판이다.
    __try { DrawOverlay(thisptr); }        // 화살표와 규율은 안에서 따로 켜고 끈다
    __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringW(L"[WindArrowKR] 오버레이를 그리다 넘어졌다 — 이번 판은 건너뛴다.");
    }
}

// 훅을 걸기 전에 프롤로그를 확인한다. 판이 다르면 걸지 않는다.
static int VerifyTarget(void)
{
    static const BYTE kRender[] = { 0x81, 0xEC, 0x80, 0x01, 0x00, 0x00 };   // sub esp,0x180
    static const BYTE kDraw[]   = { 0x81, 0xEC, 0x8C, 0x00, 0x00, 0x00 };   // sub esp,0x8c
    static const BYTE kMake[]   = { 0xA1, 0x60, 0xD3, 0x62, 0x00 };         // mov eax,[0x62d360]
    return memcmp(GA(RVA_RENDER), kRender, sizeof(kRender)) == 0
        && memcmp(GA(RVA_DRAWSPRITE), kDraw, sizeof(kDraw)) == 0
        && memcmp(GA(RVA_MAKESURFACE), kMake, sizeof(kMake)) == 0;
}

int Overlay_Install(void)
{
    if (!VerifyTarget()) {
        OutputDebugStringW(L"[WindArrowKR] 판이 다르다 — 훅을 걸지 않는다.");
        return 0;
    }
    if (MH_Initialize() != MH_OK) return 0;
    if (MH_CreateHook(GA(RVA_RENDER), (LPVOID)Hooked_Render, (LPVOID*)&g_origRender) != MH_OK) return 0;
    if (MH_EnableHook(GA(RVA_RENDER)) != MH_OK) return 0;
    OutputDebugStringW(L"[WindArrowKR] 렌더러 훅 설치.");
    return 1;
}

void Overlay_Uninstall(void)
{
    MH_DisableHook(GA(RVA_RENDER));
    MH_Uninitialize();
}
