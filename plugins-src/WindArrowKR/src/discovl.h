#pragma once
#include <windows.h>

// 규율 오버레이 — 화면 왼쪽 위에 "규율 87" 을 얹는다.
//
// 규율은 하루가 갈 때마다 깎이는데 게임 화면 어디에도 안 나온다(옵시디안
// 70.분석-규율(대원 불만과 하루 셈)). 그래서 화살표와 같은 자리에 숫자로 띄운다.
//
// 그림은 게임에서 가져올 데가 없으니 GDI 로 글자를 찍어 8bpp 색인 그림으로 옮긴다 —
// 색인 0 이 투명이라는 규칙만 지키면 게임 스프라이트 함수가 그대로 찍어 준다(arrow.h 와 같다).
//
// 켜고 끄는 것은 FatigueUtilKR 의 규율 창 체크박스다. 상태는 게임 창 프로퍼티에 있다
// (common/ovlflag.h). 여기서는 그 칸만 본다.

#define DISCOVL_W  120
#define DISCOVL_H  22

// 지금 규율로 그림을 맞춰 두고 8bpp 색인 그림을 돌려준다. 값을 못 읽으면 NULL.
const unsigned char* DiscOvl_Bitmap(void);

// 그림이 마지막으로 바뀐 번호. 서피스를 다시 올릴지 판단하는 데 쓴다.
int DiscOvl_Serial(void);
