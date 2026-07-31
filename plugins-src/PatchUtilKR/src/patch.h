#pragma once
#include <windows.h>

// PatchUtilKR — cds-helper의 ExePatch(정적 파일 헥스 패치)를 런타임 메모리 패치로 옮긴 플러그인.
//  - patches.json (cds-helper 커스텀 패치와 동일 스키마)을 읽어
//  - 파일 오프셋 → 로드된 cds_95 모듈의 가상주소로 변환 후
//  - VirtualProtect + 메모리 쓰기로 적용/해제(원본 스냅샷 복원).
//  - 게임 "파일(F)" 메뉴에 "패치" 항목을 달고, 클릭하면 목록 창(체크=적용).
void PatchKR_Init(HINSTANCE hinst);
