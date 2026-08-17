#include <windows.h>
#include "warwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 훅 설치는 메뉴 감시 스레드에서 한다(DllMain 안에서 MinHook 을 쓰면 안 된다).
        LandKR_Init(hModule);   // "파일 > 모드 > 육상전 부대" 메뉴 설치
    }
    return TRUE;
}
