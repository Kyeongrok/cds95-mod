#include <windows.h>
#include "dialog.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        DialogKR_Init(hModule);   // dialogs\*.json 로드 + 적용 + "파일>모드>대사" 메뉴 설치
    }
    return TRUE;
}
