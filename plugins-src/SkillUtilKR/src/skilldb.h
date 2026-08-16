#pragma once
#include <windows.h>

// SkillUtilKR — 도시마다 배울 수 있는 기능·언어를 고친다.
//
// 게임은 (도시 x 건물) 마다 레코드를 하나씩 두고, 거기 든 32비트 마스크 하나로
// 그 자리에서 배울 수 있는 것을 정한다. 조합·교회·학자 저택이 전부 같은 칸을 쓴다.
// (분석: 옵시디안 `Project/cds95/분석/18.분석-도시별 기능·언어(조합·교회).md`)
//
//   정적 표  .rdata  VA 0x500918  1508행 x 0x38   ← 새 게임의 원본
//     +0x00 이름ptr  +0x04 종류이름ptr  +0x08 도시  +0x0C 건물코드  +0x30 마스크
//   런타임   .data   VA 0x5B0A08  1508개 x 8      ← 지금 게임이 실제로 보는 값
//     (행 수는 게임의 순회 범위에서 나온다 — 0x4737A0 이 0x5B0A08 부터 0x5B3928 까지
//      8바이트씩 훑는다. (0x5B3928 - 0x5B0A08) / 8 = 1508.)
//     +0x00 = 정적 +0x30 의 사본 (새 게임 때 0x473190 이 복사, 세이브에 들어간다)
//
//   비트 0~12  기능 13종  (이름표 VA 0x560A10)
//   비트 13~26 언어 14종  (이름표 VA 0x560A48)
//   비트 27~31 아무도 안 쓴다
//
// 그래서 고칠 자리가 둘이다.
//   · .rdata 를 고치면 → 이 다음 "새 게임" 부터 먹는다 (읽기전용이라 VirtualProtect 필요)
//   · 런타임을 고치면 → 지금 하는 게임에 바로 먹고 세이브에도 들어간다
// 둘 다 메모리만 고치는 것이라 게임을 끄면 사라진다. 그래서 skills.json 에 적어 두고
// 플러그인이 뜰 때 .rdata 에 다시 발라 준다.

#define SKILL_N   13
#define LANG_N    14
#define MASK_ALL  0x07FFFFFFu

#define BLD_COUNT     1508
#define BLD_SIZE      0x38
#define BLD_RVA       0x00100918u   // VA 0x500918 (.rdata)
#define BLD_LIVE_RVA  0x001B0A08u   // VA 0x5B0A08 (.data 의 0채움 대역)
#define BLD_LIVE_SZ   8
#define GAME_YEAR_RVA 0x001A4D20u   // VA 0x5A4D20 — 지금 연도. 세이브를 불러와야 값이 선다

#define BLD_OFF_NAME  0x00
#define BLD_OFF_KIND  0x04
#define BLD_OFF_CITY  0x08
#define BLD_OFF_CODE  0x0C
#define BLD_OFF_MASK  0x30

// 건물코드 — 이 플러그인이 다루는 것만
#define BLD_CHURCH  3
#define BLD_GUILD   9
#define BLD_HOUSE0  12   // 저택 · 상관 · 학자 저택 (12~15)
#define BLD_HOUSE1  15

// 표를 찾아 검사하고 원본 마스크를 떠 둔다. 성공 1. 여러 번 불러도 된다.
int SkillDb_Load(HINSTANCE self);
int SkillDb_Ready(void);

#define SKDB_OK        0
#define SKDB_E_MODULE  1   // 모듈 핸들을 못 얻음
#define SKDB_E_READ    2   // 그 주소를 읽을 수 없음
#define SKDB_E_ROWS    3   // 칸 내용이 건물 레코드로 안 보임(다른 빌드)
#define SKDB_E_TAIL    4   // 표 끝 다음 행까지 말이 됨(표가 밀려 잡혔다)
int SkillDb_Status(void);

int SkillDb_Count(void);                  // 1508
int SkillDb_City(int k);                  // 도시 번호. 잘못된 k 면 -1
int SkillDb_Code(int k);                  // 건물코드. 잘못된 k 면 -1
const wchar_t* SkillDb_Name(int k);       // "리스본 항해자 조합"
const wchar_t* SkillDb_KindName(int k);   // "조합" / "교회" / "학자 저택"
const wchar_t* SkillDb_CityName(int k);

unsigned SkillDb_Mask(int k);             // 지금 .rdata 에 든 값
unsigned SkillDb_OrigMask(int k);         // 플러그인이 뜰 때 떠 둔 EXE 원본
unsigned SkillDb_LiveMask(int k);         // 지금 게임이 보는 값. 게임 전이면 0
int      SkillDb_Changed(int k);          // 원본과 다른가

// 마스크를 바꾼다. .rdata 와 (게임이 떠 있으면) 런타임 둘 다에 쓴다. 성공 1.
int SkillDb_SetMask(int k, unsigned mask);

int SkillDb_GameLoaded(void);             // 세이브를 불러와 런타임 값이 서 있나

// 기능/언어 이름. bit 는 0~12 / 0~13.
const wchar_t* SkillDb_SkillName(int bit);
const wchar_t* SkillDb_LangName(int bit);

// ---- skills.json (CDS95Util\skills.json) ----
// 원본과 다른 줄만 적는다. 없으면 파일을 지운다.
int SkillDb_SaveJson(void);
// 파일을 읽어 .rdata 에 바른다. 바른 줄 수를 돌려준다(없으면 0).
int SkillDb_ApplyJson(void);
void SkillDb_JsonPath(wchar_t* out, int cch);
