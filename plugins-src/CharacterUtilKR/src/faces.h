#pragma once
#include <windows.h>

// MALE.CDS / FEMALE.CDS (LS12 압축 얼굴)를 열어두고 임의 크기로 그려준다.
// 도감 탭과 항해사 탭이 같은 파일을 쓰므로 로드는 한 번만 한다.

#define FACE_MALE   0
#define FACE_FEMALE 1

void Face_Load(void);                       // 최초 1회만 실제로 연다
void Face_Unload(void);
int  Face_Count(int gender);                // 파일에 든 얼굴 수(못 열었으면 0)

// (x,y,w,h) 에 얼굴을 늘려 그린다. gender/code 가 없거나 디코드에 실패하면
// 빈 액자(자리표시자)를 그린다. 테두리는 항상 그린다.
void Face_Draw(HDC dc, int x, int y, int w, int h, int gender, int code);
