#include <windows.h>
#include "market.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        MarketKR_Init(hModule);   // "파일 > 매매" 메뉴 설치
    }
    return TRUE;
}
