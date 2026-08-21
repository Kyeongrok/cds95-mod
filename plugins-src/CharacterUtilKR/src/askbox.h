#pragma once
#include <windows.h>

// 게임 다이얼로그를 흉내낸 [YES][NO] 판.
// 항해 중 "…출항하겠습니까?" 창과 같은 모양이다 — 짙은 자주 바탕에 크림 이중 테두리,
// 왼쪽에 말하는 사람 초상화, 아래에 게임 띠 단추 둘.
//
// 창을 따로 만들지 않는다(MessageBox 를 쓰면 윈도 대화상자가 그대로 떠서 게임 화면에서
// 튄다). 호스트 창이 제 WM_PAINT 끝에 Ask_Paint 를 부르고, 클릭·키를 Ask_Click /
// Ask_Key 에 먼저 넘겨 주면 그동안만 판이 산다 — 판이 떠 있으면 그 둘이 입력을 삼키므로
// 모달처럼 군다.
//
// 글자는 게임 비트맵 글꼴(ALL_FONT.16P / ANKFONT.DAT)로 찍고 단추는 MISC.CDS 띠로
// 그린다(uikit 의 UI_Button 을 통하므로 GameSkin_Button 이 걸려 있으면 저절로 그리 된다).
// 그 파일들을 못 읽으면 창의 GDI 글꼴·기본 단추로 물러난다 — 모양만 수수해지고 동작은 같다.

#define ASK_MAX_LINES 6

// text 는 \n 으로 줄을 나눈다(최대 ASK_MAX_LINES 줄).
// gender/faceCode 가 음수면 초상화 없이 글만 넣는다.
// 판 높이는 줄 수(와 초상화)에 맞춰 저절로 정해진다 — 한 줄짜리는 한 줄만큼만 뜬다.
void Ask_Open(const wchar_t* text, int gender, int faceCode);

// 단추 하나짜리 알림 판. 물을 것 없이 알리기만 할 때 쓴다.
// [확인] 을 누르거나 엔터·ESC 로 닫으며, 답은 늘 1 로 나온다.
void Ask_Info(const wchar_t* text, int gender, int faceCode);

int  Ask_Active(void);
void Ask_Close(void);

void Ask_Paint(HDC dc);                  // 호스트 창 그리기 맨 끝에서

// 판이 떠 있으면 1 을 돌려주고(그 입력은 판이 먹은 것이다) *answer 에
// 1 = 예 / 0 = 아니오 / -1 = 아직 을 담는다. 판이 없으면 0 을 돌려준다.
int  Ask_Click(POINT pt, int* answer);
int  Ask_Key(WPARAM wp, int* answer);    // 엔터·Y = 예, ESC·N = 아니오
