#pragma once
#include <windows.h>

// ModUtilKR — 어떤 플러그인을 쓸지 체크박스로 고른다.
//
// 플러그인은 ddraw.dll(DDrawWrapper)이 게임이 뜰 때 CDS95Util\*.plugin 을 훑어 불러오고,
// 한 번 불러온 것을 내리는 길이 없다. 그래서 "끄기"는 확장자를 .plugin.off 로 바꿔
// 다음 실행 때 로더 눈에 안 띄게 하는 것이다(켜면 .plugin 으로 되돌린다).
// 게임이 실행 중이어도 이름은 바꿀 수 있다 — 로더가 FILE_SHARE_DELETE 로 열어 두기 때문이다.
// 따라서 반영은 항상 다음 실행부터다.
//
// 설정 파일은 없다. CDS95Util 폴더에 있는 파일이 곧 목록이다.

void ModKR_Init(HINSTANCE hinst);
