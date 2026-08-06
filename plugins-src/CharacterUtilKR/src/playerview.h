#pragma once
#include <windows.h>

// "플레이어" 탭 — 주인공의 지금 초상화를 크게 보여주고, 아래 격자에서 골라 바꾼다.
// 창의 나머지(테두리/타이틀/탭바)는 character.c 가 그린다.

void Pl_Activate(HWND h, int active);   // 탭 전환. 켤 때마다 주인공 레코드를 다시 읽는다
void Pl_Paint(HDC dc);
int  Pl_Click(HWND h, POINT pt);        // 처리했으면 1
int  Pl_Key(HWND h, WPARAM wp);         // 처리했으면 1
void Pl_Wheel(HWND h, int notches);
