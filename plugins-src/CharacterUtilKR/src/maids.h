#pragma once
#include <windows.h>

// CDS_95.EXE 의 .rdata 에 정적으로 박혀 있는 여급 표를 실행 중 메모리에서 그대로 읽는다.
// 40바이트 × 127행이고, 표 자체가 게임 이미지 안에 있으므로 세이브와 무관하게 항상 읽힌다.
// 헥스로 EXE 를 고치면 그 값이 그대로 보이라고 구운 표 대신 메모리에서 읽는다.
//
// 레코드 배치 (ce/CDS_95.CT 의 "여급 정보" 그룹 + 실제 EXE 로 전수 확인):
//   +0x00 이름 문자열 포인터(cp949)   +0x04 얼굴코드
//   +0x08 1495년 기준 나이(부호 있음) → 등장연도 = 1495 - 값
//   +0x0C 성좌(0~11)   +0x10 혈액형(0=A 1=B 2=O 3=AB)
//   +0x14 미상(0~30)   +0x18 미상(0~7)
//   +0x1C 건물(127행 전원 4=주점)     +0x20 언어 비트마스크(bit0~13)
//   +0x24 도시번호(kCities 색인)
// 성좌와 미상 2필드는 게임이 화면에 쓰지 않아 표시하지 않는다.
//
// 언어 비트 b 는 세이브 특기 ID (SAVE_SKILL_LANG0 + b) 와 순서가 같아서
// savedata.c 의 이름표(kSkillName/kSkillShort)를 그대로 재사용한다.

#define MAID_COUNT 127
#define MAID_RVA   0x117AF8u   // 모듈 base 기준 오프셋. 절대 VA 를 박지 않는다.

// 등장연도 선택 범위. 원본 값이 1474(테레사)~1525(샤론)이라 그 바깥으로 조금 여유를 둔다.
// 127명 전원이 목록 안에 들어오므로 어떤 여급을 열어도 현재 연도가 선택된 채로 뜬다.
#define MAID_YEAR_MIN 1470
#define MAID_YEAR_MAX 1530
#define MAID_YEAR_N   (MAID_YEAR_MAX - MAID_YEAR_MIN + 1)

#define MAID_LANG_N   14      // 언어 비트 수(스페인어 … 동아시아어)

// 편집 스위치. 0 이면 그 값은 보여주기만 하고 고치는 UI 를 감춘다.
// 쓰는 코드(Maid_SetCity / Maid_ToggleLang)는 그대로 남으므로 1 로 올리면 바로 돌아온다.
//
// 도시: 런타임에 고쳐도 여급이 옮겨가지 않는다. 게임이 세이브를 불러오는 시점에 여급 배치를
//   끝내 두고 그 뒤로는 이 표의 도시를 다시 보지 않는 듯하다. 새 게임 시작 전에 고치면
//   먹을 수도 있어서 코드는 남겨 둔다.
// 언어: 고쳐도 반영됐는지 확인할 방법이 마땅치 않아 일단 내려 둔다(효과가 없다고 확인된
//   것은 아니다 — 여급에게 언어를 배우는 시점까지 가봐야 알 수 있다).
#define CHARKR_EDIT_CITY 0
#define CHARKR_EDIT_LANG 0

typedef struct {
    wchar_t  name[32];
    int      face;        // 얼굴코드. 23·34·77 은 여급 두 명이 나눠 쓴다
    int      ageAt1495;   // 음수면 게임 시작 뒤에 등장
    int      blood;       // 0=A 1=B 2=O 3=AB
    unsigned lang;        // bit0~13
    int      city;
} MaidInfo;

// 표를 읽어 검사한다. 성공 1 / 다른 빌드로 보이면 0(호출한 쪽이 예전 동작으로 폴백).
// Face_Load() 로 FEMALE.CDS 를 연 뒤에 불러야 한다(얼굴코드 범위 검사에 쓴다).
int  Maid_Load(void);
int  Maid_Count(void);                 // 실패 시 0
const MaidInfo* Maid_At(int row);
int  Maid_Year(const MaidInfo* m);     // 등장연도 = 1495 - ageAt1495

// 등장연도/언어를 로드된 EXE 이미지에 직접 써넣는다(.rdata 라 VirtualProtect 로 잠깐 연다).
// 메모리만 바뀌므로 게임을 끄면 원래대로 돌아간다. 성공 1.
int  Maid_SetYear(int row, int year);
int  Maid_ToggleLang(int row, int bit);        // 언어 비트 하나를 뒤집는다
int  Maid_SetCity(int row, int city);          // CHARKR_EDIT_CITY 참고 — 지금은 UI 에서 안 부른다

// 이름표는 savedata.c 가 들고 있는 표(kSkillName / kCities)를 그대로 쓴다.
// character.c 가 savedata 를 직접 알 필요 없게 여기서 한 겹 감싼다.
const wchar_t* Maid_LangName(int bit);         // 0 -> "스페인어" … 13 -> "동아시아어"
const wchar_t* Maid_CityName(int city);
int            Maid_CityCount(void);           // 226
const wchar_t* Maid_BloodName(int blood);      // 0 -> "A" … 3 -> "AB"

// 정보 패널 본문. 언어 목록 줄("언어 스페인어, 포르투갈어, …")이 들어가고,
// 도시는 CHARKR_EDIT_CITY 가 꺼져 있을 때만 여기 같이 적는다(켜져 있으면 셀의 select box 몫).
// 등장연도는 늘 셀의 select box 가, 혈액형은 셀의 머리글이 맡는다. out 은 256 wchar 이상.
void Maid_FormatInfo(const MaidInfo* m, wchar_t* out, int cap);
