#include "hooks.h"
#include <MinHook.h>

// ============================================================================
// TODO(리버싱 필요): 아래 주소는 전부 자리표시자(placeholder)입니다.
//
// CDS95.exe(한국어판 Ver.1.2.0.0, cds95runfile/CDS95.EXE)를 Ghidra 등으로 열어
// "아이템을 인벤토리에 추가하는 함수"의 실제 시작 주소를 찾은 뒤 채워 넣으세요.
//
// 추천 절차:
//   1) 게임을 실행하고 아이템을 하나 획득한다.
//   2) Cheat Engine으로 인벤토리 슬롯(아이템 ID/개수)이 저장된 메모리 주소를 스캔한다.
//      (정확한 값으로 스캔 -> 아이템을 하나 더 얻어서 값이 바뀐 주소로 좁혀나가는 방식)
//   3) 그 주소에 "Find out what writes to this address"를 걸고 아이템을 다시 획득해
//      실제로 그 값을 써넣는 CALL 명령의 위치(= 호출자)를 알아낸다.
//   4) 그 호출자에서 CALL하는 함수의 시작 주소로 들어가, 그 함수 자체가
//      "추가 함수"의 진짜 시작인지 더 위로 타고 올라가며 확인한다.
//   5) Ghidra에서 해당 함수를 디스어셈블해 인자 개수/타입, 호출 규약
//      (cdecl / stdcall / thiscall)을 확인하고 아래 함수 포인터 타입을 맞게 수정한다.
//   6) 함수의 런타임 VA(= GetModuleHandle(NULL) 기준 주소, 이 게임은 비-ASLR 고정
//      베이스이므로 그대로 하드코딩 가능하지만, 안전하게 이미지 베이스 기준
//      상대 오프셋으로 계산하는 것을 권장)를 아래 표에 채운다.
//
// 아래 값(0)은 "이 버전은 아직 모른다"는 의미이며, InstallHooks가 안전하게 건너뜁니다.
// ============================================================================

typedef struct
{
    DWORD version;      // GetGameVersion()의 반환값. 예: 1200 = 한국어판 1.2.0.0
    LPVOID targetRVA;    // 이미지 베이스로부터의 상대 오프셋(RVA). 아직 모르면 NULL.
} HookTarget;

static const HookTarget kAddItemTargets[] = {
    { 1270, NULL }, // 일본판 Ver.1.2.7.0 - 원본 CollectionUtil.plugin 리버싱 필요
    { 1400, NULL }, // 일본판 Ver.1.4.0.0 - 원본 CollectionUtil.plugin 리버싱 필요
    { 1200, NULL }, // 한국어판 Ver.1.2.0.0 - 직접 리버싱 필요 (cds95runfile/CDS95.EXE 대상)
};

// TODO(리버싱 필요): 실제 호출 규약과 인자를 확인하기 전까지는 추측치입니다.
// KOEI 구형 타이틀은 C로 작성된 경우가 많아 cdecl/stdcall일 가능성이 높지만,
// 반드시 Ghidra 디컴파일 결과(스택 정리 방식)로 검증한 뒤 수정하세요.
typedef int(__cdecl* AddItemToInventory_t)(void* pPlayer, int itemId, int quantity);

static AddItemToInventory_t g_originalAddItemToInventory = NULL;

// 지금은 원본 동작을 그대로 통과시키기만 합니다.
// -> 이 상태로 먼저 "게임이 죽지 않고 정상 동작하는지"부터 검증한 뒤,
//    실제 기능(16개 제한 우회 등)을 여기에 추가하세요.
static int __cdecl DetourAddItemToInventory(void* pPlayer, int itemId, int quantity)
{
    return g_originalAddItemToInventory(pPlayer, itemId, quantity);
}

static LPVOID ResolveTarget(DWORD gameVersion)
{
    HMODULE hGame = GetModuleHandleW(NULL);
    if (!hGame)
        return NULL;

    for (size_t i = 0; i < sizeof(kAddItemTargets) / sizeof(kAddItemTargets[0]); i++)
    {
        if (kAddItemTargets[i].version == gameVersion && kAddItemTargets[i].targetRVA != NULL)
        {
            return (LPBYTE)hGame + (SIZE_T)kAddItemTargets[i].targetRVA;
        }
    }
    return NULL;
}

void InstallHooks(DWORD gameVersion)
{
    LPVOID target = ResolveTarget(gameVersion);
    if (!target)
    {
        // 아직 이 버전용 주소가 채워지지 않음 -> 조용히 건너뜀 (DebugView로 확인 가능)
        OutputDebugStringW(L"[CollectionUtilKR] hook target not found for this version, skipping.");
        return;
    }

    if (MH_CreateHook(target, &DetourAddItemToInventory,
                       (LPVOID*)&g_originalAddItemToInventory) != MH_OK)
    {
        OutputDebugStringW(L"[CollectionUtilKR] MH_CreateHook failed.");
        return;
    }

    if (MH_EnableHook(target) != MH_OK)
    {
        OutputDebugStringW(L"[CollectionUtilKR] MH_EnableHook failed.");
        return;
    }

    OutputDebugStringW(L"[CollectionUtilKR] hook installed.");
}

void RemoveHooks(void)
{
    MH_DisableHook(MH_ALL_HOOKS);
}
