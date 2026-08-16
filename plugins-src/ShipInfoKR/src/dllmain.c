#include <windows.h>
#include "shipwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 후킹 없음. 표와 그림 파일을 읽기만 한다.
        ShipInfoKR_Init(hModule);   // "파일 > 모드 > 선체" 메뉴 설치
    }
    return TRUE;
}
