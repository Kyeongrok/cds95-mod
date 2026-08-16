#pragma once
#include <windows.h>

// 배 그림 — 게임 폴더의 SHIPSTIL.CDS (LS12 아카이브, 파트 26개).
//
//   파트 0        768바이트 = 팔레트 256색, 한 색이 3바이트
//   파트 1~6   각 76800바이트 = 320x240, 8bpp 색인, 위에서 아래로   ← 배 그림 6장
//   파트 7~25    크기 제각각 — 아직 안 봤다(돛·선수상 덧그림으로 보인다)
//
// 색인 v 를 색으로 바꾸는 규칙 (6장을 다 풀어 확인했다):
//   그림에 나오는 색인이 10~245 (236가지)이고 팔레트에서 값이 든 칸이 0~235 (236개)라
//   **색인 v -> 팔레트 v-10** 으로 딱 맞아떨어진다. ITEM.CDS(색인 160~245 가 제 팔레트)
//   와 오프셋만 다르고 나머지는 같다.
//   한 색의 3바이트는 다른 CDS 와 같이 (파랑, 빨강, 초록) 순이다.
//   색인 74 (= 팔레트 64, fc 00 fc 마젠타) 가 투명 자리다 — 배경으로 깔린다.

#define SHIPSTIL_W   320
#define SHIPSTIL_H   240
#define SHIPSTIL_SZ  (SHIPSTIL_W * SHIPSTIL_H)
#define SHIPSTIL_N   6            // 파트 1~6
#define SHIPSTIL_PAL_BASE 10      // 색인 - 이 값 = 팔레트 칸
#define SHIPSTIL_KEY 74           // 투명 색인

void ShipStil_Load(void);
void ShipStil_Free(void);
int  ShipStil_Count(void);        // 6. 0 이면 파일을 못 열었다

// pic(0~5) 을 (x,y,w,h) 에 늘려 그린다. 투명 자리는 bg 색으로 채운다. 성공 1.
int  ShipStil_Draw(HDC dc, int x, int y, int w, int h, int pic, COLORREF bg);
