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
//                          **게임이 편성을 다 뽑은 뒤 · 그림을 읽기 직전**이라, 여기서 눌러 쓰면
//                          그림까지 그 병종으로 나온다
//   병종 -> 파트           VA 0x0044A1F0(칸, 병종) — 아군/적과 지방에 따라 다른 그림
//   칸별 그림 자리         VA 0x00444520(칸) — **0~5 는 36,864바이트(96x384),
//                          6~11 은 65,536바이트(128x512)**
//   능력치 꺼내기          VA 0x00446FF0(갈래, 칸) — 부대가 아니라 **적장 인물 레코드**의
//                          `+0x20 + 갈래*4` 를 읽는다. 전투 밖에서 부르면 죽는다
//
//   ★ 적장 알아내기
//     this + 0x98  아군 쪽 객체 ptr  ->  [+4] = 캐릭터 id
//     this + 0x9C  **적 쪽 객체 ptr  ->  [+4] = 캐릭터 id**
//     캐릭터 id = (갈래 << 12) | 번호   (0x00477C00 이 >>12, 0x00477C10 이 &0xFFF)
//       갈래 0 플레이어 0x005B60A0
//       갈래 1 인물 런타임 배열 0x0058BF90 + 번호*284 (284 = 0x11C)  ← 예니체리·맘루크가 여기
//       갈래 2 0x005B3C60 + 번호*60      갈래 3 0x00629AD8 + 번호*72
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
#define LW_ENEMY_OFF   0x9C          // CLandWar + 0x9C
#define LW_CHAR_RVA    0x0018BF90u   // VA 0x0058BF90 인물 런타임 배열(갈래 1)
#define LW_CHAR_SZ     284
#define LW_CHAR_NAME   0xB4          // 이름 19바이트 CP949 (CharacterUtilKR 과 같은 자리)

#define LW_ID_COMMON   (-1)          // "모든 적장 공통" 예약
#define LW_PRESET_MAX  24            // 적장별 예약 벌 수

int  LandWar_Load(void);            // 모듈을 잡는다. 성공 1
int  LandWar_Ready(void);

const wchar_t* LandWar_TypeName(int t);   // 못 읽으면 L"?"
const wchar_t* LandWar_TypeDesc(int t);

int  LandWar_Active(void);          // 지금 전투가 서 있나
int  LandWar_Type(int slot);              // 지금 병종. 못 읽으면 -1
int  LandWar_SetType(int slot, int t);    // 지금 당장 갈아 끼운다. 성공 1
int  LandWar_Word(int slot, int idx);     // 레코드의 idx 번째 dword(0~9)
int  LandWar_Stat(int slot, int kind);    // ※ 전투 중에만. 밖에서 부르면 죽는다

// 이번 전투의 적장. 없으면 -1. 이름은 읽히면 1(못 읽으면 out 이 비고 0).
int  LandWar_EnemyId(void);
int  LandWar_EnemyName(int id, wchar_t* out, int cap);

// 예약 — 적장 id 별로 한 벌(12칸). id = LW_ID_COMMON 이면 딱 맞는 벌이 없을 때 쓰는 공통 벌.
int  LandWar_SetPreset(int id, int slot, int t);   // t < 0 이면 그 칸은 건드리지 않음
int  LandWar_Preset(int id, int slot);
void LandWar_ClearPreset(int id);
int  LandWar_PresetCount(void);
int  LandWar_PresetIdAt(int i);

// CDS95Util\landwar.txt 로 남기고 읽는다.
int  LandWar_Save(void);
int  LandWar_LoadFile(void);

void LandWar_AllowBig(int on);
int  LandWar_BigAllowed(void);
int  LandWar_TypeOkForSlot(int slot, int t);

int  LandWar_HookInstall(void);
