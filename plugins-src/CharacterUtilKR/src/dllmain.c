#include <windows.h>
#include "character.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CharKR_Init(hModule);   // 게임 창을 찾아 "인물" 메뉴 설치 (백그라운드 스레드)
    }
    return TRUE;
}
