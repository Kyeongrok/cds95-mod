#pragma once
#include <windows.h>

// DialogUtilKR — 게임 안 문구를 파일로 갈아 끼우는 플러그인.
//  - CDS95Util\dialogs\*.json (과 mods\<만든이>\dialogs\*.json) 을 읽어
//  - 원문을 EXE 의 데이터 구역에서 찾아 그 자리에 새 글을 쓴다(CP949).
//  - 자리보다 길면 따로 잡은 메모리에 새 글을 두고, 그 문자열을 가리키던
//    포인터(데이터 표 · 코드의 push/mov imm32)를 전부 새 주소로 돌린다.
//  - 게임 "파일 > 모드 > 대사" 에 목록 창이 붙는다. 창에서 다시 읽을 수 있다.
void DialogKR_Init(HINSTANCE hinst);
