#pragma once
#include <windows.h>

// 술집 "정보를 듣는다" 대사 표 — 게임 원본 표(.rdata)를 실행 중에 고친다.
//
//   대사 표   모듈 + 0x125078 (24바이트 x 191)
//             +0x00 유적 코드   +0x04~+0x10 들을 수 있는 도시 4칸   +0x14 대사 char*(cp949)
//
// 스폰서와 계약을 맺으면 술집 메뉴에 "정보를 듣는다"가 생기고, 누르면 이 표에서 고른
// 대사가 나온다. 뜯은 자리는 note/TavernInfo-0x42E780.md 에 정리해 뒀다. 요약하면:
//
//   계약 전역 0x61D1D0 [+0x10] = 계약된 힌트 번호
//     -> 0x493E60 이 힌트 표(0x4D8E88, 80B x 186) +0x00 에서 **유적 코드**를 꺼내고
//     -> 0x414390 이 이 표에서 "코드가 같고 지금 도시가 도시 4칸에 든" 줄을 모으고
//     -> 0x42E780 이 그 중 하나를 무작위로 골라 술집 객체 +0xB8 에 넣는다
//     -> 0x42FE5C 가 그 값이 0 이상이면 메뉴 항목을 켠다
//
// 그러니 **도시 4칸을 고치면 "그 이야기를 어느 도시에서 들을지"가 바뀌고**, 유적 코드를
// 고치면 "어느 발견물에 딸린 이야기인지"가 바뀐다.
//
// ★ 지금 도시에 맞는 줄이 하나도 없으면 게임은 도시 조건을 풀고(-1) 표 전체에서 고른다
//   (0x42E780 의 뒤쪽 갈래). 그래서 도시를 다 비워도 이야기가 아주 없어지지는 않는다.
//
// 표를 읽는 코드가 매번 이 자리를 직접 인덱싱하므로(사본이 없다) VirtualProtect 로 잠깐
// 열고 고치면 그대로 먹는다. 발견물 좌표를 고치는 DiscoveryEditKR 과 같은 수법이다.
//
// 대사 글월 자체는 여기서 안 고친다 — 그것은 DialogUtilKR 이 하는 일이다(문구를 파일로
// 갈아 끼운다). 여기서는 읽어서 보여 주기만 한다.

#define TAV_N        191
#define TAV_RVA      0x125078u
#define TAV_SZ       24
#define TAV_CITY_N   4          // 도시 칸 수
#define TAV_CODE_OFF 0x00
#define TAV_CITY_OFF 0x04
#define TAV_TEXT_OFF 0x14

#define TAV_NONE     (-1)
#define TAV_CITY_MAX 225        // 도시 226개(0~225)
#define TAV_CODE_MAX 9999

// 지금 정박한 도시. 항해 중이면 -1. (BookUtilKR 이 쓰는 그 자리다)
#define TAV_CUR_CITY_RVA 0x1B6154u

typedef struct {
    int code;
    int city[TAV_CITY_N];
} TavRow;

int  TavDb_Load(void);                  // 표를 잡고 원본을 떠 둔다. 성공 1
int  TavDb_Ready(void);

int  TavDb_Get(int i, TavRow* out);     // 지금 표에 든 값
int  TavDb_Orig(int i, TavRow* out);    // 플러그인이 처음 봤을 때의 값(= 게임 원본)
int  TavDb_Changed(int i);
int  TavDb_ChangedCount(void);

const wchar_t* TavDb_Text(int i);       // 대사(읽기 전용). 못 읽으면 L""

int  TavDb_RowOk(const TavRow* r);      // 넣어도 되는 값인가
int  TavDb_Set(int i, const TavRow* r); // 표에 써넣는다
int  TavDb_Revert(int i);
int  TavDb_RevertAll(void);             // 되돌린 줄 수

int  TavDb_CurCity(void);               // 지금 도시. 항해 중이면 -1

// CDS95Util\tavern_info.json — 고친 줄만 담는다.
int  TavDb_Save(HINSTANCE hinst);       // 쓴 줄 수. 실패 -1
int  TavDb_Apply(HINSTANCE hinst);      // 읽어서 표에 넣는다. 넣은 줄 수
void TavDb_JsonPath(HINSTANCE hinst, wchar_t* out, int cch);
