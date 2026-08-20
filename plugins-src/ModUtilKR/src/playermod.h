#pragma once
#include <windows.h>

// 모드 > 플레이어 수정 — 소지금과 명성을 단추로 늘렸다 줄인다.
//
// 한 줄에 걸음이 둘이다. 바깥이 큰 걸음, 안쪽이 잔걸음 —
//   소지금 ∓100,000 · ∓10,000    명성 ∓10,000 · ∓500
//
// 값은 실행 중인 게임의 메모리에 바로 들어간다(세이브 파일은 건드리지 않는다).
// 게임에서 저장하면 그대로 남는다.
//   소지금  0x1B6194   (CharacterUtilKR/src/inventory.h)
//   명성    0x1B614C   (CharacterUtilKR/src/livechar.h)
// 둘 다 .data 뒷부분이라 세이브를 불러와야 생긴다 — 그 전에는 창이 그렇다고 알린다.
//
// 창 모양은 다른 KR 창들과 같은 세피아 판(uikit)에 게임 껍데기 단추(gameskin)다.

void PlayerMod_Show(HINSTANCE hinst, HWND gameHwnd);
