#include "fleet.h"

// 세이브를 아직 안 불러왔거나 자리가 이 판과 안 맞으면 NULL.
// .data 뒷부분이라 실행 중에만 커밋돼 있다(inventory.c 와 같은 사정).
static int* FatiguePtr(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* base = (unsigned char*)GetModuleHandleW(NULL);
    void* p;
    if (!base) return NULL;
    p = base + FLEET_FATIGUE_RVA;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return NULL;
    if (mbi.State != MEM_COMMIT) return NULL;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return NULL;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return NULL;
    return (int*)p;
}

int Fleet_Fatigue(void)
{
    int* p = FatiguePtr();
    int v;
    if (!p) return -1;
    v = *p;
    // 게임이 쓰는 범위를 크게 벗어나면 아직 함대 정보가 안 찬 것으로 본다.
    if (v < 0 || v > 1000) return -1;
    return v;
}

int Fleet_SetFatigue(int v)
{
    int* p = FatiguePtr();
    if (!p) return 0;
    if (v < 0) v = 0;
    *p = v;
    return 1;
}

// n 바이트를 그 자리에서 읽어도 되는가. 한 구역 안에 다 들어와야 한다.
static int Readable(const void* p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if ((const unsigned char*)p + n >
        (const unsigned char*)mbi.BaseAddress + mbi.RegionSize) return 0;
    return 1;
}

int Fleet_Crew(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    int i, sum = 0, ships = 0;
    if (!base) return -1;
    for (i = 0; i < FLEET_SHIPS; i++) {
        const int* slot = (const int*)(base + FLEET_LIST_RVA + (unsigned)i * 4);
        const unsigned char* s;
        int id, crew;
        if (!Readable(slot, 4)) return -1;          // 세이브 전이면 자리 자체가 없다
        id = *slot;
        if (id < 0 || id >= FLEET_SHIP_N) continue; // -1 이면 빈 칸
        s = base + FLEET_SHIP_RVA + (unsigned)id * FLEET_SHIP_SZ;
        if (!Readable(s, FLEET_SHIP_SZ)) continue;
        crew = *(const int*)(s + FLEET_SHIP_CREW);
        if (crew < 0 || crew > 99999) continue;
        sum += crew; ships++;
    }
    return ships ? sum : -1;                        // 한 척도 못 읽었으면 아직 아니다
}
