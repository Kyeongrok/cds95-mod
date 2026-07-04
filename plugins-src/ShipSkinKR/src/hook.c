#include <windows.h>
#include <MinHook.h>
#include <string.h>
#include <stdlib.h>

// ShipSkinKR — 항해 배 이미지만 바꾸기 (스탯/이름/함선종류 전부 불변).
//
// 스프라이트 소스는 함선종류 필드(기함 = 0x5A4E40 = ship[0]+0x28)다. CE find-what-accesses 로
// 확인한 결과, 해상 지도의 배 스프라이트는 이 필드를 "직접" 읽지 않고 getter 0x44C6E0
// ( mov eax,[ecx+28]; ret ) 을 매 프레임 호출해 받아온다. 그 호출자 중 스프라이트 그래픽
// 데이터 포인터를 만드는 곳이 0x48A853 (call @0x48A84E). 이 한 call-site 를 우리 함수로
// 리다이렉트해서 "기함이고 스킨이 켜져 있을 때만" 선택 타입을 반환하고, 그 외(스탯/UI/전투)의
// getter 호출은 진짜 함선종류를 그대로 받게 둔다 → 화면만 바뀌고 스탯은 불변.
//   (검증: 0x48A84E 를 mov eax,7 로 임시 치환 시 해상 스프라이트만 다우로, 함선정보는 코구 유지.)
//
// 추가로 출항 함대그래픽 함수(0x46A520)에 대한 기존 swap-restore 훅도 유지한다(함대편성 등
// 다른 화면 커버). getter 리다이렉트와 독립적이라 충돌 없음.
//
// 선택 타입: %TEMP%\cds_shiptype.txt (정수 0~7, 없거나 -1이면 미적용).

// ---- 함대그래픽 함수 (RVA, base 0x400000). 시그니처로 빌드 검증 ----
#define RVA_FLEETGFX   0x0006A520u          // 0x46A520, __thiscall(this), ret(인자없음), SEH 프롤로그
#define FLAGSHIP_TYPE  0x005A4E40u          // 기함(slot0) 함선종류 (ship[0]+0x28)

// ---- getter call-site 리다이렉트 (해상 스프라이트) ----
#define RVA_GETTER     0x0004C6E0u          // 0x44C6E0  mov eax,[ecx+28]; ret  (함선종류 getter)
#define RVA_CALLSITE   0x0008A84Eu          // 0x48A84E  call 0x44C6E0  (스프라이트 그래픽 포인터 산출부)
#define RVA_SHIP0      0x001A4E18u          // 0x5A4E18  기함(slot0) 구조체 base
#define SHIP_TYPE_OFF  0x28u                // 함선종류 오프셋

typedef int (__fastcall* FleetGfx_t)(void* thisptr, void* edx);
static FleetGfx_t g_orig = NULL;

// 시그니처: mov eax,fs:[0]; push ebp; mov ebp,esp
static const BYTE kSig[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x55, 0x8B, 0xEC };

// 선택 스프라이트 타입(0~7). 파일 %TEMP%\cds_shiptype.txt 에서 읽음.
static int ReadChosenType(void)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    HANDLE f; char buf[16]; DWORD rd = 0; int v;
    if (!GetTempPathW(MAX_PATH, tmp)) return -1;
    wsprintfW(path, L"%scds_shiptype.txt", tmp);
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL)) { CloseHandle(f); return -1; }
    CloseHandle(f);
    buf[rd] = 0;
    v = atoi(buf);
    return (v >= 0 && v <= 7) ? v : -1;
}

// getter 리다이렉트는 매 프레임 호출되므로 파일 I/O를 스로틀(200ms)해 캐시.
static volatile LONG g_skin = -1;
static DWORD g_lastCheck = 0;
static void RefreshSkin(void)
{
    DWORD now = GetTickCount();
    if ((now - g_lastCheck) < 200) return;
    g_lastCheck = now;
    g_skin = ReadChosenType();
}

// ---- getter 리다이렉트용 우리 함수 (원본과 동일한 __thiscall 시맨틱: ecx=ship, ret in eax) ----
static LPBYTE g_base = NULL;
static int __fastcall DetourGetType(void* thisptr, void* edx)
{
    int real = *(volatile int*)((BYTE*)thisptr + SHIP_TYPE_OFF);
    (void)edx;
    RefreshSkin();
    // 기함이고 스킨이 켜져 있을 때만 화면용으로 치환. 그 외엔 진짜 함선종류.
    if (g_skin >= 0 && thisptr == (void*)(g_base + RVA_SHIP0))
        return g_skin;
    return real;
}

// ---- 출항 함대그래픽 훅 (swap-restore) ----
static int __fastcall DetourFleetGfx(void* thisptr, void* edx)
{
    int chosen = ReadChosenType();
    if (chosen >= 0)
    {
        volatile int* pType = (volatile int*)FLAGSHIP_TYPE;
        int save = *pType;
        int r;
        *pType = chosen;          // 스프라이트 로드용으로 치환
        r = g_orig(thisptr, edx); // 원본: 선택 타입으로 비트맵 로드
        *pType = save;            // 스탯용 원복
        return r;
    }
    return g_orig(thisptr, edx);
}

// ---- call-site 리다이렉트 설치/해제 ----
static BYTE g_origCall[5];
static BOOL g_callPatched = FALSE;

static void InstallGetterRedirect(void)
{
    BYTE* site = g_base + RVA_CALLSITE;
    DWORD getter = (DWORD)(g_base + RVA_GETTER);
    DWORD oldProt; int newRel;
    if (site[0] != 0xE8)   // 검증: call rel32
    {
        OutputDebugStringW(L"[ShipSkinKR] getter call-site: not E8 - skip (unsupported build).");
        return;
    }
    {
        int rel = *(int*)(site + 1);
        DWORD target = (DWORD)(site + 5) + (DWORD)rel;
        if (target != getter)
        {
            OutputDebugStringW(L"[ShipSkinKR] getter call-site: target mismatch - skip (unsupported build).");
            return;
        }
    }
    memcpy(g_origCall, site, 5);
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return;
    newRel = (int)((BYTE*)&DetourGetType - (site + 5));
    site[0] = 0xE8;
    *(int*)(site + 1) = newRel;
    VirtualProtect(site, 5, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_callPatched = TRUE;
    OutputDebugStringW(L"[ShipSkinKR] sea-sprite getter call-site redirected.");
}

static void RemoveGetterRedirect(void)
{
    BYTE* site;
    DWORD oldProt;
    if (!g_callPatched || !g_base) return;
    site = g_base + RVA_CALLSITE;
    if (VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        memcpy(site, g_origCall, 5);
        VirtualProtect(site, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_callPatched = FALSE;
}

void InstallShipSkinHook(void)
{
    LPBYTE base = (LPBYTE)GetModuleHandleW(NULL);
    LPBYTE fn;
    if (!base) return;
    g_base = base;

    // 1) 해상 스프라이트: getter call-site 리다이렉트 (핵심)
    InstallGetterRedirect();

    // 2) 출항 함대그래픽: swap-restore 훅 (부가 커버) — 시그니처 검증
    fn = base + RVA_FLEETGFX;
    if (memcmp(fn, kSig, sizeof(kSig)) != 0)
    {
        OutputDebugStringW(L"[ShipSkinKR] fleet-gfx signature mismatch - skip (unsupported build).");
        return;
    }
    if (MH_CreateHook(fn, &DetourFleetGfx, (LPVOID*)&g_orig) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] CreateHook fail."); return; }
    if (MH_EnableHook(fn) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] EnableHook fail."); return; }
    OutputDebugStringW(L"[ShipSkinKR] hook installed (sea-sprite redirect + fleet-gfx swap).");
}

void RemoveShipSkinHook(void)
{
    RemoveGetterRedirect();
    MH_DisableHook(MH_ALL_HOOKS);
}
