#pragma once
#include <windows.h>

// 도시 그림 액자 — 게임 폴더의 CITYFRM.CDS(LS12 아카이브, 파트 2개).
//
//   파트 0    139,776바이트 = 416x336, 8bpp 색인   구리빛 액자
//   파트 1    139,776바이트 = 416x336, 8bpp 색인   청회색 액자
//
// 도시 그림(CITYCG.CDS)이 400x320 이니 사방 8px 만 크다. 실제로 안쪽 400x320 자리가
// 한 점도 빠짐없이 색인 74 였다 — 그 구멍에 도시 그림이 들어가고 액자만 위에 얹힌다.
// 바깥 한 겹(1,499픽셀 ≈ 둘레 1,500)도 74 라 액자 무늬 자체는 414x334 에 그려져 있다.
//
// 팔레트 파트가 따로 없다. 색인이 12~74 라 전부 게임 공용 색표(game_palette.h)로 풀린다.
// 파일이 14KB 밖에 안 되지만 CITYCG.CDS 와 짝이라 여닫는 자리를 맞춰 둔다.

#define CITYFRM_W      416
#define CITYFRM_H      336
#define CITYFRM_SZ     (CITYFRM_W * CITYFRM_H)
#define CITYFRM_MARGIN 8      // 도시 그림이 앉는 자리까지의 여백(사방 같다)

void CityFrm_Load(void);      // CITYFRM.CDS 를 연다(이미 열었으면 아무 것도 안 한다)
void CityFrm_Free(void);      // 파일 버퍼를 놓는다
int  CityFrm_Count(void);     // 액자 수. 0 이면 파일을 못 열었다

// 도시 그림 pic 을 액자 frame 에 끼워 (x,y,w,h) 에 그린다. w,h 는 액자를 포함한 크기라
// 원본 그대로 놓으려면 CITYFRM_W x CITYFRM_H 를 준다.
//
// 액자 바깥 한 겹은 뚫려 있으므로 그 자리에 back 을 칠한다 — 부르는 쪽 바탕색을 주면
// 티가 나지 않는다(단색 바탕 위에 그린다는 전제다).
// 액자를 못 읽으면 그림만 (x,y,w,h) 에 그린다 — 액자는 덤이지 없으면 안 되는 것이 아니다.
// 그림 자체를 못 풀면 아무 것도 안 그리고 0.
int  CityFrm_Draw(HDC dc, int x, int y, int w, int h, int pic, int frame, COLORREF back);
