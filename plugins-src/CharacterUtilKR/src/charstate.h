#pragma once
#include <windows.h>

// 인물 창에서 고친 값을 기억했다가 다음 실행 때 되살린다.
//
// 이 플러그인은 세이브에 아무것도 쓰지 않고 메모리만 고치므로 게임을 끄면 전부 원래대로
// 돌아간다. PatchUtilKR 의 patches.state 와 같은 방식으로, 고친 것을 플러그인 폴더의
// character.state 에 적어 두고 로드될 때 그대로 다시 쓴다.
//
// 되살릴 수 있는 것은 EXE 이미지 안에 있는 표뿐이다:
//   여급 표(.rdata 0x117AF8)  — 생년, 언어
//   스폰서 표(.rdata 0x1228BC) — 등장연도
// 이 둘은 게임이 시작될 때부터 메모리에 있으므로 DllMain 시점에 써 두면 그대로 쓰인다.
//
// 항해사(생년·특기·승무원)는 되살리지 않는다. 그쪽은 .data 뒷부분(BSS, 0x18BF98)이라
// 세이브를 불러와야 비로소 채워지고, 칸 번호도 세이브마다 달라질 수 있어서 로드 시점에
// 미리 써 둘 수가 없다.
//
// 파일 형식은 줄마다 "종류<탭>행<탭>값" (UTF-8):
//   maid.year    12  1502
//   maid.lang    12  16383
//   patron.year   7  1490

// 저장해 둔 값을 표에 다시 쓴다. Maid_Load()/Patron_Load() 뒤에 부른다.
void CharState_Apply(HINSTANCE hinst);

// 지금 표에 들어 있는 값 중 기본값과 다른 것만 골라 적는다. 값을 고칠 때마다 부른다.
void CharState_Save(HINSTANCE hinst);
