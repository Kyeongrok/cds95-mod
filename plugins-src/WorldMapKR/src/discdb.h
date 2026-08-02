#pragma once
#include <windows.h>

// 지도에 찍을 발견물 목록.
//
// 기본값은 소스에 구워 둔 표(discovery_coords.h — CDS_95.EXE 의 발견물 표에서 뽑은 게임
// 원본 좌표)이고, 플러그인과 같은 폴더(CDS95Util\)에 discoveries.json 이 있으면 그 값으로
// 덮어쓴다. citydb 가 cities.json 을 다루는 방식과 같다 — 고치고 R 을 누르면 바로 반영된다.
//
// discoveries.json 스키마:
//   [ { "id": 0, "name": "희망봉", "x1": 1386, "y1": 866, "x2": 1391, "y2": 867 }, ... ]
// id 는 게임 발견물 번호(0~273), x/y 는 WORLD.CDS 를 펼친 배열의 칸 번호다
// (x 0~2499, y 0~1249 — 함대·도시가 쓰는 원본값의 1/16).
// x1 이 없거나 음수면 그 발견물은 마커를 찍지 않는다.
// x2/y2 를 안 적으면 x1/y1 과 같은 것으로 본다(점 하나).

#define DISCDB_MAX  274
#define DISCDB_NONE (-1)

// 너무 큰 범위는 지도를 뒤덮어 다른 마커를 못 보게 만든다. 긴 변이 이 값을 넘으면
// 가운데를 기준으로 이만큼만 그린다(원래 값은 건드리지 않는다 — 그리기만 줄인다).
#define DISCDB_MAX_SPAN 300

typedef struct {
    int     x1, y1, x2, y2;   // 칸 번호. x1 == DISCDB_NONE 이면 좌표로 찾는 발견물이 아니다
    wchar_t name[40];
} DiscPt;

// 구운 표를 깔고 discoveries.json 이 있으면 덮어쓴다. 여러 번 불러도 된다(다시 읽기 겸용).
void DiscDb_Load(HINSTANCE hinst);

int  DiscDb_FromFile(void);   // discoveries.json 을 실제로 읽었으면 1
int  DiscDb_Marked(void);     // 좌표가 있어 마커를 찍을 발견물 수
const DiscPt* DiscDb_At(int i);

// 그릴 범위를 DISCDB_MAX_SPAN 안으로 줄여 돌려준다. 좌표가 없으면 0.
int  DiscDb_DrawBox(int i, int* x1, int* y1, int* x2, int* y2);
