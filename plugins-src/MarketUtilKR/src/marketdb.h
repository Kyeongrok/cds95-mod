#pragma once
#include <windows.h>

// 교역소 매매 — 실행 중인 cds_95 모듈에서 읽고, 살 때만 쓴다(파일은 안 건드린다).
//
// 자리와 규칙은 전부 게임 코드를 읽어 확정했다(0x480CC0 BuildSaleList 계열).
//
//   도시 struct   모듈 + 0x1863A8 + id*92
//     +0x0C 시세  +0x10 특산품 종류  +0x14 특산품 기준가  +0x18 특산품 재고
//     +0x40 도시상태  +0x44~0x54 공통 교역품 1~5 재고  +0x58 문화권
//   도시 record   모듈 + 0x0D14B0 + id*136
//     +0x10 · +0x14 = 연결 내륙도시 id(-1 없음)   +0x1C = 교역품 지역(0~33)
//     (세비야 record 가 4=톨레도, 카디스가 6=코르도바 — 게임의 "○○산" 수입품이 여기서 온다)
//   지역 공통품   모듈 + 0x0DF0E0 + 지역*20  (i32 x5, -1 끝)
//   지역 기준가   모듈 + 0x0DCBBC + (지역 + 34*종류)*4        ★ 70종 x 34지역 격자
//   판매 게이트   모듈 + 0x18BAB0 + 종류*4  (0 이면 그 교역품은 안 판다)
//   내 짐         모듈 + 0x1B397C + i*16   {종류, 갯수, 원산지, 내구도}
//                 빈 칸은 -1 0 -1 0 이다(다 팔고 확인). 앞에서부터 채워진다.
//   소지금        모듈 + 0x1B6194
//
// 단가 = 기준가 x 3 / 2.
//   · 공통품은 지역 기준가 표에서 (0x42E3C0 이 그렇게 읽는다)
//   · 도시 특산품과 수입품은 그 도시 struct 의 +0x14 에서 (0x480E2F 가 그 값을 넘긴다)
// 게임 화면과 대조: 돌소금 22->33, 올리브유 18->27, 총 80->120, 쇠고기 20->30 이 딱 맞고
// 대포만 160->240 인데 화면은 241 이다(품목 분류별 보정이 하나 더 있는 듯 — 아직 못 읽었다).

#define MKT_CITY_RVA    0x1863A8u
#define MKT_CITY_SZ     92
#define MKT_CITY_N      226
#define MKT_REC_RVA     0x0D14B0u
#define MKT_REC_SZ      136
#define MKT_REC_REGION  0x1C
#define MKT_REC_INLAND  0x10          // +0x10, +0x14 두 칸
#define MKT_COMMON_RVA  0x0DF0E0u     // 지역별 공통 교역품 5칸
#define MKT_PRICE_RVA   0x0DCBBCu     // 지역 기준가 격자
#define MKT_GATE_RVA    0x18BAB0u
#define MKT_CARGO_RVA   0x1B397Cu
#define MKT_MONEY_RVA   0x1B6194u
#define MKT_REGION_N    34
#define MKT_GOODS_N     70
#define MKT_CARGO_N     32            // 짐 칸을 이만큼만 본다(그 뒤는 안 건드린다)
#define MKT_COND_MAX    100           // 내구도. 이 범위를 벗어나면 짐 칸이 아니다

typedef struct {
    int kind;        // 교역품 종류(0~69)
    int price;       // 단가
    int supply;      // 공급량
    int origin;      // 원산지 도시 id
    int slot;        // 공통품이면 재고 칸 0~4, 특산품/수입품이면 -1
} MktRow;

typedef struct {
    int kind, count, origin, cond;
    int slot;        // 짐 배열의 칸 번호
} MktCargo;

int  Mkt_Ready(void);                  // 모듈을 잡았나
int  Mkt_CurrentCity(void);            // 지금 정박한 도시. 항해 중이면 -1
int  Mkt_Money(void);

int  Mkt_BuildList(int city);          // 그 도시가 파는 것을 모은다. 개수
const MktRow* Mkt_At(int i);

int  Mkt_LoadCargo(void);              // 내 짐을 훑는다. 개수

// 짐 자리가 맞는지 눈으로 보려고 — 그 자리의 날값 네 개를 그대로 돌려준다.
void Mkt_CargoRaw(int slot, int out[4]);
const MktCargo* Mkt_CargoAt(int i);

// 그 줄을 qty 만큼 산다. 성공하면 쓴 돈, 실패면 음수.
#define MKT_E_MONEY   (-1)   // 소지금 부족
#define MKT_E_FULL    (-2)   // 짐 칸이 없다
#define MKT_E_SUPPLY  (-3)   // 공급량이 모자라다
#define MKT_E_READ    (-4)   // 자리를 못 읽었다
int  Mkt_Buy(int city, int row, int qty);

// 매각가. 게임의 매각 공식은 아직 못 읽었다(짐 배열을 베이스 포인터로 다뤄 xref 가 없다).
// 지금은 대칭 규칙으로 어림한다 — 그 도시 기준가 x 시세/100.
//   사는 값은 기준가의 1.5배이므로, 산 곳에서 도로 팔면 손해가 나고
//   기준가가 높은 지역으로 옮겨 팔면 이문이 남는다(게임과 같은 방향).
// 게임 매각창 숫자를 알려 주면 그대로 맞추겠다.
int  Mkt_SellPrice(int city, int kind);

// 그 도시에서 그 교역품을 살 때의 단가. 짐 칸에는 산 값이 안 남으므로(종류·갯수·원산지·
// 내구도 넷뿐이다) 원산지 도시를 넣어 "원산지에서 샀다면 얼마" 를 셈하는 데 쓴다.
int  Mkt_BuyPriceAt(int city, int kind);
int  Mkt_Sell(int city, int cargoIndex, int qty);   // 번 돈. 실패면 음수

const wchar_t* Mkt_GoodsName(int kind);
const wchar_t* Mkt_CityName(int city);
int  Mkt_GoodsPic(int kind);           // ITEM.CDS 그림 번호
