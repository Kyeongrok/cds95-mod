#pragma once
#include <windows.h>

// SaveUtilKR — 자택·여관까지 가지 않고 그 자리에서 저장하고 중단한다.
//
// 게임은 저장을 자택·여관 메뉴에만 달아 두었을 뿐, 저장 함수 자체에는 그런 조건이 없다.
// 그래서 같은 함수를 메뉴(그리고 단축키)에서 그대로 부른다.
void SaveUtilKR_Init(HINSTANCE hinst);
