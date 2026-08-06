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

// 파트 하나를 있는 그대로 푼다(얼굴 크기로 자르지 않는다). 쓴 바이트 수, 실패 0.
// 이벤트 스크립트(퀘스트/HIST_EV 등)를 읽는 데 쓴다.
int  Ls12_DecodePart(Ls12File* f, int index, unsigned char* out, unsigned outcap);

// 파트의 원본(압축 해제) 크기. 버퍼를 잡을 때 쓴다.
unsigned Ls12_PartSize(Ls12File* f, int index);

// 푼 파트들을 다시 LS12 파일로 묶는다. 만들어진 바이트 수, 실패 0.
// 원본 인코더(天翔記 LS11Archiever)와 똑같이 실제 압축은 하지 않는다 — 사전이 항등이고
// LZ 매치를 찾지 않아 바이트 하나가 가변길이 코드 하나가 된다. 그래서 결과가 원본보다
// 2배 남짓 커지지만 게임은 그대로 읽는다(백동수 모드의 HIST_EV.CDS 가 같은 방식이다).
// 필요한 버퍼 크기는 Ls12_BuildCap 으로 미리 잡는다.
unsigned Ls12_BuildCap(const unsigned* lens, int count);
unsigned Ls12_Build(unsigned char* const* parts, const unsigned* lens, int count,
                    unsigned char* out, unsigned outcap);

// ---- 파트 하나만 갈아 끼우기 ----
// Ls12_Build 로 얼굴 파일을 다시 묶으면 414개를 전부 다시 인코딩하게 되어 MALE.CDS 가
// 1.7MB -> 8MB 로 분다. 얼굴 한 장을 바꾸려고 파일 전체를 그렇게 만들 이유가 없어서,
// 건드리지 않는 파트는 "압축된 바이트를 그대로" 옮기고 바꾸는 파트만 새로 인코딩한다.
// 원본 사전을 그대로 두므로 옮긴 파트들은 손대지 않아도 그대로 풀린다.
//
// index >= 0 이면 그 파트를 갈아 끼우고, index < 0 이면 끝에 새 파트로 붙인다(파트 수 +1).
// 만들어진 바이트 수를 돌려준다(실패 0). 필요한 버퍼는 Ls12_RewriteCap 으로 잡는다.
//
// 사전이 순열이 아니면(바이트 하나가 어느 코드에도 없으면) 인코딩할 수 없어 0 을 돌려준다.
// MALE.CDS / FEMALE.CDS 는 둘 다 중복 없는 순열이라 문제없다.
unsigned Ls12_RewriteCap(const Ls12File* f, unsigned rawlen);
unsigned Ls12_Rewrite(const Ls12File* f, int index, const unsigned char* raw, unsigned rawlen,
                      unsigned char* out, unsigned outcap);

// 새로 만든 버퍼에서 그 파트를 도로 풀어 원본과 같은지 본다. 같으면 1.
// 파일을 덮어쓰기 전에 반드시 통과시킨다 — 인코딩이 어긋나면 얼굴 파일이 통째로 깨진다.
int Ls12_VerifyPart(const unsigned char* buf, unsigned buflen, int index,
                    const unsigned char* raw, unsigned rawlen);
