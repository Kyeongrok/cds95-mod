#pragma once
#include <windows.h>

// 함대 정보 칸 하나를 읽고 쓰는 자리. 피로도 창과 규율 창이 나눠 쓴다.
//
// 자리는 ce/CDS_95.CT 의 "함대 정보" 묶음이다 — 피로도 0x5B3950, 규칙(규율) 0x5B3954,
// 물 0x5B395C, 식량 0x5B3960 … 으로 4바이트씩 이어진다. 절대주소가 아니라 모듈 베이스 +
// RVA 로 잡는다(다른 KR 플러그인과 같은 방식 — livechar.c 의 FAME_RVA 참고).
//
// ★ 세이브를 아직 안 불러왔거나 주소가 이 빌드와 안 맞으면 NULL 이다. .data 뒷부분이라
//   실행 중에만 커밋돼 있어 읽기 전에 반드시 확인한다.

#define FLEET_FATIGUE_RVA     0x1B3950u   // 피로도
#define FLEET_DISCIPLINE_RVA  0x1B3954u   // 규율(게임 원문 "규칙")

static int* FleetPtr(unsigned rva)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* base = (unsigned char*)GetModuleHandleW(NULL);
    void* p;
    if (!base) return NULL;
    p = base + rva;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return NULL;
    if (mbi.State != MEM_COMMIT) return NULL;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return NULL;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return NULL;
    return (int*)p;
}

// 지금 값. 못 읽거나 게임이 쓰는 범위를 크게 벗어나면(=아직 함대 정보가 안 찼으면) -1.
static int FleetGet(unsigned rva)
{
    int* p = FleetPtr(rva);
    int v;
    if (!p) return -1;
    v = *p;
    if (v < 0 || v > 1000) return -1;
    return v;
}

static int FleetSet(unsigned rva, int v)
{
    int* p = FleetPtr(rva);
    if (!p) return 0;
    *p = v;
    return 1;
}
