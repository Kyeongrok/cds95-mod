#pragma once
#include <windows.h>

// 규율 창 — 지금 함대 규율이 얼마인지 본다(보기만 한다. 고치지 않는다).
//
//   파일 > 규율  →  규율 87/100 · 날짜 · 지금 칸 지형 · 항해술/운용술 · 하루 셈 · 변한 자리
//
// 게임 화면 어디에도 이 숫자가 안 나와서 만든 창이다. 자리는 fleetmem.h 참고.
// 메뉴 · 단축키를 받는 쪽은 fatigue.c 다(같은 DLL 이라 메뉴 감시 스레드를 나눠 쓴다).
void Discipline_Init(HINSTANCE hinst);                  // 규율이 바뀌는 자리를 적는 감시 스레드
// 게임 창을 찾은 쪽이 한 번 불러 준다 — 지난 판에 오버레이를 켜 뒀으면 그때 다시 켠다.
void Discipline_RestoreOverlay(HINSTANCE hinst, HWND gameHwnd);
void Discipline_Show(HINSTANCE hinst, HWND gameHwnd);
