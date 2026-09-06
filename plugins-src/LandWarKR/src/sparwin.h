#pragma once
#include <windows.h>

// 육상전 모의전 창. 창을 짓는 것도, 판을 벌이는 것도 모두 게임 스레드에서 한다.
void SparWin_Init(HINSTANCE hinst);
void SparWin_Show(HWND owner);

// 게임 창 프로시저에서 그대로 넘겨 준다. 우리 메시지였으면 1.
int  SparWin_OnGameMsg(HWND game, UINT msg, WPARAM w, LPARAM l);
