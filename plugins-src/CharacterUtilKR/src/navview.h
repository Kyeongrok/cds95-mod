#pragma once
#include <windows.h>

// "항해사 찾기" 탭 — SAVEDATA.CDS 를 읽어 고용 가능한 인물을 특기/레벨로 추려 보여준다.
// 읽기 전용. 창의 나머지(테두리/타이틀/탭바)는 character.c 가 그린다.

void Nav_Init(HWND parent, HINSTANCE hinst);   // 자식 컨트롤(이름 검색칸) 생성
void Nav_Activate(HWND h, int active);         // 탭 전환. 처음 켤 때 세이브를 읽는다
void Nav_Paint(HDC dc);                        // 필터바 + 목록 + 스크롤바
int  Nav_Click(HWND h, POINT pt);              // 처리했으면 1
int  Nav_Key(HWND h, WPARAM wp);               // 처리했으면 1
void Nav_Wheel(HWND h, int notches);
int  Nav_Command(HWND h, WPARAM wp);           // 자식 컨트롤 알림. 처리했으면 1
void Nav_Destroy(void);
