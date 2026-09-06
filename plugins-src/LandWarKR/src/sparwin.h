#pragma once
#include <windows.h>

// 육상전 모의전 창. 창을 짓는 것도, 판을 벌이는 것도 모두 게임 스레드에서 한다.
// 게임 창에는 손대지 않는다 — 이 창은 「육상전 부대」 창의 [모의전…] 단추가 연다.
void SparWin_Init(HINSTANCE hinst);
void SparWin_Show(HWND owner);
