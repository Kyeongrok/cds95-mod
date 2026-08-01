#pragma once
#include <windows.h>

// 세계지도 창을 연다(WORLD.CDS + 현재 함대 위치, 읽기 전용).
void MapWin_Show(HWND owner, HINSTANCE hinst);

// 게임 창을 찾아 "파일" 메뉴에 "지도" 항목을 설치한다(백그라운드 스레드). DllMain 에서 호출.
void MapMenu_Init(HINSTANCE hinst);
