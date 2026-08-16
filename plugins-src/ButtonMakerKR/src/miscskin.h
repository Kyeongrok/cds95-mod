#pragma once
#include <windows.h>

// 메뉴 띠 껍데기 — 게임 폴더 MISC.CDS 의 파트 4(2,880바이트).
//
// 한 벌이 960바이트고 세 벌이 들어 있다.
//     벌 0  진홍 장식 — 메뉴 **타이틀**
//     벌 1  베이지   — 보통 버튼
//     벌 2  회녹색   — 다른 상태 버튼
// 한 벌 안은 조각 셋이고, 조각마다 제 폭으로 위에서 아래로 담긴 8bpp 색인이다.
//     +0    왼끝    16폭 x 24행 (384바이트)
//     +384  가운데   8폭 x 24행 (192바이트)   ← 폭만큼 옆으로 되풀이한다
//     +576  오른끝  16폭 x 24행 (384바이트)
// 그래서 띠 하나는 늘 24행이고 폭은 16 + 8*n + 16 이다.
//
// 게임도 똑같이 짓는다 — 조각 꺼내기 0x00463710(벌, 조각), 조각 시작 열 표 0x00552898
// {0, 16, 24}, 띠 짓는 자리 0x0041F606 (왼끝 0x41F61B, 가운데 되풀이 0x41F677,
// 오른끝 0x41F6EB). 파트 4 는 0x00463590 이 게임 시작 때 객체 0x005AA3B8+0x14 로 읽어 둔다.
//
// 색은 게임 공용 색표(game_palette.h)로 그대로 풀린다.

#define SKIN_H       24
#define SKIN_CAP_W   16
#define SKIN_MID_W    8
#define SKIN_STYLES   3

int  MiscSkin_Load(void);     // MISC.CDS 를 열어 파트 4 를 푼다(이미 했으면 그대로). 성공 1
void MiscSkin_Free(void);
int  MiscSkin_Ready(void);

// 조각 하나. k = 0 왼끝 / 1 가운데 / 2 오른끝. *w 에 폭을 담아 준다. 없으면 NULL.
const unsigned char* MiscSkin_Piece(int style, int k, int* w);
