#pragma once
#include <windows.h>
#include "hint_rows.h"     // kHints[186] — 힌트 이름과 분류

// 힌트 상태. 실행 중인 cds_95 모듈에서 읽는다(파일은 안 건드린다).
//
//   힌트 배열   모듈 + 0x18B4E0 (8바이트 x 186)
//               +0x00 가치   +0x04 상태
//               상태 8 = 아직 없음 / 13 = 힌트 취득 / 15 = 발견 완료
//               (ce/CDS_95.CT 의 "힌트" 묶음. 항목 186개가 0x18B4E4 + id*8 에 딱 맞는다)
//   분류 이름   모듈 + 0x160C60 (char* x 8) — 지리 역사 보물 종교 교역품 미신 생물 민족
//               (발견물 표가 쓰는 그 이름표를 그대로 읽어 게임 말투를 따른다)
//
// ★ 힌트는 발견물(274개)과 다른 목록이다. 번호도 다르다 — 예를 들어 힌트 31 은 "트로이"
//   지만 발견물 31 은 "아부심벨 대신전"이다. 그래서 이름·분류는 발견물 표가 아니라
//   hint_rows.h 의 표에서 가져온다(그 표를 어떻게 만들었는지는 그 파일 머리 참고).
//
// 힌트 배열은 .data 뒷부분이라 세이브를 불러오기 전에는 커밋조차 안 돼 있다.
// 읽기 전에 HintDb_Live 로 확인한다.

#define HINT_N        HINT_ROW_N
#define HINT_RVA      0x18B4E0u
#define HINT_SZ       8
#define HINT_CAT_N    8
#define HINT_CATNAME_RVA 0x160C60u

#define HINT_NONE  8    // 아직 힌트가 없다
#define HINT_GOT   13   // 힌트를 얻었다
#define HINT_DONE  15   // 발견까지 마쳤다

int  HintDb_Load(void);                 // 분류 이름표를 읽는다. 성공 1
int  HintDb_Ready(void);
int  HintDb_Live(void);                 // 힌트 배열이 살아 있나(세이브를 불러왔나)

const wchar_t* HintDb_Name(int i);      // 힌트 이름. 범위 밖이면 L"?"
int  HintDb_Cat(int i);                 // 분류 0~7. 모르면 -1
const wchar_t* HintDb_CatName(int cat);

int  HintDb_State(int i);               // HINT_NONE / HINT_GOT / HINT_DONE. 못 읽으면 -1
int  HintDb_Value(int i);               // 그 힌트의 가치. 못 읽으면 -1
