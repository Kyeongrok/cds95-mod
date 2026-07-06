#include <windows.h>

// ScreenUtilKR (w5/w7 PoC) — 게임 화면 인프로세스 캡처 + 입력 주입.
// 외부(PrintWindow/CopyFromScreen/합성클릭)는 이 DirectDraw 게임에서 불안정/불가
//   (PrintWindow 8/8 흰색, CopyFromScreen handle-invalid, DPI는 무관/100%).
// → 게임 내부에서: 창 DC BitBlt로 실제 프레임 캡처 + 게임 자기 메시지루프에 PostMessage 클릭.
// 제어: %TEMP%\cds_ctrl_cmd.txt 에 명령 쓰면 실행, 결과는 %TEMP%\cds_ctrl_ack.txt.

void ScreenUtil_Start(void);

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        ScreenUtil_Start();   // 워커 스레드 시작 (실패해도 게임엔 영향 없음)
    }
    return TRUE;
}
