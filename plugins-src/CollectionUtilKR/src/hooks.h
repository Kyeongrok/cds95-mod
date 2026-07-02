#pragma once
#include <windows.h>

// gameVersion: GetGameVersion()이 반환한 값 (예: 1200 = 한국어판 Ver.1.2.0.0)
// 알고 있는 버전이면 후킹을 설치하고, 모르는 버전이면 아무 것도 하지 않습니다(안전하게 무시).
void InstallHooks(DWORD gameVersion);
void RemoveHooks(void);
