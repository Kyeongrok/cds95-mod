#include "hooks.h"
#include "dialog.h"
#include <MinHook.h>
#include <intrin.h>   // _ReturnAddress
#include <string.h>   // memcmp

// ============================================================================
// HotelUtilKR — 여관 "숙박" 일수 후킹 (한국어판 cds_95.exe, 데스크톱 빌드 기준)
//
// 리버싱으로 확정한 호출 체인 (base 0x400000, 비-ASLR):
//   여관핸들러 0x47FC9A: push 0x1E(=30) / 0x47FC9C: call Rest(0x4A2AD0)  [리턴 0x47FCA1]
//     Rest(this, days, mode) 0x4A2AD0 : 숙박비(days 기반) 차감 후 AdvanceDays(days) 호출
//       AdvanceDays 0x44AFD0 : days 회 루프로 하루씩 날짜 진행 + 매일 게임 업데이트
//
// 전략: Rest(0x4A2AD0)를 후킹하고, 호출자(리턴주소)가 여관(0x47FCA1)일 때만 days 인자를
//       교체한다. 그러면 여관 숙박만 영향받고(숙박비도 days 기반이라 자동 비례),
//       항해/이벤트 등 다른 Rest 호출은 원본 그대로 통과.
//
// 이번 단계(fb4 스켈레톤): days 를 고정값으로 바꿔 "후킹 파이프라인이 동작하는지"만 검증.
//       다음 단계에서 이 고정값을 계산기 UI 입력값으로 교체.
// ============================================================================

// RVA (이미지 베이스 기준). 데스크톱 cds_95.exe 에서 확정.
#define RVA_REST        0x000A2AD0u   // Rest(this, days, mode) 진입점  (0x4A2AD0)
#define RVA_HOTEL_RET   0x0007FCA1u   // 여관의 call Rest 다음 명령 = 리턴주소 필터 (0x47FCA1)
#define RVA_HOTEL_PUSH  0x0007FC9Au   // 여관: push 0x1E(30) 리터럴 위치 (시그니처 검증용)

// Rest 는 __thiscall(ecx=this) + 스택 2인자(days, mode), ret 8.
// MSVC 자유함수엔 __thiscall 을 못 쓰므로 __fastcall(ecx, edx, ...)로 모델링한다.
// edx 는 더미: 원본은 진입 시 edx 를 쓰지 않는다(첫 명령이 mov eax,[esp+8]).
typedef int(__fastcall* Rest_t)(void* thisptr, void* edx, int days, int mode);

static Rest_t     g_origRest = NULL;
static uintptr_t  g_hotelRet = 0;

static int __fastcall DetourRest(void* thisptr, void* edx, int days, int mode)
{
    // 이 Rest 호출이 "여관 숙박" 경로에서 온 것인지 리턴주소로 판별.
    if ((uintptr_t)_ReturnAddress() == g_hotelRet)
    {
        // 모달 입력창을 띄워 숙박 일수를 받는다. 취소하면 기본값(원래 30일) 유지.
        OutputDebugStringW(L"[HotelUtilKR] hotel stay intercepted -> asking days.");
        days = HotelKR_AskDays(days);
    }
    return g_origRest(thisptr, edx, days, mode);
}

// Rest 진입 시그니처: 8B 44 24 08 (mov eax,[esp+8]) 56 (push esi) 83 F8 02 (cmp eax,2)
static const BYTE kRestSig[] = { 0x8B, 0x44, 0x24, 0x08, 0x56, 0x83, 0xF8, 0x02 };

void InstallHooks(DWORD gameVersion)
{
    (void)gameVersion;

    HMODULE hGame = GetModuleHandleW(NULL);
    if (!hGame)
        return;

    LPBYTE base = (LPBYTE)hGame;
    LPBYTE rest = base + RVA_REST;

    // 잘못된 빌드에 훅을 걸어 크래시 나는 것을 막기 위해 원본 바이트를 검증한다.
    // (PatchUtil 이 패치 전 원본 바이트를 검증하는 것과 같은 취지.)
    if (memcmp(rest, kRestSig, sizeof(kRestSig)) != 0)
    {
        OutputDebugStringW(L"[HotelUtilKR] Rest signature mismatch - skipping (unsupported build).");
        return;
    }
    if (base[RVA_HOTEL_PUSH] != 0x6A || base[RVA_HOTEL_PUSH + 1] != 0x1E)
    {
        OutputDebugStringW(L"[HotelUtilKR] hotel push-30 signature mismatch - skipping.");
        return;
    }

    g_hotelRet = (uintptr_t)(base + RVA_HOTEL_RET);

    if (MH_CreateHook(rest, &DetourRest, (LPVOID*)&g_origRest) != MH_OK)
    {
        OutputDebugStringW(L"[HotelUtilKR] MH_CreateHook failed.");
        return;
    }
    if (MH_EnableHook(rest) != MH_OK)
    {
        OutputDebugStringW(L"[HotelUtilKR] MH_EnableHook failed.");
        return;
    }

    OutputDebugStringW(L"[HotelUtilKR] hook installed (skeleton: fixed days).");
}

void RemoveHooks(void)
{
    MH_DisableHook(MH_ALL_HOOKS);
}
