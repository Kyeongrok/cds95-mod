#pragma once
#include <windows.h>

// CDS95Util\hotkeys.json 읽기 — HotkeyUtilKR 이 쓰고, 다른 플러그인은 읽기만 한다.
// (인물 창이 탭에 "스폰서(P)" 처럼 지금 걸린 키를 적으려고 읽는다. 그래서 읽는 쪽 코드를
//  복사하지 않고 이 파일을 같이 빌드한다 — itempic.c 를 TradeUtilKR 이 같이 쓰는 것과 같다.)
//
// 파일이 없으면(HotkeyUtilKR 이 안 깔렸거나 아직 안 떴으면) Read 가 NULL 을 돌려준다.
// 그때는 부르는 쪽이 키를 안 적으면 된다 — 단축키 자체가 없는 판이라는 뜻이다.

void     HkJson_Path(HINSTANCE hinst, wchar_t* out, int cch);
wchar_t* HkJson_Read(HINSTANCE hinst);          // 파일 전체를 와이드로. 없으면 NULL
void     HkJson_Free(wchar_t* buf);

int      HkJson_Enabled(const wchar_t* buf);    // "Enabled". 없으면 1
// 기능 이름에 걸린 키 이름("I" · "F3")을 out 에 담는다. 그 이름이 파일에 있으면 1.
// 있어도 값이 비어 있으면(out[0] == 0) 일부러 떼 놓은 자리다.
int      HkJson_KeyOf(const wchar_t* buf, const wchar_t* action, wchar_t* out, int cch);
