#include <windows.h>
#include <MinHook.h>
#include "market.h"
#include "marketdb.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 게임 교역소가 열려 있는 동안을 알아내는 훅. 실패해도 게임은 그대로 돌아간다
        // (그때는 잠금이 안 걸릴 뿐이다).
        if (MH_Initialize() == MH_OK)
            Mkt_HookInstall();
        MarketKR_Init(hModule);   // "파일 > 매매" 메뉴 설치
        break;
    case DLL_PROCESS_DETACH:
        Mkt_HookRemove();
        MH_Uninitialize();
        break;
    }
    return TRUE;
}
