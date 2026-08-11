#include <windows.h>
#include "save.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        SaveUtilKR_Init(hModule);   // "파일 > 저장 · 중단" 메뉴 설치
    }
    return TRUE;
}
