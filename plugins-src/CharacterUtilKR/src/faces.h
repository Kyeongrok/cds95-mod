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

// ---- 초상화 내보내기 / 갈아 끼우기 ----
// 게임 얼굴은 80x96 8bpp 인덱스이고, 색은 face_palette.h 의 팔레트로 입힌다.
// 그 팔레트는 게임 화면을 캡처해 역산한 "근사값"이라, PNG 로 내보냈다가 그대로 다시
// 넣으면 색이 아주 조금 달라질 수 있다(모양은 그대로다).
//
// 넣기/추가는 게임 폴더의 MALE.CDS / FEMALE.CDS 를 다시 쓴다.
//   · 맨 처음 한 번 <이름>.CDS.orig 로 원본을 남긴다(이미 있으면 그대로 둔다)
//   · 바꾸는 파트만 새로 인코딩하고 나머지는 압축 바이트를 그대로 옮긴다(파일이 안 분다)
//   · 쓰기 전에 새 파트를 도로 풀어 원본과 같은지 확인한다. 어긋나면 파일을 건드리지 않는다
//   · 성공하면 얼굴 파일을 다시 열어 창에 바로 보이게 한다
#define FACE_ERR_OK      0
#define FACE_ERR_GDIP    1   // gdiplus.dll 을 못 씀 — PNG 를 못 읽고 못 씀
#define FACE_ERR_IMAGE   2   // 그림 파일을 못 열었다
#define FACE_ERR_ARCHIVE 3   // 얼굴 파일(MALE/FEMALE.CDS)이 안 열려 있다
#define FACE_ERR_ENCODE  4   // 다시 묶다가 실패(사전이 순열이 아니거나 버퍼 부족)
#define FACE_ERR_VERIFY  5   // 새로 만든 것이 도로 안 풀린다 — 파일은 안 건드렸다
#define FACE_ERR_WRITE   6   // 파일 쓰기 실패
#define FACE_ERR_RANGE   7   // 얼굴 번호가 표 밖

// 얼굴 하나를 PNG 로 내보낸다(80x96). 성공 FACE_ERR_OK.
int Face_ExportPng(int gender, int code, const wchar_t* path);

// PNG(그 밖의 그림도 된다)를 80x96 으로 줄여 그 자리 얼굴을 갈아 끼운다.
int Face_ImportPng(int gender, int code, const wchar_t* path);

// 갈아 끼우지 않고 표 끝에 새 얼굴로 붙인다. 새 얼굴코드를 newCode 로 돌려준다.
int Face_AppendPng(int gender, const wchar_t* path, int* newCode);
