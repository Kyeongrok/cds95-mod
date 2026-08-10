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
//   교역품 레코드 모듈 + 0x0DCBBC + 종류*136 (int 34칸)      ★ 70종
//     +0x00~0x6C 지역 기준가 28칸(지역 0~27)   ← 오래 "34지역 격자" 로 알던 자리다
//     +0x70 중량(한 개 무게)  +0x74 ?(대개 7)  +0x78 ?(0/1)
//     +0x7C 이름 문자열 포인터  +0x80 그림 번호  +0x84 ?(0/4/8 — 분류로 보인다)
//     중량은 게임 구입창의 "중량" 칸 그대로다(올리브유 8 · 총 15 · 모직물 3 · 견직물 3 대조).
//   판매 게이트   모듈 + 0x18BAB0 + 종류*4  (0 이면 그 교역품은 안 판다)
//   내 짐         모듈 + 0x1B397C + (배*8 + 칸)*16   {종류, 갯수, 원산지, 내구도}
//                 ★ 배마다 여덟 칸이다 — 한 덩어리 서른두 칸이 아니다.
//                 0번 배가 0~7, 1번 배가 8~15 … 로 이어진다(짐칸 날값으로 확인:
//                 0칸=견직물200 피렌체산, 8칸=밀1 빌바오산 … 게임 매각창은 함대에 편입된
//                 배의 것만 보여 준다. 도크에 둔 배의 짐까지 읽어 엉뚱한 줄이 늘어났었다).
//                 빈 칸은 -1 0 -1 0. 그 배 안에서는 앞에서부터 채워진다.
//   소지금        모듈 + 0x1B6194
//   함선          모듈 + 0x1A4E18 + i*0x6C  (16척. ce/CDS_95.CT "1~16번 함선")
//     +0x00 이름  +0x28 함선종류(0~7)  +0x30 최저승원  +0x34 현재승원
//     +0x38·+0x3C 추진력 현재/최대  +0x40 중량  +0x44 용량(갯수)
//     +0x48·+0x4C 내구도 현재/최대  +0x50 대포수 …
//     빈 칸은 최대내구도가 0 이다. 짐칸은 +0x44 "용량(갯수)" 합으로 본다 —
//     교역품이 갯수로 세는 값이라 짐(갯수 합)과 바로 견줄 수 있다.
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
#define MKT_SHIP_RVA    0x1A4E18u
#define MKT_SHIP_SZ     0x6C
#define MKT_SHIP_N      16
#define MKT_SHIP_TYPE   0x28
#define MKT_SHIP_MASS   0x40          // 중량(무게)
#define MKT_SHIP_CAP    0x44          // 용량(갯수)
#define MKT_SHIP_HULLMX 0x4C          // 최대내구도. 0 이면 빈 칸이다
#define MKT_SHIP_FLEET  0x60          // 함대 편입 표시 — 도크에 둔 배는 -1 이다.
// 배는 열 척까지 갖고 그 중 함대에 넣은 것만 짐을 싣는다(항해는 여덟 척까지).
// 실제로 본 값: 편입된 여섯 칸이 모두 192, 도크에 둔 네 칸이 -1.
// 192 가 무슨 뜻인지는 아직 모르나 도시 id 는 아니다(192번은 사카이라 무관).
// 그래서 "-1 이 아니면 편입" 으로 본다.
#define MKT_CARGO_SLOTS 8             // 배 한 척의 짐 칸 수
#define MKT_CARGO_N     (MKT_SHIP_N * MKT_CARGO_SLOTS)   // 날값으로 볼 수 있는 칸 전체
#define MKT_CARGO_MAX   64            // 목록에 담아 두는 줄 수(함대 여덟 척 x 여덟 칸)
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

// 함대 짐칸 — 성한 배들의 중량 합 · 용량 합과 배 수. 못 읽으면 0 을 돌려준다.
// 게임은 중량 · 용량 둘 중 하나라도 넘으면 안 싣는다.
int  Mkt_Hold(int* ships, int* mass, int* cap);

// 함대 칸(모듈 + 0x1B3950). 짐 배열 바로 앞자리다.
//   +0x00 피로도  +0x04 규칙  +0x08 ?
//   +0x0C 물  +0x10 식량  +0x14 자재  +0x18 포탄        ← 이 넷이 "중량" 쪽 몫
//   +0x1C 물  +0x20 식량  +0x24 자재  +0x28 포탄        ← 같은 넷의 "용량" 쪽 몫
// 실제로 본 값: 중량쪽 5180·5180·0·23, 용량쪽 665·665·0·23 — 자재·포탄은 두 값이 같고
// 물·식량만 7.8배 차이가 난다(물·식량은 무겁고 부피는 작다). 게임 함대 화면이
// "짐중량 8230 / 짐용량 1059" 를 그 비율로 보여 준 것과 맞는다.
#define MKT_FLEET_RVA   0x1B3950u
#define MKT_FLEET_MASS  0x0C          // 물·식량·자재·포탄 넷(중량 몫)
#define MKT_FLEET_VOL   0x1C          // 같은 넷(용량 몫)
int  Mkt_SupplyMass(void);            // 물·식량·자재·포탄이 먹은 중량. 못 읽으면 -1
int  Mkt_SupplyVolume(void);          // 같은 것이 먹은 용량. 못 읽으면 -1

// 지금 도시의 값 몇 개 — 매매 창 머리에 그대로 보여 준다.
int  Mkt_CitySise(int city);          // 시세(도시struct +0x0C). 못 읽으면 -1
int  Mkt_CityState(int city);         // 도시 상태(+0x40). 못 읽으면 -1
int  Mkt_CitySpecial(int city);       // 특산품 종류(+0x10). 없으면 -1

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
int  Mkt_GoodsMass(int kind);          // 한 개 무게(레코드 +0x70). 못 읽으면 -1
#define MKT_GOODS_REC   136            // 교역품 레코드 크기(int 34칸)
#define MKT_GOODS_MASS  0x70           // 그 안의 중량 자리
