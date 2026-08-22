#pragma once
#include <windows.h>

// "모드" 창 — 파일 메뉴 아래로 늘어져 있던 플러그인 항목을 단추로 세워 보여준다.
// modMenu 는 그 항목들이 담긴 메뉴(등록부)다. 창은 그것을 읽기만 하고 고치지 않는다.
void ModWin_Show(HWND owner, HMENU modMenu, HINSTANCE hinst);

// 창이 떠 있나. 메뉴를 다시 가로챌 때 두 번 열지 않으려고 본다.
int  ModWin_IsOpen(void);
