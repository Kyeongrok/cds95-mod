#pragma once
#include <windows.h>

// ShipInfoKR — 선체 8종 도감.
//
// 선체 정적표  .rdata  VA 0x4FC1E0, 0x40 간격 x 8행
//   +0x00 이름 ptr   +0x0C 추진력   +0x14 내구력   +0x1C 적재중량
//   +0x24 적재용량   +0x2C 대포수   +0x34 필요승인 - 10
//
//   자리는 조선소 구입 창에서 되짚었다. 0x422CA0 이 그 도시가 파는 선체를 모으고
//   0x422D10 이 한 줄을 서식 0x53C1DC("%12s %6d %6d %8d %8d %8d %6d") 로 찍는데,
//   그때 읽는 주소가 그대로 이 자리들이다. 갤리온 70/55/375/3500/40/24 가
//   게임 창과 한 자리도 안 틀린다.
//
// 도시가 파는 선체  도시배열 VA 0x5863A8, 92(0x5C) 간격, +0x1E 워드의 비트 0~7
//   (0x429950 이 도시 번호로 그 자리를 계산한다. 0x422CA0 이 7 -> 0 순으로 훑어서
//    구입 목록이 갤리온부터 나온다.)

#define HULL_N        8
#define HULL_RVA      0x000FC1E0u   // VA 0x4FC1E0 (.rdata)
#define HULL_SIZE     0x40
#define HL_NAME       0x00
#define HL_SPEED      0x0C
#define HL_DURA       0x14
#define HL_WEIGHT     0x1C
#define HL_VOLUME     0x24
#define HL_GUNS       0x2C
#define HL_CREW       0x34
#define HULL_CREW_ADD 10            // 게임이 이 값에 10 을 더해 "필요승인" 으로 보여준다

#define CITY_ARR_RVA  0x001863A8u   // VA 0x5863A8 (.data 0채움 대역 — 세이브를 불러와야 선다)
#define CITY_ARR_SZ   0x5C
#define CITY_SHIP_OFF 0x1E          // word — 비트 k 가 서면 선체 k 를 판다

int Hull_Load(void);
int Hull_Ready(void);
#define HULLDB_OK       0
#define HULLDB_E_MODULE 1
#define HULLDB_E_READ   2
#define HULLDB_E_ROWS   3
#define HULLDB_E_TAIL   4
int Hull_Status(void);

const wchar_t* Hull_Name(int k);
int Hull_Dura(int k);
int Hull_Speed(int k);
int Hull_Volume(int k);
int Hull_Weight(int k);
int Hull_Crew(int k);      // 화면에 나오는 값(표값 + 10)
int Hull_Guns(int k);

// 그 도시가 선체 k 를 파나. 세이브 전이면 -1
int Hull_CitySells(int city, int k);
int Hull_CitiesReady(void);
