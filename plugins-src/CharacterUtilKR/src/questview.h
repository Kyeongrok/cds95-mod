#pragma once
#include <windows.h>

// "퀘스트" 탭 — 주인공 직업 이벤트 목록과 완료 여부. 읽기 전용.
// 창의 나머지(테두리/타이틀/탭바)는 character.c 가 그린다.

void Quest_Activate(HWND h, int active);   // 탭을 켤 때 세이브 + 이벤트 파일을 읽는다
void Quest_Paint(HDC dc);
int  Quest_Click(HWND h, POINT pt);        // 처리했으면 1
int  Quest_Key(HWND h, WPARAM wp);         // 처리했으면 1
void Quest_Wheel(HWND h, int notches);
