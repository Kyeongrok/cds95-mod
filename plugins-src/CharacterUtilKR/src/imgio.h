#pragma once
#include <windows.h>

// PNG 읽기/쓰기. GDI+ 를 실행 중에 LoadLibrary 로 부른다 —
// gdiplus.h 는 C++ 전용이라 이 C 코드에서 include 할 수 없고, 링크로 묶으면 gdiplus.dll 이
// 없는 환경에서 플러그인 자체가 안 뜬다. 없으면 이 기능만 조용히 못 쓰게 하는 편이 낫다.
//
// 픽셀은 늘 위에서 아래로, 한 점당 3바이트 R,G,B 순이다(윈도 DIB 의 B,G,R 과 반대라
// 안에서 뒤집어 준다). 버퍼 크기는 w*h*3.

int Img_Available(void);   // GDI+ 를 쓸 수 있나. 못 쓰면 0

// 그림 파일(PNG/BMP/JPG/GIF)을 열어 w x h 로 줄여 담는다. 성공 1.
// 가로세로비는 맞추지 않고 통째로 늘린다 — 얼굴 자리가 80x96 으로 고정이라 그 틀에 맞춘다.
// 투명한 곳은 bgR/bgG/bgB 로 깐다.
int Img_LoadScaled(const wchar_t* path, int w, int h,
                   int bgR, int bgG, int bgB, unsigned char* rgb);

// w x h RGB 를 PNG 로 쓴다. 성공 1.
int Img_SavePng(const wchar_t* path, int w, int h, const unsigned char* rgb);
