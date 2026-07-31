#include <windows.h>
#include "patch.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        PatchKR_Init(hModule);   // patches.json 로드 + "파일>패치" 메뉴 설치(백그라운드 스레드)
    }
    return TRUE;
}
