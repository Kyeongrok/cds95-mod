#pragma once
#include "uikit.h"    // 세피아 색표 + 직접 그린 위젯(다른 KR 플러그인 창과 공용)

// 인물 창의 배치. 창 하나를 [항해사 찾기](navview.c) / [여급] / [도감](character.c) 이 나눠 쓴다.

// "항해사 찾기" 탭 노출 스위치. 0 으로 두면 그 탭만 사라지고 [여급][도감]은 남는다.
// navview/savedata 코드는 그대로 남으므로 1 로 되돌리면 그대로 복구된다.
#define CHARKR_SHOW_NAV_TAB 1

// "플레이어" 탭 노출 스위치. 0 으로 두면 탭 단추만 빠지고 playerview/livechar 코드는 남는다.
//
// 한동안 0 이었다 — 얼굴코드(0x1B60A8 +0x00)를 고쳐도 화면이 안 따라오는 것으로 봤기 때문이다.
// 실은 따라온다. 게임이 36세부터 얼굴코드에 16 을 더해 그리는 것이라(0x47CAF0 — playerview.c 의
// PL_ELDER_* 참고) #0 을 골라도 #16 이 나왔던 것뿐이다. 그래서 다시 켠다.
// 얼굴 파일을 되쓰는 [PNG 넣기]·[끝에 추가]는 playerview.c 의 PL_IMPORT_ENABLED 로 잠가 뒀다.
#define CHARKR_SHOW_PLAYER_TAB 1

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

// ---- 항해사 찾기 탭 ----
// 필터바 아래에 주인공이 데리고 다니는 네 자리(부관/항해사/측량사/통역) 줄이 하나 더 있어
// 목록이 그만큼 내려간다. 창 높이는 두 탭 중 더 큰 쪽에 맞춘다.
#define CREW_H    28
#define CREW_Y    (FRAME + TITLE_H + TAB_H + FILTER_H + 1)
// 소지품처럼 두 열로 늘어놓는다 — 한 칸에 초상화 + 이름·명성 + 능력치 + 특기 + 소재 + 언어.
#define NAV_COLS  2
#define NAV_ROW_H 102
#define NAV_ROWS  5                                                        // 보이는 줄 수
#define NAV_PAGE  (NAV_ROWS * NAV_COLS)                                    // 한 판에 10명
#define NAV_H     (NAV_ROW_H * NAV_ROWS)                                   // 510
#define NAV_Y     (FRAME + TITLE_H + TAB_H + FILTER_H + CREW_H + 6)        // 119

// ---- 퀘스트 탭 ----
// 한 줄에 [상태 배지][장소·조건 / 소문 한 줄 / 요구·보수] 세 줄을 넣는다.
#define Q_ROW_H   62
#define Q_ROWS    8
#define Q_Y       (FRAME + TITLE_H + TAB_H + FILTER_H + 6)   // 91
#define Q_LIST_H  (Q_ROW_H * Q_ROWS)                         // 496

// ---- 플레이어 탭 ----
// 위쪽에 지금 초상화를 크게 한 장(+ 옆에 인물 요약), 아래는 골라 끼우는 얼굴 격자다.
// 초상화 원본이 80x96 이라 가로세로비를 지켜 키운다(133x160 = x1.66, 썸네일 60x72 = x0.75).
#define PL_Y       (FRAME + TITLE_H + TAB_H + FILTER_H + 6)   // 91
#define PL_PORT_W  133
#define PL_PORT_H  160
#define PL_HEAD_H  170                                        // 초상화 + 아래 여백
#define PL_GX      (FRAME + GAP)                              // 13
#define PL_GY      (PL_Y + PL_HEAD_H)                         // 261
#define PL_THUMB_W 60
#define PL_THUMB_H 72
#define PL_CELL_W  (PL_THUMB_W + 6)                           // 66
#define PL_CELL_H  (PL_THUMB_H + 16)                          // 88 (아래 16 은 번호 줄)
#define PL_COLS    11                                         // 13 + 11*66 = 739 <= 스크롤바 왼쪽
#define PL_ROWS    4                                          // 261 + 4*88 = 613 <= 창 아래끝
#define PL_MAX     512                                        // MALE.CDS 쪽 얼굴 수가 상한

#define WIN_W     (GX + COLS*CELL_W + (COLS-1)*GAP + GAP + SB_W + FRAME)   // 760
#define GAL_WIN_H (GY + GAL_H + FRAME)                                     // 618
#define NAV_WIN_H (NAV_Y + NAV_H + FRAME)                                  // 634
#define Q_WIN_H   (Q_Y + Q_LIST_H + FRAME)                                 // 590
#define WIN_H     (GAL_WIN_H > NAV_WIN_H ? GAL_WIN_H : NAV_WIN_H)

// 퀘스트 목록 폭은 창이 정해진 뒤에야 잴 수 있다(WIN_W 가 갤러리 기준으로 정해진다).
#define Q_X       (FRAME + GAP)                             // 13
#define Q_W       (WIN_W - Q_X - GAP - SB_W - FRAME)        // 722

// 탭바 / 필터바 기준선
#define TAB_Y     (FRAME + TITLE_H)
#define FILTER_Y  (FRAME + TITLE_H + TAB_H + 5)
