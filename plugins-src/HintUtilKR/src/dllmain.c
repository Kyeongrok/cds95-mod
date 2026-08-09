#include <windows.h>
#include "hint.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HintKR_Init(hModule);   // "파일 > 힌트" 메뉴 설치
    }
    return TRUE;
}
