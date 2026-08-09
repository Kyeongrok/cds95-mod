#pragma once
#include <windows.h>

// cds_95.exe 안에 박혀 있는 아이템 표 두 개 — 그림 번호·분류·값이 든 레코드표와,
// 설명문 포인터표. 둘 다 읽기만 한다.
//
// 자리는 파일 오프셋으로 적어 둔다(헥스 편집기로 다시 확인하기 쉽다). 이 exe 는
// 파일정렬 0x200 · 섹션정렬 0x1000 이라 오프셋과 RVA 가 다르므로(.rdata +0x1A00,
// .data +0x2800) 섹션표를 훑어 변환해 쓴다. 참고로 변환 결과는 RVA 0x0FD558 / 0x158B80.
//
//   레코드표  파일오프셋 0x0FBB58, 28바이트 x 286개 (색인 = 아이템 번호)
//        +0x00 char*  이름(cp949)      +0x04 int 그림번호(-1 = 없음, 0~205)
//        +0x08 int 값A                 +0x0C int 값B
//        +0x10 int                     +0x14 int 분류(0~8)        +0x18 int
//   설명문표  파일오프셋 0x156380, char* x 286개 (cp949)
//
// 그림번호는 아이템 번호와 1:1 이 아니다 — 99개는 그림이 없고, 한 그림을 여럿이
// 나눠 쓰기도 한다(53번은 아이템 9개). 그림은 반드시 rec->pic 으로만 찾는다.

#define ITEMDB_N        286
#define ITEMDB_REC_OFF  0x0FBB58u
#define ITEMDB_DESC_OFF 0x156380u
#define ITEMDB_REC_SZ   28
#define ITEMDB_PIC_MAX  205
#define ITEMDB_CAT_N    9

typedef struct {
    int pic;        // 그림 번호. 없으면 -1
    int valA, valB;
    int arg;
    int cat;        // 0~8
    int misc;
} ItemRec;

#define ITEMDB_OK       0
#define ITEMDB_E_MODULE 1   // 모듈 핸들 / PE 머리를 못 읽음
#define ITEMDB_E_READ   2   // 표 자리가 커밋 안 됨
#define ITEMDB_E_TABLE  3   // 내용이 아이템 표로 안 보임(다른 판인 듯)

int ItemDb_Load(void);      // 성공 1. 여러 번 불러도 된다(성공했으면 바로 돌아온다)
int ItemDb_Ready(void);
int ItemDb_Status(void);
void ItemDb_Reset(void);    // 실패를 기억해 두지 않고 다음에 다시 해 보게 한다

const ItemRec* ItemDb_At(int id);        // 범위 밖 / 미로드면 NULL
const wchar_t* ItemDb_CatName(int cat);  // 0~8, 밖이면 L"?"

// 설명문. 없거나 미로드면 NULL.
// 돌려준 포인터는 다음 ItemDb_Desc 호출 전까지만 쓸 수 있다(버퍼 하나를 돌려 쓴다).
const wchar_t* ItemDb_Desc(int id);
