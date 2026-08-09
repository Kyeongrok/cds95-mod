#pragma once
#include <windows.h>

// 단축키를 걸 수 있는 기능들. 이름 · 게임 창에 던질 메뉴 커맨드 ID · 기본키.
//
// ID 는 KR 플러그인 예약대역(0xB000~0xCFFF)에서 각 플러그인이 제 메뉴에 쓰는 값 그대로다.
//   Trade=0xB101(교역) 0xB102(교역품) 0xC0xx(워프)   Char=0xB301(정보) 0xB310+탭
//   Ship=0xB410   Patch=0xB500   Map=0xB600   Mod=0xB700   QMod=0xB800
//   Upd=0xB900    Fatigue=0xBA00   Hotkey=0xBB00(이 창)
// 0xB310+탭 은 CharacterUtilKR 이 "그 탭으로 열기"용으로 따로 받아 주는 자리다
// (character.c 의 ID_CHAR_TAB. 탭 번호는 도감0 항해사1 여급2 스폰서3 퀘스트4 소지품5 플레이어8).
//
// 이름은 hotkeys.json 의 열쇠이기도 하다 — 여기서 이름을 고치면 예전 json 의 그 줄은 무시된다.

typedef struct { const wchar_t* name; UINT id; int defKey; } HkAction;

static const HkAction kActions[] = {
    { L"정보",          0xB301, 'I' },
    { L"항해사 찾기",   0xB311, 'N' },
    { L"퀘스트",        0xB314, 'Q' },
    { L"소지품",        0xB315, 'B' },
    { L"여급",          0xB312, 'M' },
    { L"스폰서",        0xB313, 'P' },
    { L"도감",          0xB310, 'G' },
    { L"플레이어",      0xB318, 'H' },
    { L"교역",          0xB101, 'T' },
    { L"교역품",        0xB102, 'O' },
    { L"지도",          0xB600, 'W' },
    { L"함선",          0xB410, 'S' },
    { L"피로도",        0xBA00, 'F' },
    { L"패치",          0xB500, 'C' },
    { L"퀘스트 모드",   0xB800, 'K' },
    { L"플러그인 관리", 0xB700, 'L' },
    { L"업데이트",      0xB900, 'U' },
    { L"단축키",        0xBB00, 'J' },
};

#define ACT_N ((int)(sizeof(kActions)/sizeof(kActions[0])))
