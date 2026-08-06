#include <windows.h>
#include "questmod.h"

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        QuestModKR_Init(hModule);   // 고른 모드를 제자리에 깔고 "파일 > 퀘스트 모드" 메뉴 설치
    }
    return TRUE;
}
