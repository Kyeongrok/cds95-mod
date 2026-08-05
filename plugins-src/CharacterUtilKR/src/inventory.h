#pragma once
#include <windows.h>

// 주인공의 소지품. livechar.h 의 인물 배열과 마찬가지로 EXE 파일에는 없고
// (.data 의 raw 크기를 넘어가는 뒷부분이라) 세이브를 불러올 때 비로소 채워진다.
// 그래서 여기 쓴 값이 곧 게임이 지금 쓰는 값이다 — 세이브 파일은 건드리지 않는다.
//
// 자리 (ce/CDS_95.CT 의 "소지금" · "소지 아이템" · "보관 아이템" 그룹):
//   0x1B6194  소지금 (i32)
//   0x1B61A0  데리고 다니는 네 자리 — livechar.c 가 쓴다
//   0x1B61B0  부인
//   0x1B61B8  소지 아이템 16칸 (i32)
//   0x1B61F8  보관 아이템 99칸 (i32)
// 값은 item_names.h 의 kItemNames[286] 색인이고(CE 드롭다운이 "41:에스톡" 으로 같다),
// 빈 칸은 -1 이다(바로 위 승무원 자리가 0xFFFFFFFF 를 빈 칸으로 쓰는 것과 같다).

#define INV_MONEY_RVA 0x1B6194u
#define INV_HELD_RVA  0x1B61B8u
#define INV_STORE_RVA 0x1B61F8u
#define INV_HELD_N    16
#define INV_STORE_N   99
#define INV_ITEM_N    286
#define INV_EMPTY     (-1)
#define INV_MONEY_MAX 99999999

#define INV_HELD  0
#define INV_STORE 1

// 자리를 잡고 내용이 말이 되는지 본다. 성공 1. 창을 열 때마다 불러도 된다.
int Inv_Load(void);
int Inv_Ready(void);

// 왜 실패했는지 — 화면에 숫자로 띄워 원인을 좁힌다.
#define INV_OK        0
#define INV_E_MODULE  1   // 모듈 핸들을 못 얻음
#define INV_E_READ    2   // 그 자리를 못 읽음(주소가 틀렸거나 커밋 안 됨)
#define INV_E_RANGE   3   // 칸 값이 아이템 번호로 안 보임(주소가 어긋남)
#define INV_E_EMPTY   4   // 전부 0 — 아직 세이브를 안 불러왔다
#define INV_E_MONEY   5   // 소지금이 말이 안 됨
int Inv_Status(void);

int Inv_Count(int kind);                    // INV_HELD -> 16, INV_STORE -> 99
int Inv_Get(int kind, int i);               // 아이템 번호. 빈 칸이면 -1, 못 읽으면 -1
int Inv_Set(int kind, int i, int item);     // item < 0 이면 비운다. 성공 1
int Inv_Used(int kind);                     // 채워진 칸 수
int Inv_FirstEmpty(int kind);               // 빈 칸 번호. 없으면 -1

int Inv_Money(void);                        // 못 읽으면 -1
int Inv_SetMoney(int v);                    // 성공 1
