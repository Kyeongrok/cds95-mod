#pragma once
#include <windows.h>

// 발견물 인스턴스 107개 — "내가 발견했는가"가 실제로 들어 있는 자리다.
//
//   인스턴스 배열   모듈 + 0x21E4C8 (0xA8 바이트 x 107, 끝 0x229898)
//                   +0x16 플래그(워드)
//                   +0x18 + s*0x30 : 사람 칸 셋(s = 0 나 / 1 남 / 2 보고)
//                        +0x00 이름 39바이트(cp949, 첫 바이트가 0 이면 빈 칸)
//                        +0x28 해   +0x2C 달
//   인스턴스 번호는 발견물 표(disc.h, 0x11C540)의 앞 107개와 그대로 짝이다.
//
// ★ 왜 이 파일이 생겼나 — 힌트 배열(hintdb.h)의 "발견" 비트는 발견한 때가 아니라
//   후원자에게 **보고한** 때 켜진다. EXE 에서 그 비트를 켜는 곳은 0x4AACA0 하나뿐이고,
//   그것을 부르는 두 곳(0x40AFC6 · 0x40B98F)이 모두 "…보고했습니다" 메시지를 내는
//   길이다. 발견하는 순간에 힌트 배열을 건드리는 코드는 없다. 그래서 힌트만 읽으면
//   발견해 놓고 아직 보고를 안 한 것이 계속 "힌트 있음"으로 남는다.
//   발견한 순간에 채워지는 자리가 바로 이 인스턴스의 사람 칸 0번이다.
//
// 힌트와 이어 붙이는 법 —
//   발견물 표 +0x08 과 힌트 표(0xD8E88, 80바이트 x186) +0x00 이 같은 "유적 번호"다.
//   게임도 보고할 때 이 번호가 같은 힌트를 모두 켠다(0x4AACA0 안의 186 회 훑기).
//   그래서 힌트 하나에 인스턴스가 여럿 걸릴 수 있고(피라미드·스핑크스처럼),
//   그 중 하나라도 발견했으면 그 힌트는 발견한 것으로 본다 — 게임과 같은 셈법이다.

#define DINST_N        107
#define DINST_RVA      0x21E4C8u
#define DINST_SZ       0xA8
#define DINST_SLOT_OFF 0x18
#define DINST_SLOT_SZ  0x30
#define DINST_NAME_SZ  39

#define DINST_ME     0      // 내가 발견했다
#define DINST_OTHER  1      // 남이 먼저 발견했다(역사 속 인물)
#define DINST_REPORT 2      // 내가 보고까지 마쳤다

// 유적 번호 표 자리
#define DINST_DTAB_RVA 0x11C540u    // 발견물 표(92바이트 x 274) — +0x08 이 유적 번호
#define DINST_DTAB_SZ  92
#define DINST_DTAB_N   274
#define DINST_HTAB_RVA 0xD8E88u     // 힌트 표(80바이트 x 186) — +0x00 이 유적 번호
#define DINST_HTAB_SZ  80

int  DInst_Load(void);                 // 유적 번호 표를 읽어 이음표를 만든다. 성공 1
int  DInst_Ready(void);
int  DInst_Live(void);                 // 인스턴스 배열이 살아 있나(세이브를 불러왔나)

int  DInst_OfDisc(int disc);           // 발견물 번호 -> 인스턴스 번호. 없으면 -1
int  DInst_CountOfHint(int hint);      // 그 힌트에 걸린 인스턴스 수(0 이면 힌트만으로 봐야 한다)
int  DInst_OfHint(int hint, int k);    // 그 힌트에 걸린 k 번째 인스턴스. 없으면 -1

int  DInst_Filled(int inst, int slot);         // 그 칸이 채워졌나
const wchar_t* DInst_Who(int inst, int slot);  // 그 칸의 사람 이름. 비었으면 L""
int  DInst_Year(int inst, int slot);           // 그 칸의 해. 모르면 -1
int  DInst_Month(int inst, int slot);          // 그 칸의 달. 모르면 -1

// 힌트 한 줄이 어디까지 왔나 — 걸린 인스턴스를 모두 훑는다.
int  DInst_HintFound(int hint);        // 하나라도 내가 발견했나. 모르면 -1
int  DInst_HintReported(int hint);     // 하나라도 보고까지 했나. 모르면 -1
int  DInst_HintTaken(int hint);        // 남이 먼저 발견해 둔 것이 있나. 모르면 -1
