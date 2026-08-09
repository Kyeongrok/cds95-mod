#pragma once
#include <windows.h>

// 발견물 — 힌트(186)와 다른 목록이다. 이름·분류·가치는 실행 중 모듈의 표에서,
// 발견 여부는 게임 폴더의 SAVEDATA.CDS 에서 읽는다. 둘 다 읽기만 한다.
//
//   발견물 표   모듈 + 0x11C540 (.rdata, 92바이트 x 274)
//               +0x00 이름 char*(cp949)   +0x04 분류(0~7)   +0x18 가치
//               (+0x44~0x50 은 좌표 — WorldMapKR 이 쓴다)
//
//   발견 여부   SAVEDATA.CDS 0x19E6A 부터 164바이트 x 250, 각 칸 첫 바이트의
//               bit6 = 발견, bit7 = 발표. (cds-helper 의 SaveDataService 와 같은 자리이고,
//               실제 세이브로 확인했다 — 켜진 칸의 이름이 위 표와 그대로 맞는다.)
//
// 왜 세이브를 읽나 — 힌트 상태는 실행 중 배열(0x18B4E0)에 있지만 발견물 쪽은 그런 자리를
// 아직 못 찾았다. 세이브는 마지막으로 저장한 시점이라, 저장 뒤에 발견한 것은 안 보인다.
// 그래서 창에 "세이브 기준" 이라고 적어 둔다. 파일은 절대 쓰지 않는다.

#define DISC_N        274
#define DISC_RVA      0x11C540u
#define DISC_SZ       92
#define DISC_SAVE_OFF 0x19E6Au
#define DISC_SAVE_SZ  164
#define DISC_SAVE_N   250

int  Disc_Load(void);                    // 표를 읽고 세이브도 있으면 읽는다. 성공 1
int  Disc_Ready(void);                   // 이름표를 읽었나
int  Disc_HaveSave(void);                // 세이브를 읽었나(발견 여부를 아는가)
int  Disc_Count(void);                   // 274

const wchar_t* Disc_Name(int i);
int  Disc_Cat(int i);                    // 0~7. 모르면 -1
int  Disc_Value(int i);                  // 가치. 모르면 -1

#define DISC_UNKNOWN  -1                 // 세이브가 없어 모른다
#define DISC_NOT      0                  // 아직 못 찾음
#define DISC_FOUND    1                  // 발견
#define DISC_REPORTED 2                  // 발견 + 발표
int  Disc_Found(int i);
