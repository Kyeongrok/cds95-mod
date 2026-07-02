#include <windows.h>
#include <MinHook.h>
#include "version.h"
#include "hooks.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        // 후킹 실패/대상 미검증이어도 게임은 정상 진행되어야 한다.
        // (여기서 FALSE 를 반환하면 게임 프로세스 전체가 죽는다.)
        if (MH_Initialize() == MH_OK)
        {
            InstallHooks(GetGameVersion());
        }
        break;

    case DLL_PROCESS_DETACH:
        RemoveHooks();
        MH_Uninitialize();
        break;
    }

    return TRUE;
}
