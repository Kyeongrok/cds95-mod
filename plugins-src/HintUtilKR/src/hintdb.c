#include "hintdb.h"

// 자리는 hintdb.h 에 적어 뒀다. 여기서는 읽기만 한다.
//
// 이름·분류는 hint_rows.h 의 구운 표에서, 분류 이름은 EXE 의 이름표에서, 상태·가치는
// 실행 중 배열에서 온다. 상태 배열(0x18B4E0)은 .data 뒷부분이라 세이브를 불러와야
// 생긴다 — 그래서 읽을 때마다 커밋을 확인한다(FatigueUtilKR 의 FatiguePtr 과 같은 방식).

static unsigned char* g_base = NULL;
static int g_ready = 0;
static wchar_t g_catName[HINT_CAT_N][16];

static int Commit(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

// exe 안의 cp949 문자열을 와이드로. 못 읽으면 빈 문자열.
static void Cp949(const char* s, wchar_t* out, int cch)
{
    out[0] = 0;
    if (!s || !Commit(s, 1)) return;
    MultiByteToWideChar(949, 0, s, -1, out, cch);
}

int HintDb_Load(void)
{
    const char* const* cats;
    int i;

    if (g_ready) return 1;
    g_base = (unsigned char*)GetModuleHandleW(NULL);
    if (!g_base) return 0;

    cats = (const char* const*)(g_base + HINT_CATNAME_RVA);
    if (!Commit(cats, HINT_CAT_N * sizeof(char*))) return 0;
    for (i = 0; i < HINT_CAT_N; i++) Cp949(cats[i], g_catName[i], 16);
    if (!g_catName[0][0]) return 0;              // 분류 이름이 안 나오면 다른 판이다

    g_ready = 1;
    return 1;
}

int HintDb_Ready(void) { return g_ready; }

static const int* HintPtr(int i)
{
    const unsigned char* p;
    if (!g_base || i < 0 || i >= HINT_N) return NULL;
    p = g_base + HINT_RVA + (unsigned)i * HINT_SZ;
    return Commit(p, HINT_SZ) ? (const int*)p : NULL;
}

int HintDb_Live(void)
{
    const int* p = HintPtr(0);
    if (!p) return 0;
    // 상태 칸이 아는 값(8/13/15)이면 살아 있는 것으로 본다.
    return (p[1] == HINT_NONE || p[1] == HINT_GOT || p[1] == HINT_DONE);
}

const wchar_t* HintDb_Name(int i)
{
    return (i >= 0 && i < HINT_N) ? kHints[i].name : L"?";
}

int HintDb_Cat(int i)
{
    return (i >= 0 && i < HINT_N) ? (int)kHints[i].cat : -1;
}

const wchar_t* HintDb_CatName(int cat)
{
    return (g_ready && cat >= 0 && cat < HINT_CAT_N) ? g_catName[cat] : L"?";
}

int HintDb_State(int i)
{
    const int* p = HintPtr(i);
    return p ? p[1] : -1;
}

int HintDb_Value(int i)
{
    const int* p = HintPtr(i);
    return p ? p[0] : -1;
}
