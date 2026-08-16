#include <windows.h>
#include "btnwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 후킹 없음. 게임 파일을 읽기만 한다 — 실제 로드는 창을 열 때 한다.
        BtnKR_Init(hModule);   // "파일 > 모드 > 버튼 만들기" 메뉴 설치
    }
    return TRUE;
}
