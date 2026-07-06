#pragma once
#include <windows.h>

// 게임 메인 창을 찾아 상단 메뉴에 "교역" 메뉴를 설치한다(백그라운드 스레드에서 완료).
// hinst: 플러그인 모듈 핸들(시세 창 클래스 등록/생성에 사용).
void TradeKR_Init(HINSTANCE hinst);
