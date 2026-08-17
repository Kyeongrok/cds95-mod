#pragma once
#include <windows.h>

// 실행 중인 게임의 "살아 있는" 인물 배열. 여급 표(maids.c)와 달리 이건 EXE 파일에 없다 —
// .data 의 초기화되지 않은 뒷부분(RVA 0x18BF98 은 .data 의 raw 크기를 넘어간다)이라
// 세이브를 불러올 때 비로소 채워진다. 그래서 여기 쓴 값이 곧 게임이 지금 쓰는 값이다.
//
// 레코드 배치 (ce/CDS_95.CT 의 "인물 정보" 그룹에서 뽑음. 275칸, 0x11C 간격 균일):
//   +0x00 얼굴   +0x04 나이(현재 나이. 해가 바뀌면 오른다)   +0x08 성별   +0x0C 국적
//   +0x10 혈액형 +0x14 직업   +0x18 체력 +0x1C 지력 +0x20 무력 +0x24 매력 +0x28 운
//   +0xA4 명성치 +0xA8 악명치 +0xAC 현재도시 +0xB0 현재건물
//   +0xB4 이름(19, cp949)     +0xC7 성(19, cp949)
//
// 세이브(SAVEDATA.CDS)의 레코드 색인과 이 배열의 칸 번호는 1:1 이 아니다
// (세이브는 461칸, 이 배열은 275칸). 그래서 색인 계산 대신 이름+얼굴로 짝을 찾는다.

#define LIVECHAR_RVA   0x18BF98u
#define LIVECHAR_SIZE  0x11C
#define LIVECHAR_COUNT 275

// 생년 선택 범위.
#define LIVECHAR_YEAR_MIN 1460
#define LIVECHAR_YEAR_MAX 1526
#define LIVECHAR_YEAR_N   (LIVECHAR_YEAR_MAX - LIVECHAR_YEAR_MIN + 1)

// 배열을 찾아 검사한다. 성공 1 / 게임이 아직 세이브를 안 불러왔거나 다른 빌드면 0.
// 창을 열 때마다 불러도 된다(성공할 때까지 매번 다시 시도한다).
int LiveChar_Load(void);
int LiveChar_Ready(void);

// 왜 실패했는지. 화면에 숫자로 띄워 원인을 좁히는 용도다.
#define LIVECHAR_OK        0
#define LIVECHAR_E_MODULE  1   // 모듈 핸들을 못 얻음
#define LIVECHAR_E_READ    2   // 그 주소를 읽을 수 없음(주소가 틀렸거나 커밋 안 됨)
#define LIVECHAR_E_SLOTS   3   // 칸 내용이 인물 레코드로 안 보임(주소가 어긋남)
#define LIVECHAR_E_EMPTY   4   // 이름 붙은 칸이 너무 적음(세이브를 아직 안 불러옴)
#define LIVECHAR_E_YEAR    5   // 현재연도가 말이 안 됨
int LiveChar_Status(void);
int LiveChar_NamedCount(void);   // 마지막 검사에서 이름이 읽힌 칸 수(쓰레기 칸도 이름처럼 보인다)
int LiveChar_OkCount(void);      // 그 중 필드까지 인물 레코드로 말이 되던 칸 수

int LiveChar_Year(void);                    // 게임 안의 지금 연도. 못 읽으면 0

// 주인공 명성. 못 읽으면 -1.
// 세이브(+0x53)에서도 읽을 수 있지만 그쪽은 2바이트라 65535 를 넘으면 잘리고,
// 마지막으로 저장한 시점 값이라 지금과 다르다. 실행 중 값은 4바이트다.
int LiveChar_PlayerFame(void);

// 이름(성·이름을 가운뎃점으로 이은 것)과 얼굴코드로 칸 번호를 찾는다.
// 딱 하나만 맞을 때만 그 번호를, 아니면 -1 을 돌려준다(엉뚱한 인물에 쓰지 않도록).
int LiveChar_Find(const wchar_t* name, int faceCode);

int LiveChar_Age(int slot);                 // 현재 나이. 실패면 -9999
int LiveChar_SetBirthYear(int slot, int year);   // 나이 = 지금연도 - 생년. 성공 1

// 특기 레벨. id 는 세이브와 같은 1~27(항해술 … 동아시아어), 값은 0~3.
// 레코드 +0x38 부터 4바이트씩 27개가 늘어서고 그 끝(+0xA0) 다음이 명성치(+0xA4)다.
#define LIVECHAR_SKILL_N   27
#define LIVECHAR_SKILL_MAX 3
int LiveChar_Skill(int slot, int id);            // 못 읽으면 -1
int LiveChar_SetSkill(int slot, int id, int lv); // 성공 1

void LiveChar_NameAt(int slot, wchar_t* out, int cap);   // 그 칸의 인물 이름

// 고용상태(1=대화만 2=고용가능 3=고용중). 세이브에는 +0x62 에 있는데 실행 중 배열에서는
// CE 표에 라벨이 없어(레코드 +0xDA 뒤가 통째로 미상) 자리를 직접 찾아내야 한다.
// 세이브의 같은 값과 대조해 딱 맞는 오프셋을 고른다 — 한 번 찾으면 기억한다.
//   hire[i] = 세이브의 고용상태, slot[i] = 그 인물의 배열 칸(-1 이면 건너뛴다), n = 개수
#define LIVECHAR_HIRE_TALK  1
#define LIVECHAR_HIRE_FREE  2
#define LIVECHAR_HIRE_TAKEN 3
int LiveChar_CalibrateHire(const int* hire, const int* slot, int n);
int LiveChar_HireOffset(void);            // 못 찾았으면 -1 (진단용)
int LiveChar_Hire(int slot);              // 못 읽으면 -1
int LiveChar_SetHire(int slot, int v);    // 성공 1

// ---- 주인공(플레이어) 레코드 ----
// 인물 배열과 같은 배치의 레코드가 딱 하나 따로 있다(ce/CDS_95.CT "주인공 정보").
// CE 가 짚은 자리들이 위 레코드 배치와 정확히 맞아떨어져서 오프셋을 그대로 쓴다:
//   얼굴 0x1B60A8(+0x00) · 성별 0x1B60B0(+0x08) · 혈액형 0x1B60B8(+0x10)
//   직업 0x1B60BC(+0x14) · 항해술 0x1B60E0(+0x38) · 명성 0x1B614C(+0xA4)
//   이름 0x1B615C(+0xB4) · 성 0x1B616F(+0xC7)
// 인물 배열에 없는 것이 하나 있다 — 생년/월/일이 이름 뒤 +0xE0/+0xE4/+0xE8 에 붙는다.
//
// 칸이 하나뿐이라 "말이 되는 칸 수" 로 판정할 수 없어서, 이름·성별·얼굴이 인물 레코드로
// 보이는지만 본다. 세이브를 안 불러왔으면 이름이 비어 있어 걸러진다.
#define PLAYER_RVA 0x1B60A8u

int  Player_Load(void);        // 성공 1. 탭을 열 때마다 다시 불러도 된다
int  Player_Ready(void);
int  Player_Status(void);      // LIVECHAR_E_* 와 같은 코드

void Player_Name(wchar_t* out, int cap);
int  Player_Face(void);        // 얼굴코드. 못 읽으면 -1
int  Player_SetFace(int code); // 초상화 교체. 성공 1
int  Player_Gender(void);      // 0=남 1=여. 못 읽으면 -1
// 성별 바꾸기. 게임이 초상화를 MALE.CDS / FEMALE.CDS 중 어디서 꺼낼지가 이 값으로 갈리므로,
// 반대쪽 표의 얼굴을 쓰려면 이것도 같이 써야 한다(초상화 말고 게임 진행에도 영향이 간다).
int  Player_SetGender(int g);  // 성공 1
int  Player_Age(void);         // 못 읽으면 -9999
int  Player_BirthYear(void);   // 못 읽으면 0
int  Player_Blood(void);       // 0=A 1=B 2=O 3=AB. 못 읽으면 -1
int  Player_Job(void);         // 못 읽으면 -1
int  Player_Fame(void);        // 못 읽으면 -1
int  Player_Infamy(void);      // 못 읽으면 -1
// 명성·악명 더하기(빼려면 음수). 0 아래로는 안 내려가고 FAME_MAX 위로는 안 올라간다.
// 레코드가 세이브에 그대로 들어가므로 게임에서 저장하면 남는다.
// 바뀐 값을 돌려준다. 못 쓰면 -1.
int  Player_AddFame(int delta);
int  Player_AddInfamy(int delta);

// 주인공이 데리고 다니는 네 자리(부관/항해사/측량사/통역).
// 0x1B61A0 부터 4바이트씩 넷이고, 값은 4096 + 인물 배열 칸 번호, 0xFFFFFFFF 면 비어 있다.
// CE 표의 드롭다운이 276줄(275명 + "없음")인 것이 인물 배열 크기와 정확히 맞는다.
// (그 뒤 0x1B61B0 은 "부인" 인데 목록이 128줄로 달라서 여기 넣지 않았다.)
#define LIVECHAR_CREW_N 4
const wchar_t* LiveChar_CrewLabel(int which);    // 0 -> "부관" … 3 -> "통역"
int  LiveChar_Crew(int which);                   // 그 자리의 인물 칸 번호. 비었으면 -1
int  LiveChar_SetCrew(int which, int slot);      // slot < 0 이면 비운다. 성공 1
