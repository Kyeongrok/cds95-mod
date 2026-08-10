#pragma once
#include <windows.h>

// 힌트 한 줄을 고르면 그 분류를 좋아하는 후원자를 추려 준다.
//
// 후원자 표(이름·도시·직업·자금·얼굴·취향)는 CharacterUtilKR 의 chardb.c 를 그대로 같이
// 빌드해 쓰고, 초상화는 faces.c(MALE/FEMALE.CDS), 워프는 TradeUtilKR 의 워프 표를 쓴다.
//
// ★ 취향 비트 순서와 발견물 분류 번호는 서로 다르다.
//   취향(chardb.c kPatronPref): 지리 역사 종교 민족 생물 미신 교역품 보물
//   분류(EXE 발견물 표 +0x04):  지리 역사 보물 종교 교역품 미신 생물 민족
//   그래서 아래 표로 옮겨 짚는다.

#define PPICK_MAX 96

int  PPick_Build(int cat);        // 분류(0~7)를 좋아하는 후원자를 모은다. 모은 수를 돌려준다
int  PPick_Count(void);
int  PPick_Row(int i);            // 후원자 표의 행 번호

// 그 후원자가 있는 도시의 워프 번호(TradeUtilKR 의 kWarps 색인). 없으면 -1.
int  PPick_WarpIndex(int i);
