#pragma once
#include <windows.h>

// CDS_95.EXE 의 .rdata 에 있는 후원자(스폰서) 표. 여급 표(maids.c)와 같은 성격이라
// 실행 중 메모리에서 읽고, 등장연도는 그 자리에 바로 써넣는다.
//
// 레코드 배치 (ce/CDS_95.CT 의 "후원자 정보" 그룹 + 실제 EXE 로 전수 확인. 81행, 0x3C 간격):
//   +0x00 얼굴코드   +0x04 성별(0=남 1=여)   +0x08 국적   +0x0C 직업
//   +0x10 등장연도(1480 기준 — 실제 연도 = 1480 + 값)
//   +0x1C 권력   +0x20 도시   +0x24 건물   +0x28 재력   +0x2C 감별   +0x34 취향   +0x36 언어
// 자금/친밀도 같은 것은 .data 뒷부분(실행 중에만 있는 자리)이라 여기 없다.
//
// 81행의 얼굴코드가 char_names.h 의 스폰서 칸과 그대로 맞아떨어져서, 도감의 얼굴칸에서
// 표의 행을 얼굴코드로 찾을 수 있다(이름 대조가 필요 없다).

#define PATRON_COUNT     81
#define PATRON_RVA       0x1228BCu
#define PATRON_SIZE      0x3C

// 연도 선택 범위. 게임 기준연도가 1480 이고 원본 값은 1480~1520대에 몰려 있다.
#define PATRON_YEAR_MIN  1480
#define PATRON_YEAR_MAX  1540
#define PATRON_YEAR_N    (PATRON_YEAR_MAX - PATRON_YEAR_MIN + 1)

// 연도 편집 스위치. 등장연도가 맞다는 것은 게임에서 확인했다.
// 다만 **이미 진행 중인 게임에는 반영되지 않는다** — 세이브를 불러오는 시점에 후원자 배치가
// 굳는 것으로 보인다(여급 도시 CHARKR_EDIT_CITY 에서 겪은 것과 같은 성질). 그래서
// 새 게임을 시작하기 전에 고쳐야 한다.
// (cds-helper 의 patrons.json 이 appearYear 로 적어 둔 값과도 38개 중 37개가 맞는다.
//  CE 표는 이 필드를 "등장연도관련" 이라고 뭉뚱그려 놨는데, 등장연도가 맞았다.)
#define CHARKR_EDIT_PATRON_YEAR 1

// 표를 찾아 검사한다. 성공 1 / 다른 빌드로 보이면 0. 여러 번 불러도 된다.
int Patron_Load(void);
int Patron_Ready(void);

// 그 얼굴칸의 후원자 행 번호. 없으면 -1.
int Patron_Find(int gender, int face);

int Patron_Year(int row);                  // 등장연도. 실패하면 0
int Patron_SetYear(int row, int year);     // 로드된 EXE 이미지에 직접 쓴다(파일은 그대로). 성공 1
