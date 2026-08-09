#pragma once
#include <windows.h>

// 아이템 그림 — 게임 폴더의 ITEM.CDS(LS12 아카이브, 파트 412개).
//
//   파트 2p     14400바이트 = 120x120, 8bpp 색인, 위에서 아래로   (p = 0~205)
//   파트 2p+1     258바이트 = 86색 팔레트, 한 색이 3바이트
//
// 색인 v 를 색으로 바꾸는 규칙(그림 206장을 다 풀어 확인했다):
//   v >= 160 : 이 그림 제 팔레트. k = v-160 자리의 3바이트가 (파랑, 빨강, 초록) 순이다.
//              86색이 색인 160~245 를 딱 채운다(실제로 쓰이는 최대 색인이 245).
//   v <  160 : 게임 공용 색표(game_palette.h). 그림에는 10~73 만 나온다.
// 팔레트 성분은 6비트 DAC 값을 2비트 올린 것이라 최대가 252다. 255 로 늘리지 않는다 —
// faces.c 도 그대로 두므로 초상화와 톤이 맞는다.

#define ITEMPIC_W  120
#define ITEMPIC_H  120
#define ITEMPIC_SZ (ITEMPIC_W * ITEMPIC_H)

void ItemPic_Load(void);    // ITEM.CDS 를 연다(이미 열었으면 아무 것도 안 한다)
int  ItemPic_Count(void);   // 그림 장수. 0 이면 파일을 못 열었다

// pic 을 (x,y,w,h) 에 늘려 그린다. 성공 1.
// 못 풀면 아무 것도 안 그리고 0 — 빈 액자와 안내문은 부르는 쪽 몫이다(창마다 색표가 다르다).
int  ItemPic_Draw(HDC dc, int x, int y, int w, int h, int pic);
