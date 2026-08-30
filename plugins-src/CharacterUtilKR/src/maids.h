#pragma once
#include <windows.h>

// CDS_95.EXE 의 .rdata 에 정적으로 박혀 있는 여급 표를 실행 중 메모리에서 그대로 읽는다.
// 40바이트 × 127행이고, 표 자체가 게임 이미지 안에 있으므로 세이브와 무관하게 항상 읽힌다.
// 헥스로 EXE 를 고치면 그 값이 그대로 보이라고 구운 표 대신 메모리에서 읽는다.
//
// 레코드 배치 (ce/CDS_95.CT 의 "여급 정보" 그룹 + 실제 EXE 로 전수 확인):
//   +0x00 이름 문자열 포인터(cp949)   +0x04 얼굴코드
//   +0x08 1495년 기준 나이(부호 있음) → 생년 = 1495 - 값
//         (등장연도가 아니다. 인물은 18세가 되어야 술집에 나온다 — navview 쪽 주석 참고)
//   +0x0C 성좌(0~11)   +0x10 혈액형(0=A 1=B 2=O 3=AB)
//   +0x14 **운명의 반려자 얼굴코드**(0~30)   +0x18 **성격**(0~7)
//     +0x14 는 "이 여급과 맺어질 주인공 얼굴"이다. 점술이 그 여급이 있는 도시를 일러 주는
//     자리(0x40A680)에서 주인공의 표시 얼굴코드와 견준다. 표시 얼굴은 36세부터 +16 이다.
//     +0x18 은 성격 8종(0=당당한 … 7=견실한). 여급이 하는 말이 이 값으로 갈리고
//     (0x4A3130 의 점프 테이블), 주인공과의 궁합도 이 값으로 따진다 — Maid_Match 참고.
//   +0x1C 건물(127행 전원 4=주점)     +0x20 언어 비트마스크(bit0~13)
//   +0x24 도시번호(kCities 색인)
// 성좌는 게임이 화면에 쓰지 않아 표시하지 않는다.
//
// 언어 비트 b 는 세이브 특기 ID (SAVE_SKILL_LANG0 + b) 와 순서가 같아서
// savedata.c 의 이름표(kSkillName/kSkillShort)를 그대로 재사용한다.

#define MAID_COUNT 127
#define MAID_RVA   0x117AF8u   // 모듈 base 기준 오프셋. 절대 VA 를 박지 않는다.

// 생년 선택 범위. 원본 값이 1474(테레사)~1525(샤론)이라 그 바깥으로 조금 여유를 둔다.
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
// 언어: 켜 둔다. 확인하는 법 — 주인공이 아는 언어와 겹치는 것을 하나 켜 주고 그 여급에게
//   말을 걸어 본다. 대화가 통하면 반영된 것이다(겹치는 언어가 없으면 말이 안 통한다).
#define CHARKR_EDIT_CITY 0
#define CHARKR_EDIT_LANG 1

typedef struct {
    wchar_t  name[32];
    int      face;        // 얼굴코드. 23·34·77 은 여급 두 명이 나눠 쓴다
    int      ageAt1495;   // 음수면 1495년에 아직 태어나지 않았다는 뜻
    int      blood;       // 0=A 1=B 2=O 3=AB
    unsigned lang;        // bit0~13
    int      city;
    int      personality; // 0~7. Maid_PersonalityName 으로 이름을 얻는다
    int      fateFace;    // 운명의 반려자 얼굴코드(0~30)
} MaidInfo;

// 표를 읽어 검사한다. 성공 1 / 다른 빌드로 보이면 0(호출한 쪽이 예전 동작으로 폴백).
// Face_Load() 로 FEMALE.CDS 를 연 뒤에 불러야 한다(얼굴코드 범위 검사에 쓴다).
int  Maid_Load(void);
int  Maid_Count(void);                 // 실패 시 0
const MaidInfo* Maid_At(int row);
int  Maid_Year(const MaidInfo* m);     // 생년 = 1495 - ageAt1495

// 생년/언어를 로드된 EXE 이미지에 직접 써넣는다(.rdata 라 VirtualProtect 로 잠깐 연다).
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

// 정보 패널 본문 세 줄. out 은 256 wchar 이상.
//
//   성격 친절한 · 궁합 ◎
//   도시 오포르토 (지금 세비야)          <- 옮겨 갔을 때만 괄호가 붙는다
//   언어 스페인어, 포르투갈어
//
// 궁합과 "지금" 은 실행 중에만 아는 값이라 세이브를 불러오기 전에는 그 자리가 빠진다.
// 도시는 CHARKR_EDIT_CITY 가 꺼져 있을 때만 여기 적는다(켜져 있으면 셀의 select box 몫).
// 생년은 늘 셀의 select box 가, 혈액형은 셀의 머리글이 맡는다.
void Maid_FormatInfo(int row, wchar_t* out, int cap);

// ---- 실행 중에만 있는 여급 상태(친밀도 · 지금 도시) ----
// 위 표(.rdata)와 달리 이쪽은 게임이 돌 때만 있는 "살아 있는" 여급 객체 배열이다.
// EXE 안 생성자(0x479510)가 60바이트 x 127칸을 만들어 놓고 vtable(0x518ED0)을 꽂는다.
//   +0x00 vtable   +0x04 종류(2 = 여급)
//   +0x20 친밀도(0~100)   +0x24 지금 있는 도시(처음엔 표 +0x24 와 같다)
//   +0x28/+0x2C/+0x30/+0x34 미상   +0x38 남은 일수(-1 = 없음)
// 친밀도는 0x478530 이 0~100 으로 잘라 넣고(clamp), 여급을 만나 이야기를 나눌수록 오른다.
//
// **이 값들은 세이브에 그대로 들어간다** — 여급 127칸을 훑는 직렬화 루프가 넷 있고
// (쓰기 0x4796B0 · 읽기 0x479630, 친밀도는 부모 클래스 0x478500 / 0x4784D0),
// 세이브를 불러오면 그대로 되살아난다. 그래서 여기 보이는 목록은 "이 세이브에서
// 만난 여급"이 맞다. 새 게임을 시작하면 0x461E6C 가 친밀도를 전원 0 으로 되돌린다.
//
// 자리 확인: ce/CDS_95.CT 의 "여급 정보" 그룹이 0x5B3C80(=+0x20)을 친밀도,
// 0x5B3C84(=+0x24)를 현재도시로 짚어 두었고, 생성자가 +0x24 에 표 +0x24(도시)를
// 그대로 옮겨 담는 것이 코드에서 그대로 보인다.
#define MAID_LIVE_RVA     0x1B3C60u
#define MAID_LIVE_SZ      0x3C
#define MAID_LIVE_VT_RVA  0x118ED0u   // 그 배열이 쓰는 vtable. 칸 검사용
#define MAID_LIVE_OFF_INTIMACY 0x20
#define MAID_LIVE_OFF_CITY     0x24
#define MAID_INTIMACY_MAX 100

// 배열을 찾아 검사한다. 성공 1. 여러 번 불러도 된다(실패하면 다음에 다시 본다).
int Maid_LiveReady(void);

int Maid_Intimacy(int row);   // 0~100. 못 읽으면 -1
int Maid_LiveCity(int row);   // 지금 있는 도시. 못 읽으면 -1
// 만난 적이 있는가. 친밀도가 0 보다 크면 말을 섞은 것이다(그냥 앉아만 있는 여급은 0).
// 못 읽으면 -1.
int Maid_Met(int row);

// ---- 성격과 궁합 ----
// 여급 성격은 8종이고, 주인공에게는 그 8종에 대한 **선호(0~2)** 가 있다.
// 게임이 여급과 이야기할 때(0x465C61) 그 여급 성격에 대한 선호가 **2 면 친밀도를
// 5 + rand(15) 만큼**, 아니면 **2 + rand(10) 만큼** 올린다(둘 다 60 이 상한).
// 그러니 선호 2 = "궁합이 좋다" 이고, 이 창은 그것을 ◎ 로 적는다.
//
// 선호는 주인공의 **성좌 + 혈액형 + 얼굴**에서 나온다(게임 함수 0x477FE0 → 0x47CB70):
//
//   pref[i] = 성좌표[성좌][i] + 혈액형표[혈액형][i]        (i = 성격 0~7)
//   pref[ 얼굴표[표시얼굴].성격 ] += 얼굴표[표시얼굴].보정
//   pref[i] = clamp(pref[i], 0, 2)
//
// 세 표 모두 EXE 안에 구워져 있다(아래 RVA). 성좌는 생월·생일에서 나오고(0x42E620,
// 경계표 0x547260 — 0=양자리 … 11=물고기자리), 표시 얼굴은 36세부터 +16 이다.
#define MAID_PERSONALITY_N 8

#define MATCH_ZODIAC_RVA 0x168578u   // 12성좌 x 8칸(int)
#define MATCH_BLOOD_RVA  0x1686F8u   // 4혈액형 x 8칸 — 성좌표 바로 뒤에 이어 붙어 있다
#define MATCH_FACE_RVA   0x11ACA0u   // 얼굴 32줄 x 32바이트, +0x18 성격번호 · +0x1C 보정
#define MATCH_ZODIAC_RVA_END (MATCH_ZODIAC_RVA + 12u * 32u)
#define MATCH_FACE_N     32
#define MATCH_ELDER_AGE  36          // 이 나이부터 얼굴코드에 16 을 더해 그린다
#define MATCH_ELDER_STEP 16

const wchar_t* Maid_PersonalityName(int p);   // 0 -> "당당한" … 7 -> "견실한"

// 주인공의 성격 선호를 계산한다. out 은 8칸. 성공 1 / 세이브를 아직 안 불러왔으면 0.
int Maid_PlayerPrefs(int out[MAID_PERSONALITY_N]);

// 그 여급과의 궁합. 2 = 좋음(◎) · 1 = 보통(○) · 0 = 나쁨(△). 못 재면 -1.
int Maid_Match(int row);

// 그 여급이 주인공의 "운명의 반려자" 인가. 1/0, 못 재면 -1.
int Maid_IsFate(int row);
