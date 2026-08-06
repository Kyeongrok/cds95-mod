#include <windows.h>
#include "update.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        UpdateKR_Init(hModule);   // "파일 > 업데이트" 메뉴 설치 (백그라운드 스레드)
    }
    return TRUE;
}
