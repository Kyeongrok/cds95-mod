#pragma once
#include <windows.h>

// FatigueUtilKR — 함대 피로도를 원하는 만큼 덜어내는 작은 창.
//
//   파일 > 피로도  →  [줄일 값: 20] [줄이기]
//
// 값 하나만 만지므로 후킹은 없다. 게임이 쓰는 자리(모듈 베이스 + 0x1B3950)를 그대로
// 읽고 쓴다. 그 자리는 ce/CDS_95.CT 의 "함대 정보 > 피로도"(CDS_95.EXE+1B3950, 4바이트)다.
void FatigueUtilKR_Init(HINSTANCE hinst);
