#pragma once
#include <windows.h>

// 발견물 — 힌트(186)와 다른 목록이다. 이름·분류·가치는 실행 중 모듈의 표에서 읽고,
// 발견 여부는 발견물 인스턴스(discinst.h)에서, 그것이 없는 줄만 힌트 배열에서
// 가져온다. 셋 다 읽기만 한다.
//
//   발견물 표   모듈 + 0x11C540 (.rdata, 92바이트 x 274)
//               +0x00 이름 char*(cp949)   +0x04 분류(0~7)   +0x18 가치
//               (+0x44~0x50 은 좌표 — WorldMapKR 이 쓴다)
//
// 발견 여부를 어디서 가져오나 —
//   앞의 107개는 발견물 인스턴스(모듈+0x21E4C8)가 있어서 "내가 발견했다 / 보고까지
//   했다"가 사람 칸으로 남는다. 발견한 순간에 채워지는 자리가 그것뿐이다 —
//   힌트 배열의 발견 비트는 보고할 때에야 켜진다(discinst.h 머리 참고).
//   나머지(교역품·비보·모조품 따위)는 인스턴스가 없어 힌트 배열의 상태로 가른다.
//   그 이음표가 disc_hint.h 다. 274개 중 184개가 이어지고, 나머지는 DISC_NOLINK 다.
//
// ★ 예전에는 SAVEDATA.CDS 0x19E6A + i*164 의 첫 바이트를 발견 플래그로 읽었다. 그 자리는
//   발견물과 아무 상관이 없었고(범위가 힌트 영역 0x1A625 를 통째로 덮어 버린다),
//   화면에 뜨던 "발견 · 발표" 는 거기 있던 0xCC/0xFF 를 bit6/bit7 로 읽은 헛것이었다.
//   cds-helper 의 SaveDataService 상수를 그대로 물려받은 것이라 같은 실수를 하지 말 것.

#define DISC_N        274
#define DISC_RVA      0x11C540u
#define DISC_SZ       92

int  Disc_Load(void);                    // 이름표를 읽는다. 성공 1
int  Disc_Ready(void);                   // 이름표를 읽었나
int  Disc_Live(void);                    // 발견 여부를 알 수 있나(힌트 배열이 살아 있나)
int  Disc_Count(void);                   // 274

const wchar_t* Disc_Name(int i);
int  Disc_Cat(int i);                    // 0~7. 모르면 -1
int  Disc_Value(int i);                  // 가치. 모르면 -1

#define DISC_NOLINK   -2                 // 발견이라는 개념이 없는 줄(인스턴스도 힌트도 없다)
#define DISC_UNKNOWN  -1                 // 세이브를 아직 안 불러와 모른다
#define DISC_NOT      0                  // 아직 못 찾음
#define DISC_HINTED   1                  // 힌트는 얻었고 아직 못 찾음
#define DISC_FOUND    2                  // 발견했지만 아직 보고 안 함
#define DISC_REPORTED 3                  // 발견하고 보고까지 마침
int  Disc_Found(int i);
int  Disc_Inst(int i);                   // 그 발견물의 인스턴스 번호. 없으면 -1
int  Disc_Taken(int i);                  // 남이 먼저 발견해 둔 줄인가(1/0), 모르면 -1

int  Disc_HintId(int i);                 // 이어진 힌트 번호. 없으면 -1
int  Disc_LinkCount(void);               // 힌트가 이어진 발견물 수(184)
