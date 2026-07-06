#pragma once
#include <windows.h>

// cds_95.exe 의 FileVersion 을 "1200" 형식(major*1000+minor*100+build*10+revision)의
// 정수로 반환. 읽지 못하면 0. (HotelUtilKR 에서는 참고 로그용이며 게이팅은 시그니처로 함.)
DWORD GetGameVersion(void);
