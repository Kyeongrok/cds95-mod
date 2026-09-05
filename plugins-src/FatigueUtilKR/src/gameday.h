#pragma once
#include <windows.h>
#include "fleetmem.h"

// 규율 창이 읽는 게임 자리들 — 달력 · 함대 위치 · 칸 종류 · 기능.
//
// 자리는 옵시디안 `Project/cds95/분석/항해/19.분석-날짜와 시간의 흐름.md` 와
// `70.분석-규율(대원 불만과 하루 셈).md` 에서 왔다. 여기 적은 것은 전부 읽기만 한다.
//
//   달력   0x5A4D18 +0x08 연 · +0x0C 월 · +0x10 일 · +0x14 눈금(0~47, 48눈금이 하루)
//   함대   0x5B63B0 경도(0~40000) · 0x5B63B4 위도(0~20000, 10000 이 적도)
//   말     0x5B61B4 != 0 이면 뭍(말)을 타고 있다 — 하루 셈이 갈린다
//   세계   0x61B2D0 +0xAC 가 세계 칸 배열 — 칸 = [위도/16 * 2500 + 경도/16] (0x426D70)
//   칸종류 0x4CD048[칸값 & 0x3FFF] → 0 근해 1 원양 2 육지 3 산 4 사막 5 강 6 숲
//   눈금표 0x53C330 + 종류*8 의 첫 4바이트 = 그 칸을 지나는 데 드는 눈금
//   이름표 0x56F810 + 종류*8 (cp949 8바이트 고정폭)
//   소지금 0x5B6194 (게임이 함대 물건 +0x5C8 에서 옮겨 둔 거울값)
//   제독   0x5B60A0
//
// ★ 칸 종류는 게임 함수(0x426740)를 부르지 않고 같은 셈을 여기서 한다 — 배열 포인터
//   하나만 따라가면 되고, 그래야 감시 스레드에서 불러도 게임과 부딪히지 않는다.

#define GD_CAL_RVA       0x1A4D18u   // 달력(게임 상태) 객체
#define GD_CAL_YEAR      0x08
#define GD_CAL_MONTH     0x0C
#define GD_CAL_DAY       0x10
#define GD_CAL_TICK      0x14
#define GD_TICKS_PER_DAY 48

// 달력 객체의 첫 워드는 화면 갈래 잣대이기도 하다 — 해상 렌더러(0x48A213)가 이 두 비트를
// 보고 지도를 그릴지 정한다. 도시 안이나 시설 화면에서는 서지 않는다.
// ★ 도시 안에서는 세계 칸 배열이 우리가 아는 그 배열이 아닐 수 있으므로, 이 비트가
//   서지 않으면 지형을 아예 묻지 않는다.
#define GD_SCREEN_MAP_MASK 0x18u

#define GD_LON_RVA       0x1B63B0u
#define GD_LAT_RVA       0x1B63B4u
#define GD_HORSE_RVA     0x1B61B4u   // 0 이 아니면 뭍(말)
#define GD_MONEY_RVA     0x1B6194u

#define GD_WORLD_OBJ_RVA 0x21B2D0u
#define GD_WORLD_PTR_OFF 0xACu
#define GD_WORLD_STRIDE  2500        // 한 행에 칸 2500개(칸 하나가 word)
#define GD_WORLD_ROWS    1250        // 쓰이는 행은 1250 — 위도 0~20000 을 16 으로 나눈 값
#define GD_CELL_LUT_RVA  0x000CD048u
#define GD_TERR_TICK_RVA 0x0013C330u
#define GD_TERR_NAME_RVA 0x0016F810u
#define GD_TERR_N        7           // 0~6 만 실제로 나온다(그 밖은 0xFF)

#define GD_ADMIRAL_RVA   0x1B60A0u

// 기능 번호 — 사람 레코드 +0x40 부터 4바이트씩 놓인다(70번 노트).
#define GD_SKILL_SAIL    0            // 항해술 — 바다의 하루가 본다
#define GD_SKILL_HANDLE  1            // 운용술 — 뭍의 하루가 본다

// 고위도 벌점 — 적도(위도 10000)에서 얼마나 떨어졌나로 0~3. 0x475587~0x4755DC.
// 위도 20000 이 180도이므로 7222·7777·8333 은 대략 65도 · 70도 · 75도다.
#define GD_LAT_EQUATOR   10000
#define GD_LAT_STEP1     0x1C36      // 7222
#define GD_LAT_STEP2     0x1E61      // 7777
#define GD_LAT_STEP3     0x208D      // 8333
