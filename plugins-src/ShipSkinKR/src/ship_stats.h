#pragma once
#include <windows.h>

// CDS_95.EXE 의 함선 성능표. .rdata 안에 있는 정적 데이터라 파일에도 들어 있다.
// 파일오프셋 0xFA7E0 부터 64바이트씩 8종 — RVA 0xFC1E0 (VA 0x4FC1E0).
// (.rdata 는 raw 0xC1600 / RVA 0xC3000 이므로 RVA = 파일오프셋 + 0x1A00.
//  .text 식으로 잘못 계산해 0xFB3E0 을 썼다가 표를 못 찾은 적이 있다.)
//
// 레코드 배치 (전부 4바이트. 올려받은 함선 자료의 수치와 8종 전부 대조해 확인):
//   +0x00 이름 문자열 포인터
//   +0x0C 추진력 현재  +0x10 추진력 최대
//   +0x14 내구성 현재  +0x18 내구성 최대
//   +0x1C 중량   현재  +0x20 중량   최대
//   +0x24 용량   현재  +0x28 용량   최대
//   +0x2C 포탑   현재  +0x30 포탑   최대
//   +0x34 최대선원(코드) — 실제 선원수 = 50 + 값 x 5
//   +0x38 필요자금(코드) — 실제 자금   = 값 x 1000
//   +0x08 등장시기(코드) — 아래 참고
//   +0x04 / +0x3C 은 아직 무엇인지 모른다(+0x04 를 읽는 코드가 아예 없다).
//
// 등장시기(+0x08)는 0x42A390 에서 이렇게 쓰인다:
//     ecx = 5*도시규모(도시struct +0x08) - 1475 + 현재연도[0x5A4D20]
//     if (표[+0x08] >= ecx) 이 함선은 아직 안 나온다
// 즉 등장 조건은  현재연도 >= 1475 + 값 - 5 x 도시규모  이고, 규모가 한 단계 클수록
// 5년씩 일찍 나온다. 값 1 = 1년이다.
// 0x4AD8D4 / 0x4ADE9E 에서는 그 차이의 홀짝으로 조선소가 어느 배를 취급할지 가른다.
// 화면에는 규모 1 기준(= 1470 + 값)으로 보여준다 — 코구/다우 1480, 카라벨 1485,
// 대형카라벨 1495, 카락 1505, 대형카락 1515, 중카락 1525, 갤리온 1535.
// (코구가 1480년에 어디서나 팔린다는 사실이 규모 1 기준선과 정확히 맞는다.)

#define SHIPSTAT_RVA   0xFC1E0u
#define SHIPSTAT_SIZE  0x40
#define SHIPSTAT_N     8            // 코구 카라벨 대형카라벨 카락 대형카락 중카락 갤리온 다우

// 칸 종류. 앞 다섯은 현재/최대 두 값, 뒤 둘은 값 하나뿐이다.
#define SF_SPEED 0
#define SF_HULL  1
#define SF_MASS  2
#define SF_CAP   3
#define SF_GUN   4
#define SF_CREW  5
#define SF_COST  6
#define SF_YEAR  7
#define SHIPSTAT_FIELD_N 8

int  ShipStat_Load(void);                  // 표를 찾아 검사한다. 성공 1
int  ShipStat_Ready(void);

const wchar_t* ShipStat_Name(int ship);
const wchar_t* ShipStat_FieldName(int field);
int  ShipStat_HasMax(int field);           // 현재/최대 두 값을 갖는 칸인가

// hi = 0 현재 / 1 최대. 값 하나뿐인 칸은 hi 를 무시한다.
int  ShipStat_Get(int ship, int field, int hi);        // 화면에 보이는 값(코드 아님)
int  ShipStat_Set(int ship, int field, int hi, int v); // 화면 값으로 쓴다. 성공 1
int  ShipStat_Step(int field);                         // 휠 한 칸에 움직일 양

void ShipStat_Restore(void);               // 창을 열 때 떠 둔 원래 값으로 되돌린다

// 조선소가 취급할 함선은 "1월 1일"에만 다시 계산된다 — 달력 처리 0x44B174 가 해를 넘길 때
// 0x44B3D0 을 부르고, 그 안에서 226개 도시를 돌며 0x42A340 으로 목록을 다시 만든다.
// 결과는 도시 객체(세이브에 들어감)에 굳으므로, 등장시기를 고쳐도 새해가 오기 전에는
// 조선소 목록이 그대로다(세이브를 불러오기 전에 고쳐도 마찬가지 — 세이브의 값이 덮어쓴다).
// 아래는 그 갱신 중 "도시별 함선 재계산"만 그대로 떼어 226곳에 직접 돌린다.
// 연간 처리의 나머지(인물·후원자 갱신 등)는 건드리지 않는다.
// 돌려주는 값은 갱신한 도시 수. 아직 게임에 안 들어갔으면 0.
int ShipStat_RefreshYards(void);
