#pragma once
#include <windows.h>

// 숙박 일수를 물어보는 모달 입력창을 띄우고 입력값(1~999)을 반환한다.
// 취소/실패 시 defaultDays 를 그대로 반환(= 원래 동작 유지).
// 게임 메인 스레드(Rest 훅 내부)에서 동기적으로 호출된다 — DialogBox 자체 메시지 루프가
// 돌기 때문에 훅 안에서 블로킹해도 창이 정상 동작한다.
int HotelKR_AskDays(int defaultDays);
