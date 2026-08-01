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
