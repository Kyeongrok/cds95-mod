#pragma once
#include <windows.h>

// 도시 그림 — 게임 폴더의 CITYCG.CDS(LS12 아카이브, 파트 452개 = 그림 226장).
// 도시 번호(0~225)가 TradeUtilKR 의 kCities 색인과 그대로 맞는다
// (29=베니스가 물의 도시, 225=원주민 마을로 확인).
//
//   파트 2p     128000바이트 = 400x320, 8bpp 색인, 위에서 아래로   (p = 0~225)
//   파트 2p+1      258바이트 = 86색 팔레트, 한 색이 3바이트
//
// ITEM.CDS(itempic.c)와 같은 구조지만 제 팔레트가 얹히는 자리가 다르다.
// 226장을 다 풀어 보니 색인이 10~159 에만 나오고, 86색이 74~159 를 딱 채운다.
//   v >= 74 : 이 그림 제 팔레트. k = v-74 자리의 3바이트가 (파랑, 빨강, 초록) 순이다.
//   v <  74 : 게임 공용 색표(game_palette.h).
// 팔레트 성분은 6비트 DAC 값을 2비트 올린 것이라 최대가 252다. itempic.c 와 마찬가지로
// 255 로 늘리지 않는다 — 다른 창의 그림과 톤을 맞춘다.
//
// 파일이 20MB 라 창을 닫을 때 CityCg_Free 로 놓는다(늘 물고 있을 이유가 없다).

#define CITYPIC_W  400
#define CITYPIC_H  320
#define CITYPIC_SZ (CITYPIC_W * CITYPIC_H)

void CityCg_Load(void);    // CITYCG.CDS 를 연다(이미 열었으면 아무 것도 안 한다)
void CityCg_Free(void);    // 파일 버퍼를 놓는다. 다음 Load 에서 다시 연다
int  CityCg_Count(void);   // 그림 장수. 0 이면 파일을 못 열었다

// pic 을 (x,y,w,h) 에 늘려 그린다. 성공 1.
// 못 풀면 아무 것도 안 그리고 0 — 빈 액자와 안내문은 부르는 쪽 몫이다.
int  CityCg_Draw(HDC dc, int x, int y, int w, int h, int pic);

// ---- 내보내기 / 넣기 ----
// 넣기는 게임 폴더의 CITYCG.CDS 를 다시 쓴다. faces.c 의 초상화 바꾸기와 같은 절차다.
//   · 맨 처음 한 번 CITYCG.CDS.orig 로 원본을 남긴다(이미 있으면 그대로 둔다)
//   · 그림 파트와 팔레트 파트 둘만 새로 인코딩하고 나머지 450개는 압축 바이트를 그대로 옮긴다
//   · 쓰기 전에 새 파트를 도로 풀어 원본과 같은지 확인한다. 어긋나면 파일을 건드리지 않는다
//   · 성공하면 파일을 다시 열어 창에 바로 보이게 한다
//
// 넣는 그림은 400x320 으로 늘려 담고, 색은 공용 색표 + 새로 짠 86색 팔레트로 되돌린다
// (quant.h 참고). 내보낸 PNG 를 그대로 다시 넣으면 한 점도 안 틀린다.
#define CITYPIC_ERR_OK      0
#define CITYPIC_ERR_GDIP    1   // gdiplus.dll 을 못 씀 — PNG 를 못 읽고 못 씀
#define CITYPIC_ERR_IMAGE   2   // 그림 파일을 못 열었다
#define CITYPIC_ERR_ARCHIVE 3   // CITYCG.CDS 가 안 열려 있다
#define CITYPIC_ERR_ENCODE  4   // 다시 묶다가 실패(사전이 순열이 아니거나 버퍼 부족)
#define CITYPIC_ERR_VERIFY  5   // 새로 만든 것이 도로 안 풀린다 — 파일은 안 건드렸다
#define CITYPIC_ERR_WRITE   6   // 파일 쓰기 실패(게임이 파일을 쥐고 있을 수 있다)
#define CITYPIC_ERR_RANGE   7   // 그림 번호가 표 밖

// 도시 그림 한 장을 PNG 로 내보낸다(400x320). 성공 CITYPIC_ERR_OK.
int  CityCg_ExportPng(int pic, const wchar_t* path);

// 그림 파일(PNG/BMP/JPG/GIF)을 400x320 으로 맞춰 그 자리 그림을 갈아 끼운다.
// exact 가 NULL 이 아니면 색을 한 점도 안 틀리게 넣었는지(1) 근사했는지(0)를 담아 준다.
int  CityCg_ImportPng(int pic, const wchar_t* path, int* exact);
