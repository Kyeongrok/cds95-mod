#pragma once
#include <windows.h>

// 플러그인 창을 게임 껍데기로 입힌다 — 단추는 베이지 띠, 제목은 진홍 장식 띠.
// 그림은 MISC.CDS 파트 4, 글자는 게임 비트맵 글꼴(ALL_FONT.16P / ANKFONT.DAT)이다.
// (형식은 miscskin.h · gamefont.h 머리말, 분석은 옵시디안 21번 노트)
//
// 쓰는 법 — 창 만들 때 한 번:
//     GameSkin_Apply(hwnd);                     // 밀어넣기 단추를 오너드로우로 바꾼다
// 창 프로시저에서:
//     case WM_DRAWITEM: if (GameSkin_DrawItem((DRAWITEMSTRUCT*)l)) return TRUE; break;
// 제목이 필요하면 WM_PAINT 에서:
//     GameSkin_Title(dc, rect, L"서적");
//
// 껍데기를 못 읽으면(다른 게임 폴더 등) 아무 것도 안 하고 0 을 돌려준다 —
// 그러면 창은 윈도 기본 단추 그대로 뜬다. 절대 죽지 않는다.

int  GameSkin_Ready(void);     // MISC.CDS 와 글꼴을 읽었나. 처음 부를 때 읽는다

// 띠의 제 높이(24). 칸을 이 높이로 잡으면 가장 깔끔하다 —
// 더 높게 잡아도 늘리지 않고 가운데에 놓지만, 위아래가 비어 헐렁해 보인다.
#define GAMESKIN_H  24

// 창의 자식 중 **밀어넣기 단추**만 오너드로우로 바꾼다.
// 라디오·체크는 건드리지 않는다(그건 윈도 것이 더 알아보기 쉽다).
void GameSkin_Apply(HWND win);

// WM_DRAWITEM 처리. 우리 단추면 그리고 1, 아니면 0.
int  GameSkin_DrawItem(const DRAWITEMSTRUCT* di);

// 제목 띠(진홍 장식). r 높이에 맞춰 늘려 그린다.
void GameSkin_Title(HDC dc, RECT r, const wchar_t* text);

// CharacterUtilKR 의 UI_Button 에 갈아 끼우는 짝.
//     UI_SetButtonDraw(GameSkin_Button);
// 그리면 1, 껍데기를 못 읽어 못 그리면 0 (그러면 UI_Button 이 원래 모양으로 그린다).
// active 면 진홍 장식 띠 + 크림 글자로 도드라지게, 아니면 베이지 띠 + 짙은 갈색.
int  GameSkin_Button(HDC dc, RECT r, const wchar_t* t, BOOL active);
