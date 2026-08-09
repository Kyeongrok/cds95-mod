#include <windows.h>
#include "hotkey.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HotkeyKR_Init(hModule);   // "파일 > 단축키" 메뉴 설치 + 키보드 훅
    }
    return TRUE;
}
