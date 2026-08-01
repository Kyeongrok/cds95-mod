#pragma once
#include <windows.h>

// KR 플러그인 창들이 나눠 쓰는 그리기 도구 — 세피아 색표 + 직접 그린 위젯.
// 창마다 다른 배치(칸 크기, 목록 폭 같은 것)는 여기 두지 않는다. 그건 각 창의 ui.h 몫이다.
//
// 자식 컨트롤(BUTTON/EDIT/COMBOBOX)을 쓰지 않고 전부 직접 그린다.
// 게임의 DirectDraw 화면 위에서 자식 컨트롤이 불안정했기 때문이다.

#define FRAME     3
#define TITLE_H   26

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
void UI_Select(HDC dc, RECT r, const wchar_t* text, BOOL open);

// 세로 스크롤바(트랙+썸)를 그린다. 클릭 판정은 호출한 쪽에서 track 으로 한다.
void UI_Scrollbar(HDC dc, RECT track, int scroll, int maxScroll, int visRows, int totalRows);

// 창 테두리 + 타이틀바 + 오른쪽 × 버튼. × 의 사각형을 closeOut 으로 돌려준다
// (클릭 판정에 쓰라고). title 이 NULL 이면 타이틀바만 그린다.
void UI_WindowFrame(HDC dc, RECT client, const wchar_t* title, RECT* closeOut);
