#include <windows.h>
#include <MinHook.h>

// ShipSkinKR (w1/B 코스메틱) — 항해 배 이미지만 바꾸기 (스탯 불변).
// 게임은 출항(지도 진입) 시 함대그래픽 함수(0x46A520)에서 함선종류를 읽어 스프라이트 비트맵을
// 로드/캐싱한다. 이 함수 진입 동안만 기함(slot0, 0x5A4E40)의 함선종류를 사용자 선택 타입으로 치환하고
// 함수 종료 시 원복 → 스프라이트는 선택 타입으로 로드, 스탯(다른 코드가 읽음)은 원래 함선종류 유지.
// 선택 타입: %TEMP%\cds_shiptype.txt (정수 0~7, 없거나 -1이면 미적용).

void InstallShipSkinHook(void);
void RemoveShipSkinHook(void);

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        if (MH_Initialize() == MH_OK)
            InstallShipSkinHook();   // 실패해도 게임은 정상 진행
        break;
    case DLL_PROCESS_DETACH:
        RemoveShipSkinHook();
        MH_Uninitialize();
        break;
    }
    return TRUE;
}
