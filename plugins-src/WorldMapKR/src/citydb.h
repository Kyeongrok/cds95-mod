#pragma once
#include <windows.h>

// 지도에 찍을 도시 목록.
//
// 기본값은 소스에 구워 둔 표(city_coords.h — 워프 데이터에서 뽑은 게임 원본 좌표)이고,
// 플러그인과 같은 폴더(CDS95Util\)에 cities.json 이 있으면 그 값으로 덮어쓴다.
// PatchUtilKR 이 patches.json 을 다루는 방식과 같다 — 파일을 고치고 R 을 누르면 바로 반영된다.
//
// cities.json 스키마(cds-helper 것과 같은 모양):
//   [ { "id": 0, "name": "리스본", "latitude": 38.52, "longitude": -9.29, "hasLibrary": true }, ... ]
// id 는 게임 도시 번호(0~225), 위경도는 도 단위(소수 셋째 자리까지 읽는다).
// 위경도 둘 중 하나라도 없으면(null) 그 도시는 마커를 찍지 않는다.
// hasLibrary 가 있으면 그 값을 쓰고, 없으면 TradeUtilKR 의 kCities 쪽 값을 그대로 쓴다.
//
// 안에서는 게임 원본 단위로 들고 있다 — 함대 마커가 읽는 값과 같은 단위라 둘을 같은 식으로 찍는다.
//   경도 0=서경180, 20000=0, 40000=동경180 / 위도 0=북위90, 10000=0, 20000=남위90

#define CITYDB_MAX  226
#define CITYDB_NONE (-1)

typedef struct {
    int     lonRaw, latRaw;   // 게임 원본 단위. lonRaw == CITYDB_NONE 이면 좌표를 모르는 도시
    int     lib;              // 도서관이 있는 도시면 1
    wchar_t name[40];
} CityPt;

// 구운 표를 깔고 cities.json 이 있으면 덮어쓴다. 여러 번 불러도 된다(다시 읽기 겸용).
void CityDb_Load(HINSTANCE hinst);

int  CityDb_FromFile(void);   // cities.json 을 실제로 읽었으면 1
int  CityDb_Marked(void);     // 좌표가 있어 마커를 찍을 도시 수
const CityPt* CityDb_At(int i);
