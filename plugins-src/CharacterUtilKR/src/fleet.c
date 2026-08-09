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
