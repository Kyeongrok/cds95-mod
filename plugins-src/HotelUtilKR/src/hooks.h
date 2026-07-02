#pragma once
#include <windows.h>

// gameVersion: GetGameVersion()이 반환한 값 (참고용 로그). 실제 게이팅은 시그니처 검증으로 함.
void InstallHooks(DWORD gameVersion);
void RemoveHooks(void);
