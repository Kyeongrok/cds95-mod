#pragma once
#include <windows.h>

// 게임 창을 찾아 상단 메뉴에 "인물" 항목을 설치한다(백그라운드 스레드). DllMain 에서 호출.
void CharKR_Init(HINSTANCE hinst);

// 인물(얼굴) 코드 브라우저 창을 연다(세피아 프레임 + 컬러 얼굴, ◀▶ 코드 이동, 남/여 전환).
void CharKR_ShowWindow(HWND owner, HINSTANCE hinst);

// 탭(character.c 의 TAB_*)을 지정해 연다. 이미 떠 있으면 그 탭으로 옮긴다.
// 게임 창에 WM_COMMAND(0xB310 + 탭번호)를 던져도 같다 — 단축키 플러그인이 그 길로 부른다.
void CharKR_ShowTab(HWND owner, HINSTANCE hinst, int tab);
