#pragma once

// WindArrowKR — 항해 화면 바다 위에 풍향·해류 화살표를 얹는다.
//
// cds-helper 는 자기 지도 창에 D3D 셰이더로 화살표를 그린다(FlowArrows.cs). 이쪽은 게임
// 화면 자체에 얹는 것이라 방식이 다르다 — 게임이 배·말을 그릴 때 쓰는 그 함수를 그대로
// 불러 우리 화살표를 찍는다. 그래서 팔레트도, 화면효과 켬/끔 갈래도, 클리핑도 전부
// 게임이 알아서 한다.
//
// 근거는 옵시디안 [[12.분석-WORLD.CDS 화면 그리기(런타임)]] · [[16.분석-풍향·풍속·해류]] ·
// [[31.분석-바다 물결과 구름]] 과 이 파일을 쓰며 새로 뜯은 0x48AC30 / 0x4BA5F4 다.

#define GA_BASE            0x00400000u

// ---- 훅 대상 --------------------------------------------------------------
// 해상 화면 렌더러. __thiscall void(int), ret 4. this = 지도 객체(0x61B2D0).
// 타일을 다 찍고 함대·구름·글자까지 그린 뒤 돌아온다 → 리턴 직후가 우리 차례다.
#define RVA_RENDER         0x0008A1E0u     // 0x0048A1E0   sub esp,0x180

// ---- 빌려 쓰는 게임 함수 --------------------------------------------------
// 스프라이트 찍기. __thiscall, ret 0x1C.
//   Draw(this, 서피스, 그림, 프레임, x, y, w, h)
// 화면효과를 켰으면 DirectDraw Blt(색인 0 투명, DDBLT_KEYSRCOVERRIDE + 빈 DDBLTFX),
// 껐으면 소프트 경로로 그림 포인터를 픽셀 단위로 옮긴다. 어느 쪽이든 화면 y 에 +0x20 을
// 자동으로 더한다(위 32점은 상태 띠다) — 우리는 바다 쪽 좌표만 넘기면 된다.
#define RVA_DRAWSPRITE     0x0008AC30u     // 0x0048AC30   sub esp,0x8c

// 서피스 만들기. __cdecl int(DDSURFACEDESC*, IDirectDrawSurface**, 0). 0 이면 성공.
// 속은 [0x62D360] 의 IDirectDraw 로 CreateSurface(vtable+0x18) 를 부르는 것뿐이다.
#define RVA_MAKESURFACE    0x000BA5F4u     // 0x004BA5F4   mov eax,[0x62d360]

// ---- 읽기만 하는 자리 -----------------------------------------------------
// 바다상태. 워드 두 개뿐이다 — +0x00 바람, +0x02 해류.
// 항해 루프가 발짝마다 0x424E50 으로 다시 굴려 넣는다.
#define GA_SEA_STATE       0x00586168u

// 화면 갈래 잣대. 렌더러 첫머리(0x48A213)가 [0x5A4D18] & 0x18 을 보고, 서 있지 않으면
// 타일을 아예 안 그리고 단색으로 칠하고 나간다(지도를 안 그리는 화면). 그 갈래에서는
// 우리도 손을 떼야 한다 — 바다가 없는 자리에 화살표만 뜨면 볼썽사납다.
#define GA_SCREEN_MODE     0x005A4D18u
#define SCREEN_MODE_MAP    0x18u


static void* GA(unsigned rva)
{
    return (void*)((unsigned char*)GetModuleHandleW(NULL) + rva);
}
