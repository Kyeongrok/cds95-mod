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

        // 후킹 초기화가 실패하거나 대상 함수를 못 찾아도 게임 자체는 정상 진행되어야 합니다.
        // (여기서 실패를 이유로 FALSE를 반환하면 게임 프로세스 전체가 죽습니다.)
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
