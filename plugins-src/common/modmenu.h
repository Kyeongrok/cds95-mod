#pragma once
#include <windows.h>

// 모드 창(ModWindowKR)의 등록부 — KR 플러그인이 나눠 쓰는 약속이다.
//
// ## 왜 필요한가
//
// 플러그인이 스무 개를 넘으면서 "파일" 메뉴가 게임 원래 항목보다 우리 것으로 더 길어졌다.
// 그래서 ModWindowKR 이 파일 메뉴의 "모드" 를 **하위 메뉴 없는 단추**로 바꾸고, 누르면
// 창을 띄워 항목을 단추로 늘어놓는다. 그 창이 읽는 목록이 등록부다.
//
// 등록부는 메뉴바에 걸리지 않은 떠 있는 메뉴라, 사람 눈에는 안 보이고 창에만 쓰인다.
// ModWindowKR 이 만들어 게임 창의 프로퍼티로 걸어 둔다.
//
// ## 플러그인이 할 일 — 검사에 등록부를 더한다
//
// 플러그인은 1초마다 "내 항목이 메뉴에 있나" 를 보고 없으면 다시 단다. 파일 메뉴만
// 훑으면 옮겨 간 뒤로는 늘 "없다" 가 나와 끝없이 다시 달게 된다(ModUtilKR 주석에 그
// 사고 기록이 있다). 그래서 붙이기 전에 **등록부도 함께** 본다.
//
//     if (!MenuHasId(target, ID_X) && !ModMenu_HasId(g_gameHwnd, ID_X)) { ... 붙인다 ... }
//
// 붙이는 자리는 예전 그대로 파일 메뉴여도 된다 — ModWindowKR 이 곧 등록부로 걷어간다.
// 사람이 파일 메뉴를 여는 순간에도 한 번 걷으므로 옮겨지는 모습은 눈에 띄지 않는다.
//
// ModWindowKR 이 없으면 프로퍼티도 없어 ModMenu_HasId 가 늘 FALSE 다. 그러면 예전과
// 똑같이 파일 메뉴에 붙고 그대로 쓰인다 — 이 헤더를 넣었다고 그쪽에 매이지는 않는다.

// ## 메뉴 ID 표 — 새로 만들 때 여기서 빈 자리를 고른다
//
// 예전에는 파일마다 주석으로 "누가 무엇을 쓴다" 를 적어 두었는데, 그 목록이 저마다
// 낡아 0xB800 과 0xB900 을 두 번씩 겹쳐 쓰는 사고가 났다. 겹치면 서로의 항목을
// 제 것으로 잘못 알아본다 — 붙여야 할 것을 안 붙이거나, 남의 것을 지운다.
//
//   0xB101 교역시세    0xB102 교역품     0xB103 워프
//   0xB301 정보        0xB310+ 정보 탭
//   0xB410 함선(스킨)  0xB500 패치       0xB600 지도
//   0xB700 플러그인 관리   0xB701 플레이어 수정
//   0xB800 퀘스트 모드 0xB900 업데이트   0xBA00 피로도
//   0xBB00 단축키      0xBC00 힌트       0xBD00 매매
//   0xBE00 저장        0xBE01 중단       0xBF00 도시그림
//   0xC000+ 워프 목적지(도시 번호를 더해 쓴다 — 226칸을 먹는다)
//   0xC100 대사        0xC200 기능수련   0xC300 서적
//   0xC400 선체        0xC500 버튼 만들기 0xC600 육상전 부대
//   0xC700 풍향 화살표 0xC800 모드 창
//
// 다음 빈 자리: 0xC900.

#define MODMENU_PROP L"CDS95_ModMenu"

// 등록부 메뉴. 아직(또는 영영) 없으면 NULL.
static HMENU ModMenu_Handle(HWND gameHwnd)
{
    return gameHwnd ? (HMENU)GetPropW(gameHwnd, MODMENU_PROP) : NULL;
}

// 등록부(하위까지)에 그 ID 가 있나.
static BOOL ModMenu_MenuHasId(HMENU m, UINT id)
{
    int n, i;
    if (!m) return FALSE;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        if (sub) { if (ModMenu_MenuHasId(sub, id)) return TRUE; continue; }
        if (GetMenuItemID(m, (UINT)i) == id) return TRUE;
    }
    return FALSE;
}

// 이 창의 등록부에 그 ID 가 이미 올라 있나. 플러그인은 이것만 부르면 된다.
static BOOL ModMenu_HasId(HWND gameHwnd, UINT id)
{
    return ModMenu_MenuHasId(ModMenu_Handle(gameHwnd), id);
}
