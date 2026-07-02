#pragma once
#include <windows.h>

// CDS95.exe의 FileVersion을 "1270" 형식(major*1000 + minor*100 + build*10 + revision)의
// 정수로 반환합니다. 기존 문서의 CDS95Patch1270.txt 같은 파일명 규칙과 동일한 형식입니다.
// 버전 정보를 읽지 못하면 0을 반환합니다.
DWORD GetGameVersion(void);
