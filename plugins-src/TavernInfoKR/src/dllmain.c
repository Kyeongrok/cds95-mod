#include <windows.h>
#include "tavwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 후킹 없음. .rdata 의 코드·도시 칸만 VirtualProtect 로 잠깐 열고 고친다.
        // tavern_info.json 을 넣는 것도 메뉴 스레드가 뜨자마자 한다(tavwin.c MenuThread).
        TavernKR_Init(hModule);
    }
    return TRUE;
}
