#pragma once
#include <windows.h>

// 인물 창의 공통 레이아웃/그리기 도우미.
// 창 하나를 "도감" 탭(character.c)과 "항해사 찾기" 탭(navview.c)이 나눠 쓴다.

// "항해사 찾기" 탭 노출 스위치. 0 으로 두면 그 탭만 사라지고 [여급][도감]은 남는다.
// navview/savedata 코드는 그대로 남으므로 1 로 되돌리면 그대로 복구된다.
#define CHARKR_SHOW_NAV_TAB 1

#define FRAME     3
#define TITLE_H   26
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
#define GY        (FRAME + TITLE_H + TAB_H + FILTER_H + GAP)   // 탭 표시 95 / 숨김 69
#define GAL_H     (ROWS_VIS * ROW_PITCH)   // 520

#define WIN_W     (GX + COLS*CELL_W + (COLS-1)*GAP + GAP + SB_W + FRAME)   // 760
#define WIN_H     (GY + GAL_H + FRAME)                                     // 탭 표시 618 / 숨김 592

// 탭바 / 필터바 기준선
#define TAB_Y     (FRAME + TITLE_H)
#define FILTER_Y  (FRAME + TITLE_H + TAB_H + 5)

#define COL_BG        RGB(150,130,105)
#define COL_FACE_TOP  RGB(216,201,176)
#define COL_FACE_BOT  RGB(158,138,113)
#define COL_LIGHT     RGB(238,228,208)
#define COL_DARK      RGB( 90, 75, 60)
#define COL_TEXT      RGB( 55, 40, 25)
#define COL_DISP_BG   RGB(206,194,171)
#define COL_SEL_BG    RGB(120,100, 80)   // 활성 필터 버튼
#define COL_ROW_ALT   RGB(196,183,159)   // 목록 짝수 행
#define COL_WARN_BG   RGB(214,180,150)   // 명성 부족 행
#define COL_WARN_TX   RGB(150, 55, 20)
#define COL_LANG_TX   RGB( 30, 70,130)   // 언어 특기

extern HFONT g_font;        // 굵은 14px (제목/버튼/이름)
extern HFONT g_smallFont;   // 12px (상세)

void UI_CreateFonts(void);
void UI_DestroyFonts(void);

void UI_VGradient(HDC dc, RECT r, COLORREF top, COLORREF bot);
void UI_Bevel(HDC dc, RECT r, BOOL sunken);
void UI_Button(HDC dc, RECT r, const wchar_t* t, BOOL active);
void UI_Text(HDC dc, RECT r, const wchar_t* t, HFONT f, COLORREF c, UINT fmt);

// 콤보박스처럼 보이는 눌림 상자 + 오른쪽 ▼. 펼친 목록은 쓰는 쪽이 직접 그린다.
// (게임 DirectDraw 화면 위에서 COMBOBOX 자식 컨트롤이 불안정해 전부 직접 그린다.
//  navview 의 특기/Lv 필터와 도감의 여급 등장연도가 같이 쓴다.)
void UI_Select(HDC dc, RECT r, const wchar_t* text, BOOL open);

// 세로 스크롤바(트랙+썸)를 그린다. 클릭 판정은 호출한 쪽에서 track 으로 한다.
void UI_Scrollbar(HDC dc, RECT track, int scroll, int maxScroll, int visRows, int totalRows);
