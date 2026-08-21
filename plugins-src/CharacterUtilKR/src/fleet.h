#pragma once
#include <windows.h>

// 함대 값 몇 개. 지금은 피로도 하나뿐이다.
//
// 자리는 ce/CDS_95.CT 의 "함대 정보" 묶음 — 피로도 0x5B3950, 규칙 0x5B3954,
// 물 0x5B395C, 식량 0x5B3960 … 으로 이어진다. 절대주소가 아니라 모듈 베이스 + RVA 로
// 잡는다(livechar.c 의 FAME_RVA 와 같은 방식). 소지품과 마찬가지로 세이브를 불러와야
// 채워지는 자리라 읽기 전에 커밋 여부를 본다.
//
// FatigueUtilKR 플러그인도 같은 자리를 쓴다. 별개 DLL 이라 코드를 나눠 쓸 수 없어
// 자리만 양쪽에 적어 두었다 — 한쪽을 고치면 다른 쪽도 같이 봐야 한다.

#define FLEET_FATIGUE_RVA 0x1B3950u
#define FLEET_FATIGUE_MAX 100

int Fleet_Fatigue(void);        // 지금 피로도. 못 읽거나 말이 안 되면 -1
int Fleet_SetFatigue(int v);    // 성공 1

// 지금 데리고 다니는 선원 수 — 육분의를 쓸 때 "한 사람당 얼마" 를 셈하는 데 쓴다.
// 자리는 MarketUtilKR/src/marketdb.h 와 같다(별개 DLL 이라 코드를 못 나눈다. 한쪽을
// 고치면 다른 쪽도 같이 봐야 한다).
//   함대 객체(0x5B3928)의 +0x04 부터 여덟 칸이 배 번호다(-1 이면 빈 칸).
//   배 struct 는 0x5A4E18 + id*0x6C 이고 그 +0x34 가 현재승원이다.
// ★ 배 struct 열여섯 칸을 그냥 훑으면 안 된다 — 판 배도 이름·내구도가 그대로 남아 있어
//   선원 수가 부푼다. 게임도 이 목록만 본다(0x473DB0 · 0x473DC0).
#define FLEET_LIST_RVA   0x1B392Cu
#define FLEET_SHIPS      8
#define FLEET_SHIP_RVA   0x1A4E18u
#define FLEET_SHIP_SZ    0x6Cu
#define FLEET_SHIP_N     16
#define FLEET_SHIP_CREW  0x34u          // 현재승원

int Fleet_Crew(void);           // 함대 배들의 현재승원 합. 못 읽으면 -1
