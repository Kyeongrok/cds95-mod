#pragma once
#include <windows.h>

// "소지품" 탭 — 주인공의 소지금 / 소지 아이템 16칸 / 보관 아이템 99칸.
// 실행 중인 게임 메모리에 바로 쓴다(세이브 파일은 안 건드린다).
// 창의 나머지(테두리/타이틀/탭바)는 character.c 가 그린다.

void Inv_Activate(HWND h, int active);
void Inv_Paint(HDC dc);
int  Inv_Click(HWND h, POINT pt);
int  Inv_Key(HWND h, WPARAM wp);
void Inv_Wheel(HWND h, int notches);
