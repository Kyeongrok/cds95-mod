#include <windows.h>
#include "skillwin.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 후킹은 하나도 없다 — 메모리 표만 읽고 쓴다. 그래서 MinHook 도 안 쓴다.
        SkillKR_Init(hModule);   // "파일 > 모드 > 기능수련" 메뉴 설치 + skills.json 적용
        break;
    }
    return TRUE;
}
