#include <windows.h>

// GameApiKR (w12) — PowerShell에서 게임 내부 함수를 파라미터와 함께 호출하는 API.
// 게임 실행 시 같이 로드되어 워커 스레드가 명령파일을 폴링, 지정 함수를 호출하고 결과를 반환한다.
//
// 프로토콜:
//   명령: %TEMP%\cds_api_cmd.txt  (한 줄)
//     "<conv> <addr_hex> [ecx_hex] [arg1_hex] [arg2_hex] ..."
//     conv: c=__cdecl, t=__thiscall(ecx=this), s=__stdcall
//     addr_hex: 함수 주소(hex, 예 44C6E0)
//     ecx_hex: thiscall의 this (c/s면 첫 인자로 취급 안 함; 자리 맞추려면 0)
//     argN_hex: 정수 인자들(hex)
//   결과: %TEMP%\cds_api_ack.txt  →  "OK <eax_hex>"  또는 "ERR <msg>"
//
//   예) "t 44C6E0 5A4E18"        → thiscall getType(this=0x5A4E18) = 기함 함선종류
//       "c 401000 0 1 2"          → cdecl func(1,2)

void GameApi_Start(void);

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        GameApi_Start();
    }
    return TRUE;
}
