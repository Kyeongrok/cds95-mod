#pragma once
#include <windows.h>

// 육상전 모의전 — 게임의 육상전 한 판을 아무 때나 불러서 붙어 본다.
// (분석: 옵시디안 `Project/cds95/분석/전투/65.분석-육상전.md`)
//
//   한 판 부르기   VA 0x0044AA30  __cdecl(갈래, 아군, 적, 도시, 싸움터)
//                  0 이김 · 1 물러남 · 2 몰살. 이 함수는 그 결과로 아무것도 하지 않는다 —
//                  도시 점령도 GAME OVER 도 부르는 쪽(0x00468990 · 0x0048C022)이 한다.
//                  그래서 여기서 부르면 판만 벌어지고 판 밖은 그대로다.
//   싸움터 인자    7 도시 · 2 초지 · 4 황무지 · 그 밖 숲 (0x0044A624 벌)
//
//   ★ 갈래 2 — 도시 방어부대
//     0x0044AA30(2, 0x005AA2B8, 0, 도시레코드, 싸움터)
//     적은 그 자리에서 지어낸다 — 대장 능력은 도시 규모(0x00449E50), 병력은
//     50x규모² + 100 + rand(50) (규모 2 이하면 곱하기 2, 0x004A11D0),
//     진형은 도시 문화권(0x004A1320)이다.
//
//   ★ 갈래 1 — 필드 부대 (뭍을 걷다 마주치는 그 부대)
//     0x0048BE80 이 지금 자리를 지역 여덟(표 0x00569EC0, 32바이트)에서 찾고,
//     그 안에서 벌 둘 중 하나를 굴려 이렇게 부른다.
//         대장 인물번호 = 246 + 지역*2 + 벌      (0x0048BF1D 의 `0xF6`)
//         병력          = 표[+0x10 + 벌*8] + rand(표[+0x14 + 벌*8])
//         0x0044AA30(1, 0x005AA2B8, &적함대, 0, 지금 지형)
//     적함대는 열여섯 바이트짜리 작은 물건이다.
//         +0x00 함수표 0x004C3620 · +0x04 인물 id · +0x08 0 · +0x0C 병력
//     진형은 그 대장 국적의 수도 문화권으로 갈린다(0x00447070).
//
//   ※ 갈래 1 은 0x0044A830 이 「그냥 지나간다」를 굴린다. 모의전은 붙자고 부르는 것이라
//     그 다섯 바이트를 부르는 동안만 `xor eax,eax / ret 8` 로 눌러 둔다.

#define SP_REGION_N    8      // 필드 지역 여덟
#define SP_FIELD_N    16      // 지역마다 두 벌
#define SP_CITY_N    226
#define SP_TERRAIN_N   4
#define SP_CULTURE_N  11

int  Spar_Load(void);          // 모듈을 잡고 표를 살펴본다. 성공 1
int  Spar_Ready(void);

const wchar_t* Spar_TerrainName(int t);
const wchar_t* Spar_CultureName(int c);
const wchar_t* Spar_RegionName(int r);
const wchar_t* Spar_ShapeText(int culture);   // 그 문화권이 내는 병종 줄

const wchar_t* Spar_CityName(int city);
int  Spar_CityCulture(int city);              // 못 읽으면 -1
int  Spar_CityScale(int city);                // 못 읽으면 -1
int  Spar_CityMenLo(int city);                // 예상 병력 아래·위. 못 읽으면 0
int  Spar_CityMenHi(int city);

int  Spar_FieldRegion(int slot);              // slot / 2
int  Spar_FieldLeader(int slot);              // 인물 번호 246~261
int  Spar_FieldMenLo(int slot);
int  Spar_FieldMenHi(int slot);
int  Spar_FieldCulture(int slot);             // 대장 국적의 수도 문화권. 못 읽으면 -1
int  Spar_FieldLeaderName(int slot, wchar_t* out, int cap);   // 읽혔으면 1

// 지금 붙을 수 있나. 못 붙으면 까닭을 적고 0 을 낸다.
int  Spar_CanRun(wchar_t* why, int cap);

// 한 판. 0 이김 · 1 물러남 · 2 몰살 · -1 못 붙임.
// restore 가 서 있으면 소지금·명성·악명·주인공과 부관의 능력을 싸움 앞으로 되돌린다.
int  Spar_RunCity(int city, int terrain, int restore);
int  Spar_RunField(int slot, int terrain, int restore);
