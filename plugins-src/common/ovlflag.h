#pragma once
#include <windows.h>

// 화면 왼쪽 위 오버레이 켬/끔 — 두 플러그인이 나눠 갖는 약속.
//
// ## 왜 나눠 갖나
//
// 게임 화면에 무언가 얹으려면 해상 렌더러(0x48A1E0)가 돌아온 자리에 훅을 걸어야 하는데,
// 그 훅은 이미 WindArrowKR 에 있다(풍향·해류 화살표). 같은 함수를 DLL 둘이 저마다
// MinHook 으로 훅하면 트램폴린이 서로를 물어 언훅 순서에 따라 게임이 죽는다. 그래서
// **그리는 일은 WindArrowKR 한 곳에만** 두고, 켜고 끄는 체크는 그 값을 가진 플러그인
// (규율은 FatigueUtilKR 의 규율 창)이 갖는다.
//
// 상태는 게임 창의 프로퍼티 한 칸이다 — modmenu.h 가 모드 창 등록부를 게임 창에 걸어
// 두는 것과 같은 결이다. 그리는 쪽은 매 프레임 이 칸만 보고, 켜는 쪽은 이 칸만 고친다.
//
// WindArrowKR 이 빠져 있으면 체크는 켜져도 아무것도 안 그려진다 — 단축키가 그렇듯
// 없는 플러그인의 몫은 그냥 일어나지 않는다.

#define OVL_DISC_PROP L"CDS95_DiscOverlay"

// 규율 오버레이가 켜져 있나.
static int OvlDisc_Get(HWND gameHwnd)
{
    return (gameHwnd && GetPropW(gameHwnd, OVL_DISC_PROP)) ? 1 : 0;
}

static void OvlDisc_Set(HWND gameHwnd, int on)
{
    if (!gameHwnd) return;
    if (on) SetPropW(gameHwnd, OVL_DISC_PROP, (HANDLE)(UINT_PTR)1);
    else    RemovePropW(gameHwnd, OVL_DISC_PROP);
}
