#include <windows.h>
#include <MinHook.h>

// ShipSkinKR (w2 코스메틱) — 항해 배 이미지만 바꾸기 (스탯/이름/함선종류 불변).
// 해상 지도 스프라이트는 함선종류 getter(0x44C6E0)를 매 프레임 호출해 받아온다. 그 호출자 중
// 스프라이트 그래픽 포인터를 만드는 call-site(0x48A84E → 0x48A853)를 우리 함수로 리다이렉트해서
// "기함이고 스킨 켜짐"일 때만 선택 타입을 반환하고, 그 외 getter 호출(스탯/UI/전투)은 진짜
// 함선종류를 받게 둔다 → 화면만 스킨, 스탯 불변. (0x5B3A00 은 스프라이트와 무관한 죽은 후보였음.)
// 부가로 출항 함대그래픽(0x46A520) swap-restore 훅도 유지(다른 화면 커버). 상세는 hook.c 주석.
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
