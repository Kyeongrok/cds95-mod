#include <windows.h>
#include "dcwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 후킹 없음. .rdata 의 좌표 넷만 VirtualProtect 로 잠깐 열고 고친다.
        // disc_coords.json 을 넣는 것도 메뉴 스레드가 뜨자마자 한다(dcwin.c MenuThread).
        DiscEditKR_Init(hModule);
    }
    return TRUE;
}
