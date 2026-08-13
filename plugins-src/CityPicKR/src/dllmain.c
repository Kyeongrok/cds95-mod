#include <windows.h>
#include "picwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        PicMenu_Init(hModule);   // 게임 창을 찾아 "파일 > 도시그림" 메뉴 설치 (백그라운드 스레드)
    }
    return TRUE;
}
