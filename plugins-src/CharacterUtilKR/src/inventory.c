#include "inventory.h"

static unsigned char* g_base = NULL;
static int g_ready = 0;
static int g_status = INV_E_READ;

int Inv_Ready(void)  { return g_ready; }
int Inv_Status(void) { return g_status; }
int Inv_Count(int kind) { return kind == INV_HELD ? INV_HELD_N : INV_STORE_N; }

static int Readable(const void* p, SIZE_T n)
{
    const unsigned char* q = (const unsigned char*)p;
    const unsigned char* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static int* SlotPtr(int kind, int i)
{
    if (!g_ready || i < 0 || i >= Inv_Count(kind)) return NULL;
    return (int*)(g_base + (kind == INV_HELD ? INV_HELD_RVA : INV_STORE_RVA)) + i;
}

// 아이템 번호이거나 빈 칸이어야 한다. 그 밖의 값이 하나라도 있으면 자리가 어긋난 것이다.
static int Sane(int v) { return v == INV_EMPTY || (v >= 0 && v < INV_ITEM_N); }

int Inv_Load(void)
{
    const unsigned char* base;
    const int* held;
    const int* store;
    int i, money, nonzero = 0;

    g_ready = 0;
    g_base = NULL;
    base = (const unsigned char*)GetModuleHandleW(NULL);
    if (!base) { g_status = INV_E_MODULE; return 0; }

    // 소지금부터 보관 아이템 끝까지 한 덩이로 확인한다.
    if (!Readable(base + INV_MONEY_RVA, (INV_STORE_RVA - INV_MONEY_RVA) + INV_STORE_N * 4)) {
        g_status = INV_E_READ; return 0;
    }
    money = *(const int*)(base + INV_MONEY_RVA);
    if (money < 0 || money > INV_MONEY_MAX) { g_status = INV_E_MONEY; return 0; }

    held  = (const int*)(base + INV_HELD_RVA);
    store = (const int*)(base + INV_STORE_RVA);
    for (i = 0; i < INV_HELD_N; i++)  { if (!Sane(held[i]))  { g_status = INV_E_RANGE; return 0; } if (held[i])  nonzero = 1; }
    for (i = 0; i < INV_STORE_N; i++) { if (!Sane(store[i])) { g_status = INV_E_RANGE; return 0; } if (store[i]) nonzero = 1; }

    // 세이브를 불러오기 전에는 이 구간이 통째로 0 이다. 0 은 아이템 0번(잠수폭탄)이라
    // 범위 검사만으로는 안 걸러진다 — 빈 칸이 -1 로 채워졌는지로 가린다.
    if (!nonzero) { g_status = INV_E_EMPTY; return 0; }

    g_base = (unsigned char*)base;
    g_ready = 1;
    g_status = INV_OK;
    return 1;
}

int Inv_Get(int kind, int i)
{
    const int* p = SlotPtr(kind, i);
    if (!p) return -1;
    return Sane(*p) ? *p : -1;
}

int Inv_Set(int kind, int i, int item)
{
    int* p = SlotPtr(kind, i);
    if (!p) return 0;
    if (item < 0 || item >= INV_ITEM_N) item = INV_EMPTY;
    *p = item;
    return 1;
}

int Inv_Used(int kind)
{
    int i, n = 0, c = Inv_Count(kind);
    for (i = 0; i < c; i++) if (Inv_Get(kind, i) >= 0) n++;
    return n;
}

int Inv_FirstEmpty(int kind)
{
    int i, c = Inv_Count(kind);
    for (i = 0; i < c; i++) if (Inv_Get(kind, i) < 0) return i;
    return -1;
}

int Inv_Money(void)
{
    if (!g_ready) return -1;
    return *(const int*)(g_base + INV_MONEY_RVA);
}

int Inv_SetMoney(int v)
{
    if (!g_ready) return 0;
    if (v < 0) v = 0;
    if (v > INV_MONEY_MAX) v = INV_MONEY_MAX;
    *(int*)(g_base + INV_MONEY_RVA) = v;
    return 1;
}
