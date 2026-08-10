#pragma once
#include <windows.h>

// MarketUtilKR — "파일 > 매매". 교역소에서 파는 것을 그림과 함께 보여 주고,
// 줄을 두 번 누르면 공급량의 절반을 산다. 읽기는 실행 중 메모리, 쓰기는
// 소지금 · 재고 · 짐 세 군데뿐이다(세이브 파일은 안 건드린다).

void MarketKR_Init(HINSTANCE hinst);
