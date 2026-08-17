#pragma once
#include <windows.h>

// 육상전 부대 — 병종을 갈아 끼운다.
// (분석: 옵시디안 `Project/cds95/분석/22.분석-애니메이션(MPEFFECT·EVANIME).md` 의 전투 그림 절)
//
//   전역 객체 `CLandWar`   VA 0x005A47E8   (문자열 "CLandWar::" 0x0056D1A8)
//   부대 배열              this + 0xAC = VA 0x005A4894, **40바이트 x 12**
//                          0~5 = 아군, 6~11 = 적 (0x004485F5 의 `cmp esi, 6`)
//   +0x04                  **병종 (0~23)**  — 0x00444750 이 이 값으로 이름표를 찾는다
//   병종 이름표            VA 0x00549AB8 (24칸, char* CP949)
//   병종 설명표            VA 0x00549B20 (24칸)
//   부대 그림 읽기         VA 0x0044A0D0  __thiscall(this), 전투 시작 때 한 번.
//                          12칸을 돌며 `+0x04`(병종)로 LandData.CDS 파트를 골라 읽는다
//   병종 -> 파트           VA 0x0044A1F0(칸, 병종) — 아군/적과 지방에 따라 다른 그림
//   칸별 그림 자리         VA 0x00444520(칸) — **0~5 는 36,864바이트(96x384),
//                          6~11 은 65,536바이트(128x512)**
//   능력치 꺼내기          VA 0x00446FF0  __thiscall(this, 갈래, 칸)  — 1 지력 / 2 무력
//
// ※ 낙타병(6)·코끼리병(7)만 그림이 65,536바이트다. 아군 칸(0~5)에 넣으면 36,864바이트
//    자리에 그만큼 써서 **옆 칸 그림이 깨진다**. 그래서 기본으로 막아 둔다.

#define LW_UNIT_N     12
#define LW_MINE_N     6
#define LW_TYPE_N     24
#define LW_UNIT_SZ    40
#define LW_TYPE_OFF   0x04
#define LW_BIG_A      6      // 낙타병
#define LW_BIG_B      7      // 코끼리병

// RVA = VA - 0x400000. 한 자리만 밀려도 엉뚱한 코드에 훅이 박히니 꼭 다시 세어 볼 것
// (LW_LOADSPR_RVA 를 0x000A00D0 으로 잘못 적어 게임이 즉사한 적이 있다).
#define LW_OBJ_RVA     0x001A47E8u   // VA 0x005A47E8
#define LW_UNITS_RVA   0x001A4894u   // VA 0x005A4894 (= 객체 +0xAC)
#define LW_NAME_RVA    0x00149AB8u   // VA 0x00549AB8
#define LW_DESC_RVA    0x00149B20u   // VA 0x00549B20
#define LW_LOADSPR_RVA 0x0004A0D0u   // VA 0x0044A0D0
#define LW_STAT_RVA    0x00046FF0u   // VA 0x00446FF0

int  LandWar_Load(void);            // 모듈을 잡는다. 성공 1
int  LandWar_Ready(void);

const wchar_t* LandWar_TypeName(int t);   // 못 읽으면 L"?"
const wchar_t* LandWar_TypeDesc(int t);

// 지금 전투가 서 있나 — 부대 배열에 병종이 하나라도 서 있으면 1.
int  LandWar_Active(void);

int  LandWar_Type(int slot);              // 지금 병종. 못 읽으면 -1
int  LandWar_SetType(int slot, int t);    // 지금 당장 갈아 끼운다. 성공 1
int  LandWar_Word(int slot, int idx);     // 레코드의 idx 번째 dword(0~9)
int  LandWar_Stat(int slot, int kind);    // 게임 함수를 그대로 부른다(1 지력 / 2 무력)

// 전투 시작 때 강제할 병종 — 슬롯 0~5. -1 이면 건드리지 않는다.
void LandWar_SetPreset(int slot, int t);
int  LandWar_Preset(int slot);
void LandWar_ClearPresets(void);

// 아군 칸에 큰 그림 병종(낙타·코끼리)을 넣도록 풀어 준다. 기본 0.
void LandWar_AllowBig(int on);
int  LandWar_BigAllowed(void);
int  LandWar_TypeOkForSlot(int slot, int t);

// 0x0044A0D0 에 훅을 건다 — 그림을 읽기 직전에 예약한 병종을 눌러 쓴다.
// 그래야 그림까지 그 병종으로 읽힌다. 성공 1.
int  LandWar_HookInstall(void);
