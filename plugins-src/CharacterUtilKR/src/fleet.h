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
