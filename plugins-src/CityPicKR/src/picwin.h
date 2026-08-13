#pragma once
#include <windows.h>

// 도시 그림 창을 연다(CITYCG.CDS 를 그대로 보여준다. 게임 메모리는 건드리지 않는다).
void PicWin_Show(HWND owner, HINSTANCE hinst);

// 게임 창을 찾아 "파일" 메뉴에 "도시그림" 항목을 설치한다(백그라운드 스레드). DllMain 에서 호출.
void PicMenu_Init(HINSTANCE hinst);
