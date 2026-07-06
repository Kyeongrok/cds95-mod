#pragma once
#include <windows.h>

// KOEI LS11/LS12 압축 얼굴 파일(MALE.CDS/FEMALE.CDS) 디코더.
// 각 얼굴은 80x96, 8bpp 인덱스(7680바이트).

#define LS12_FACE_W 80
#define LS12_FACE_H 96
#define LS12_FACE_SZ (LS12_FACE_W * LS12_FACE_H)

typedef struct {
    unsigned char* data;   // 파일 전체
    long           size;
    unsigned char  dict[256];
    int            count;              // 얼굴 수
    unsigned       comp[512];         // 파트별 압축크기
    unsigned       uncomp[512];       // 원본크기
    unsigned       off[512];          // 파일 내 오프셋
} Ls12File;

// path 의 LS11/LS12 파일을 열어 파트 테이블을 파싱한다. 성공 1, 실패 0.
int  Ls12_Open(Ls12File* f, const char* path);
void Ls12_Close(Ls12File* f);

// index 얼굴을 out(>=7680바이트)에 8bpp 인덱스로 디코드한다. 성공 1.
int  Ls12_DecodeFace(Ls12File* f, int index, unsigned char* out);
