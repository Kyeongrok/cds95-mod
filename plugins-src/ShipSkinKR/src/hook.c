#include <windows.h>
#include <MinHook.h>
#include <string.h>
#include <stdlib.h>

// 출항 함대그래픽 함수 (RVA, base 0x400000). 시그니처로 빌드 검증.
#define RVA_FLEETGFX   0x0006A520u          // 0x46A520, __thiscall(this), ret(인자없음), SEH 프롤로그
#define FLAGSHIP_TYPE  0x005A4E40u          // 기함(slot0) 함선종류 (ship[0]+0x28)

// __thiscall(ecx=this) 인자없음 → __fastcall(ecx, edx더미) 모델. 반환은 int(사용 안 함).
typedef int (__fastcall* FleetGfx_t)(void* thisptr, void* edx);
static FleetGfx_t g_orig = NULL;

// 시그니처: mov eax,fs:[0]; push ebp; mov ebp,esp
static const BYTE kSig[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x55, 0x8B, 0xEC };

// 선택 스프라이트 타입(0~7). 파일 %TEMP%\cds_shiptype.txt 에서 매번 읽음(출항 때만 호출되니 저렴).
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

void InstallShipSkinHook(void)
{
    LPBYTE base = (LPBYTE)GetModuleHandleW(NULL);
    LPBYTE fn;
    if (!base) return;
    fn = base + RVA_FLEETGFX;
    if (memcmp(fn, kSig, sizeof(kSig)) != 0)
    {
        OutputDebugStringW(L"[ShipSkinKR] fleet-gfx signature mismatch - skip (unsupported build).");
        return;
    }
    if (MH_CreateHook(fn, &DetourFleetGfx, (LPVOID*)&g_orig) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] CreateHook fail."); return; }
    if (MH_EnableHook(fn) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] EnableHook fail."); return; }
    OutputDebugStringW(L"[ShipSkinKR] hook installed (out-of-port ship sprite skin).");
}

void RemoveShipSkinHook(void)
{
    MH_DisableHook(MH_ALL_HOOKS);
}
