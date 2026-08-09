#pragma once
#include <windows.h>

// HotkeyUtilKR — 글자 한 개로 KR 플러그인 창을 연다.
//
// 하는 일은 하나뿐이다: 눌린 키를 보고 게임 창에 WM_COMMAND(그 기능의 메뉴 ID)를 던진다.
// 창을 여는 것은 늘 그 기능을 가진 플러그인이다(그쪽이 게임 창을 서브클래싱해 ID 를 가로챈다).
// 그래서 어떤 플러그인이 빠져 있으면 그 키는 그냥 아무 일도 일으키지 않는다.
//
// 키는 CDS95Util\hotkeys.json 에 남는다. 창에서 줄을 고르고 키를 누르면 바로 저장된다.

void HotkeyKR_Init(HINSTANCE hinst);
