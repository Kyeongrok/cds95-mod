#pragma once
#include <windows.h>

// 발견물 좌표 — 게임 원본 표(.rdata)를 실행 중에 고친다.
//
//   발견물 표   모듈 + 0x11C540 (92바이트 x 274)
//               +0x44 x1   +0x48 y1   +0x4C x2   +0x50 y2
//
// 좌표 단위는 WORLD.CDS 를 펼친 배열의 칸 번호다 — x 0~2499, y 0~1249.
// 함대 위치(경도 0x1B63B0 / 위도 0x1B63B4, 0~40000 / 0~20000)의 1/16 이다.
//
// ## 게임이 이 표를 어떻게 쓰나 — 왜 고치면 먹히나
//
// 세 곳이 표를 **직접** 인덱싱한다(시작할 때 딴 데로 떠 두는 사본이 없다):
//
//   0x4AAB45  mov eax, [ecx*4 + 0x51C540]     ; 이름 getter
//   0x425678  mov ebx, [ecx*4 + 0x51C584]     ; 0x425640 — 좌표가 박스 안인가
//   0x425754  mov edi, [ecx*4 + 0x51C584]     ; 0x425720 — 좌상단이 정확히 이 점인가
//
// ecx 는 idx*23 이고 *4 해서 92바이트 스트라이드가 된다. 그러니 표의 그 4칸만 고쳐 놓으면
// 게임이 다음 판정부터 그대로 읽는다. .rdata 라 읽기 전용이라서 VirtualProtect 로 잠깐만
// 열고 되돌린다(patrons.c 가 후원자 생년을 고치는 방식과 같다).
//
// 판정 함수 0x425640 (좌표 -> 발견물) 은 이렇게 생겼다:
//
//   for (inst = 0x61E4C8; inst < 0x629898; inst += 0xA8) {
//       i = (inst - 0x61E4C8) / 0xA8;                 // 0x4AAAE0 — 인스턴스 번호 = 발견물 번호
//       if (x < x1[i] || x > x2[i]) continue;
//       if (y < y1[i] || y > y2[i]) continue;
//       if (best <= (x2[i]-x1[i])^2) continue;        // 겹치면 좁은 박스가 이긴다
//       best = ...; hit = i;
//   }
//
// ★ 여기서 나오는 두 가지가 이 플러그인의 뼈대다.
//
//   1) 훑는 것은 **발견물 인스턴스 107개뿐**이다. 표는 274줄이지만 좌표로 찾아지는 것은
//      번호 0~106 뿐이고, 107 번부터는 좌표를 넣어도 이 루프에 안 걸린다.
//   2) -1 을 따로 거르는 분기는 **없다**. x1..y2 가 모두 -1 이면 박스가 (-1,-1)~(-1,-1) 이라
//      게임 좌표(0 이상)로는 영영 안 걸릴 뿐이다. 그래서 0~106 중 좌표가 -1 인 53개는
//      좌표를 넣어 주면 그 자리에서 실제로 발견된다.
//
// ## 오래 가지 않는다 — 그래서 파일에 적어 둔다
//
// 메모리만 고치는 것이라 게임을 끄면 원래대로 돌아간다(세이브에도 안 남는다). 그래서
// 고친 값을 CDS95Util\disc_coords.json 에 적어 두고, 다음에 뜰 때 알아서 다시 넣는다.
// 스키마는 WorldMapKR 의 discoveries.json 과 일부러 같게 뒀다 — 값을 서로 옮겨 붙일 수 있다.
//
//   [ { "id": 21, "name": "콜로세움", "x1": 1512, "y1": 470, "x2": 1513, "y2": 471 }, ... ]

#define DC_N        274          // 표 전체 줄 수
#define DC_JUDGED_N 107          // 그 중 좌표 판정을 받는 앞부분(인스턴스가 있는 만큼)
#define DC_RVA      0x11C540u
#define DC_SZ       92
#define DC_X1_OFF   0x44

#define DC_NONE     (-1)         // 좌표 없음
#define DC_X_MAX    2499
#define DC_Y_MAX    1249

// 함대 위치 원본값 -> 칸 번호. mapwin.c 가 쓰는 그 환산이다(40000/2500 = 16).
#define DC_LON_RVA  0x1B63B0u
#define DC_LAT_RVA  0x1B63B4u
#define DC_LON_MAX  40000
#define DC_LAT_MAX  20000

typedef struct { int x1, y1, x2, y2; } DcBox;

int  DC_Load(void);                    // 표를 잡고 원본을 떠 둔다. 성공 1
int  DC_Ready(void);

int  DC_Get(int i, DcBox* out);        // 지금 표에 든 값
int  DC_Orig(int i, DcBox* out);       // 플러그인이 처음 봤을 때의 값(= 게임 원본)
int  DC_Changed(int i);                // 원본과 다른가
int  DC_ChangedCount(void);
int  DC_Judged(int i);                 // 좌표 판정을 받는 줄인가(i < 107)

int  DC_BoxOk(const DcBox* b);          // 넣어도 되는 값인가(지도 안 · x2>=x1 · 넷 다 -1)

// 표에 써넣는다. 넷 다 DC_NONE 이면 "좌표 없음"으로 되돌린다.
// 범위를 벗어나거나 x2<x1 이면 아무것도 안 하고 0.
int  DC_Set(int i, const DcBox* b);
int  DC_Revert(int i);                 // 그 줄만 원본으로
int  DC_RevertAll(void);               // 고친 줄 수를 돌려준다

// 지금 함대가 선 칸. 세이브 전이라 못 읽으면 0.
int  DC_FleetCell(int* x, int* y);

// CDS95Util\disc_coords.json — 고친 줄만 담는다.
int  DC_Save(HINSTANCE hinst);         // 쓴 줄 수. 실패 -1
int  DC_Apply(HINSTANCE hinst);        // 읽어서 표에 넣는다. 넣은 줄 수. 파일이 없으면 0
void DC_JsonPath(HINSTANCE hinst, wchar_t* out, int cch);
