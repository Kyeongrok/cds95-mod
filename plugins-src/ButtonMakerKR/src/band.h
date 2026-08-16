#pragma once
#include <windows.h>
#include "miscskin.h"

// 띠 한 장 짓기 — 껍데기(miscskin) 위에 게임 글꼴(gamefont)로 글자를 얹는다.

#define BAND_MAX_CELLS 72                                        // 가운데 조각 되풀이 한도
#define BAND_MAX_W     (SKIN_CAP_W * 2 + SKIN_MID_W * BAND_MAX_CELLS)
#define BAND_MAX_PIX   (BAND_MAX_W * SKIN_H)

int Band_Width(int cells);          // 16 + 8*cells + 16

// 글자가 들어갈 만큼의 최소 칸 수. 양옆으로 margin 픽셀씩 띄운다.
int Band_AutoCells(const wchar_t* text, int margin);

// 8bpp 색인 그림을 idx(>= BAND_MAX_PIX)에 짓는다. 만든 폭을 돌려준다(실패 0).
// 글자는 가운데 맞춤이고, shadow 가 0 이 아니면 오른쪽 아래로 한 점 그림자를 깐다.
int Band_Build(int style, const wchar_t* text, int cells,
               unsigned char color, int shadow, unsigned char shadowColor,
               unsigned char* idx);

// 색인 그림을 24bpp 로 편다(위에서 아래로). 버퍼는 w*h*3 바이트.
void Band_ToBgr(const unsigned char* idx, int w, int h, unsigned char* bgr);  // 윈도 DIB 차례
void Band_ToRgb(const unsigned char* idx, int w, int h, unsigned char* rgb);  // PNG 차례
