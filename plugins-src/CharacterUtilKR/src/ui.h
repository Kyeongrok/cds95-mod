#pragma once
#include "uikit.h"    // 세피아 색표 + 직접 그린 위젯(다른 KR 플러그인 창과 공용)

// 인물 창의 배치. 창 하나를 [항해사 찾기](navview.c) / [여급] / [도감](character.c) 이 나눠 쓴다.

// "항해사 찾기" 탭 노출 스위치. 0 으로 두면 그 탭만 사라지고 [여급][도감]은 남는다.
// navview/savedata 코드는 그대로 남으므로 1 로 되돌리면 그대로 복구된다.
#define CHARKR_SHOW_NAV_TAB 1

#define TAB_H     26
#define FILTER_H  30
#define GAP       10
#define SB_W      12

// ---- 도감(갤러리) 탭 ----
#define PORT_W    100                      // 초상화 80x96 -> 100x120 (x1.25)
#define PORT_H    120
#define INFO_W    248                      // 정보 패널 폭
#define CELL_W    (PORT_W + 8 + INFO_W)    // 356
#define CELL_H    120
#define COLS      2
#define ROWS_VIS  4
#define ROW_PITCH (CELL_H + GAP)           // 130
#define GX        (FRAME + GAP)            // 13
#define GY        (FRAME + TITLE_H + TAB_H + FILTER_H + GAP)   // 95
#define GAL_H     (ROWS_VIS * ROW_PITCH)   // 520

#define WIN_W     (GX + COLS*CELL_W + (COLS-1)*GAP + GAP + SB_W + FRAME)   // 760
#define WIN_H     (GY + GAL_H + FRAME)                                     // 618

// 탭바 / 필터바 기준선
#define TAB_Y     (FRAME + TITLE_H)
#define FILTER_Y  (FRAME + TITLE_H + TAB_H + 5)
