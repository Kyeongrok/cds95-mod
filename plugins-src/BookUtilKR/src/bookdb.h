#pragma once
#include <windows.h>

// BookUtilKR — 도서관 서적 257권을 훑어본다.
// (분석: 옵시디안 `Project/cds95/분석/20.분석-도서관 책과 책등 색.md`)
//
//   정적표  .rdata  VA 0x4C4748  257권 x 0x58   ← EXE 에 구워진 원본
//     +0x00 제목ptr  +0x04 저자ptr  +0x0C 언어(0~13)  +0x10 등장연도 오프셋(+1480)
//     +0x18~+0x34 놓인 도시 8칸(-1 없음)   +0x38~+0x54 주는 힌트 8칸(-1 없음)
//   런타임  .data   VA 0x581120  257개 x 0x48   ← 세이브를 불러와야 선다
//     +0x00 도시8 사본  +0x20 힌트8 사본  +0x40/+0x44 읽음 표시
//
//   권수는 게임의 순회 범위에서 나온다 — 0x4B3440 이 0x581160 부터 0x5859A8 까지
//   0x48 씩 훑는다. (0x585968 - 0x581120) / 0x48 = 257.
//
// 책등 색은 게임 함수 0x4716A0 이 0/1/2 로 낸다. 흉내 내지 않고 그대로 부른다.

#define BOOK_N          257
#define BOOK_RVA        0x000C4748u   // VA 0x4C4748 (.rdata)
#define BOOK_SIZE       0x58
#define BOOK_LIVE_RVA   0x00181120u   // VA 0x581120 (.data 0채움 대역)
#define BOOK_LIVE_SZ    0x48
#define BOOK_SLOTS      8

#define BK_TITLE   0x00
#define BK_AUTHOR  0x04
#define BK_LANG    0x0C
#define BK_YEAR    0x10
#define BK_CITY0   0x18
#define BK_HINT0   0x38
#define BOOK_YEAR_BASE 1480
#define BOOK_LANG_LV   3              // 0x463E41 의 cmp eax,3 — 언어 수준 3 이상이라야 읽는다

// 게임 함수 (둘 다 __cdecl)
#define FN_COLOR_RVA  0x000716A0u     // VA 0x4716A0  int(int 책번호, int 갈래)
#define FN_LANGLV_RVA 0x00063DB0u     // VA 0x463DB0  int(void* 책레코드)

// 지금 도시 / 함대 좌표
#define CUR_CITY_RVA  0x001B6154u     // -1 이면 항해 중
#define FLEET_LON_RVA 0x001B63B0u
#define FLEET_LAT_RVA 0x001B63B4u

// 책등 색 코드
#define BOOK_C_GREEN  0   // 줄 새 힌트가 없다
#define BOOK_C_BLUE   1   // 읽으면 새 힌트가 들어온다
#define BOOK_C_RED    2   // 줄 힌트는 남았는데 조건이 모자란다
#define BOOK_C_NONE  (-1) // 세이브 전이라 알 수 없다

int  Book_Load(void);
int  Book_Ready(void);
#define BKDB_OK       0
#define BKDB_E_MODULE 1
#define BKDB_E_READ   2
#define BKDB_E_ROWS   3
#define BKDB_E_TAIL   4
int  Book_Status(void);
int  Book_Count(void);

const wchar_t* Book_Title(int k);
const wchar_t* Book_Author(int k);
int  Book_Lang(int k);                 // 0~13. 이름은 SkillDb_LangName
int  Book_Year(int k);                 // 실제 연도(1480+)
int  Book_CityCount(int k);
int  Book_City(int k, int i);
int  Book_HintCount(int k);
int  Book_Hint(int k, int i);

int  Book_Live(void);                  // 세이브를 불러왔나(런타임/힌트 배열이 서 있나)
int  Book_Read(int k);                 // 런타임 +0x40 (읽음 표시). 못 읽으면 -1
int  Book_Color(int k);                // BOOK_C_*
int  Book_LangLevel(int k);            // 우리 함대의 그 언어 최고 수준. 못 읽으면 -1
int  Book_NewHints(int k);             // 이 책이 줄 수 있는, 아직 못 얻은 힌트 수

// 그 도시에 도서관(건물코드 8)이 실제로 있나. 건물표를 못 읽으면 -1
int  Book_CityHasLibrary(int city);
// 함대에서 그 도시까지 칸 거리. 항해 중이 아니거나 모르면 -1
int  Book_CityDistance(int city);
int  Book_CurrentCity(void);           // -1 이면 항해 중
