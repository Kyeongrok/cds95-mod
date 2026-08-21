#include <windows.h>
#include "hook.h"

// WindArrowKR — 항해 화면 바다 위에 풍향·해류 화살표를 얹는다.
//
// cds-helper 는 제 지도 창에 셰이더로 그리지만 이쪽은 게임 화면 자체다. 해상 렌더러
// 0x48A1E0 이 돌아온 자리에서, 게임이 배·말을 찍을 때 쓰는 함수 0x48AC30 으로 우리
// 화살표를 얹는다. 자세한 것은 hook.c / gameaddr.h 주석에 있다.
//
// 켜고 끄기는 게임 창 "파일 > 풍향 화살표" 다. 처음에는 꺼져 있다.

void ArrowMenu_Init(HINSTANCE hinst);

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Overlay_Install();      // 실패해도 게임은 그대로 돈다
        ArrowMenu_Init(hModule);
        break;
    case DLL_PROCESS_DETACH:
        Overlay_Uninstall();
        break;
    }
    return TRUE;
}
