#include <windows.h>

// ModWindowKR — 파일 메뉴에 흩어진 KR 플러그인 항목을 "모드" 하나로 모으고,
// 그 "모드" 를 누르면 하위 메뉴 대신 창이 떠 단추로 늘어놓는다.
// 왜 메뉴를 남겨 두는지는 menu.c 머리말에 적었다.

void ModWindow_Init(HINSTANCE hinst);

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        ModWindow_Init(hModule);
    }
    return TRUE;
}
