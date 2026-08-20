#include "discinst.h"
#include "hintdb.h"      // HINT_N, HintDb_Live

// 자리와 까닭은 discinst.h 에 적어 뒀다. 여기서는 읽기만 한다.

#define DINST_PER_HINT 4          // 한 힌트에 걸리는 인스턴스는 많아야 셋이었다(피라미드·스핑크스 …)

static unsigned char* g_base = NULL;
static int g_ready = 0;
static short g_ofDisc[DINST_DTAB_N];              // 발견물 -> 인스턴스(사실상 자기 번호)
static short g_ofHint[HINT_N][DINST_PER_HINT];    // 힌트 -> 인스턴스들
static unsigned char g_nOfHint[HINT_N];
static wchar_t g_who[DINST_NAME_SZ + 1];

static int Commit(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

int DInst_Load(void)
{
    const unsigned char *dtab, *htab;
    int i, h;

    if (g_ready) return 1;
    g_base = (unsigned char*)GetModuleHandleW(NULL);
    if (!g_base) return 0;

    dtab = g_base + DINST_DTAB_RVA;
    htab = g_base + DINST_HTAB_RVA;
    if (!Commit(dtab, DINST_DTAB_N * DINST_DTAB_SZ)) return 0;
    if (!Commit(htab, HINT_N * DINST_HTAB_SZ)) return 0;

    for (i = 0; i < DINST_DTAB_N; i++)
        g_ofDisc[i] = (short)(i < DINST_N ? i : -1);

    for (h = 0; h < HINT_N; h++) {
        int code = *(const int*)(htab + h * DINST_HTAB_SZ);
        g_nOfHint[h] = 0;
        for (i = 0; i < DINST_N; i++) {
            if (*(const int*)(dtab + i * DINST_DTAB_SZ + 0x08) != code) continue;
            if (g_nOfHint[h] >= DINST_PER_HINT) break;
            g_ofHint[h][g_nOfHint[h]++] = (short)i;
        }
    }
    g_ready = 1;
    return 1;
}

int DInst_Ready(void) { return g_ready; }

static const unsigned char* Rec(int inst)
{
    const unsigned char* p;
    if (!g_base || inst < 0 || inst >= DINST_N) return NULL;
    p = g_base + DINST_RVA + (unsigned)inst * DINST_SZ;
    return Commit(p, DINST_SZ) ? p : NULL;
}

static const unsigned char* Slot(int inst, int slot)
{
    const unsigned char* r = Rec(inst);
    if (!r || slot < 0 || slot > 2) return NULL;
    return r + DINST_SLOT_OFF + slot * DINST_SLOT_SZ;
}

// 인스턴스 배열도 힌트 배열과 한 덩어리다 — 세이브를 불러와야 생긴다.
int DInst_Live(void)
{
    return Rec(0) != NULL && HintDb_Live();
}

int DInst_OfDisc(int disc)
{
    return (g_ready && disc >= 0 && disc < DINST_DTAB_N) ? g_ofDisc[disc] : -1;
}

int DInst_CountOfHint(int hint)
{
    return (g_ready && hint >= 0 && hint < HINT_N) ? (int)g_nOfHint[hint] : 0;
}

int DInst_OfHint(int hint, int k)
{
    if (!g_ready || hint < 0 || hint >= HINT_N) return -1;
    return (k >= 0 && k < (int)g_nOfHint[hint]) ? g_ofHint[hint][k] : -1;
}

int DInst_Filled(int inst, int slot)
{
    const unsigned char* s = Slot(inst, slot);
    return s ? (s[0] != 0) : 0;
}

const wchar_t* DInst_Who(int inst, int slot)
{
    const unsigned char* s = Slot(inst, slot);
    g_who[0] = 0;
    if (!s || !s[0]) return L"";
    MultiByteToWideChar(949, 0, (const char*)s, -1, g_who, DINST_NAME_SZ + 1);
    return g_who;
}

int DInst_Year(int inst, int slot)
{
    const unsigned char* s = Slot(inst, slot);
    return (s && s[0]) ? *(const int*)(s + 0x28) : -1;
}

int DInst_Month(int inst, int slot)
{
    const unsigned char* s = Slot(inst, slot);
    return (s && s[0]) ? *(const int*)(s + 0x2C) : -1;
}

// 걸린 인스턴스를 모두 훑어 하나라도 해당하면 1. 인스턴스가 아예 없으면 -1(모른다).
static int AnySlot(int hint, int slot)
{
    int k, n = DInst_CountOfHint(hint);
    if (n <= 0 || !DInst_Live()) return -1;
    for (k = 0; k < n; k++)
        if (DInst_Filled(g_ofHint[hint][k], slot)) return 1;
    return 0;
}

int DInst_HintFound(int hint)    { return AnySlot(hint, DINST_ME); }
int DInst_HintReported(int hint) { return AnySlot(hint, DINST_REPORT); }
int DInst_HintTaken(int hint)    { return AnySlot(hint, DINST_OTHER); }
