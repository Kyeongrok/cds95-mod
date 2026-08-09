#include <windows.h>
#include "fatigue.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        FatigueUtilKR_Init(hModule);   // "파일 > 피로도" 메뉴 설치
    }
    return TRUE;
}
