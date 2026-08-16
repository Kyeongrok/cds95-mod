#pragma once
#include <windows.h>

// 게임 비트맵 글꼴 — 화면에 찍히는 것과 똑같은 글자를 얻는다.
//
// ALL_FONT.16P (104,340바이트) — 30바이트 레코드 3,478개
//     +0  u16 코드   리틀엔디언. 값은 KS X 1001 두 바이트를 빅엔디언으로 본 수
//                    (CP949 로 "가" = B0 A1 이면 코드 0xB0A1, 파일에는 A1 B0 으로 들어 있다)
//     +2  28바이트   16폭 x 14행 1bpp, 한 행이 2바이트, MSB 가 왼쪽
//     코드 범위 0xA1A1~0xC8FE — 기호와 완성형 한글 2,350자. 한자는 없다.
//
// ANKFONT.DAT (1,536바이트) — 16바이트 글리프 96개
//     8폭 x 16행 1bpp, 한 행이 1바이트. ASCII 0x20(빈칸)부터 0x7F 까지 차례로.
//
// 게임도 이 두 파일을 이 순서로 읽는다(CDS_95.EXE 0x4109C3 / 0x4109CD,
// 문자열 "C:ALL_FONT.16P" = 0x535CCC · "C:ANKFONT.DAT" = 0x535CBC).

#define GF_HAN_W 16
#define GF_HAN_H 14
#define GF_ANK_W  8
#define GF_ANK_H 16
#define GF_MAX_W 16     // 글리프 버퍼는 16*16 이면 넉넉하다
#define GF_MAX_H 16

int  GameFont_Load(void);    // 실행 파일 폴더에서 두 파일을 연다(이미 열었으면 그대로). 성공 1
void GameFont_Free(void);
int  GameFont_Ready(void);

// 한 글자를 mask(>= GF_MAX_W*GF_MAX_H 바이트)에 1=획 / 0=바탕으로 푼다.
// 찾으면 1 을 주고 *w,*h 에 크기를 담는다. 글꼴에 없는 글자면 0(그 자리는 건너뛴다).
int  GameFont_Glyph(wchar_t ch, unsigned char* mask, int* w, int* h);

// 문자열을 찍는 데 드는 폭(픽셀). 없는 글자는 0폭으로 센다.
int  GameFont_TextWidth(const wchar_t* s);
