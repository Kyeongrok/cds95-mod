#include "patrons.h"

#define OFF_FACE   0x00
#define OFF_GENDER 0x04
#define OFF_YEAR   0x10
#define YEAR_BASE  1480

#define FACE_MAX   511
#define YEAR_MIN_V (-40)
#define YEAR_MAX_V 80

static unsigned char* g_tbl = NULL;

int Patron_Ready(void) { return g_tbl != NULL; }

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

static int RowOk(const unsigned char* r)
{
    int face   = *(const int*)(r + OFF_FACE);
    int gender = *(const int*)(r + OFF_GENDER);
    int year   = *(const int*)(r + OFF_YEAR);
    if (face < 0 || face > FACE_MAX) return 0;
    if (gender != 0 && gender != 1) return 0;
    if (year < YEAR_MIN_V || year > YEAR_MAX_V) return 0;
    return 1;
}

int Patron_Load(void)
{
    const unsigned char* base;
    unsigned char* tbl;
    int i;

    if (g_tbl) return 1;
    base = (const unsigned char*)GetModuleHandleW(NULL);
    if (!base) return 0;
    tbl = (unsigned char*)(base + PATRON_RVA);
    if (!Readable(tbl, (SIZE_T)(PATRON_COUNT + 1) * PATRON_SIZE)) return 0;

    for (i = 0; i < PATRON_COUNT; i++)
        if (!RowOk(tbl + i * PATRON_SIZE)) return 0;
    // 82행째까지 말이 되면 표가 밀려 잡힌 것이다. 길이가 딱 81이어야 한다.
    if (RowOk(tbl + PATRON_COUNT * PATRON_SIZE)) return 0;

    g_tbl = tbl;
    OutputDebugStringW(L"[CharacterUtilKR] 후원자 표 81행 로드.");
    return 1;
}

int Patron_Find(int gender, int face)
{
    int i;
    if (!g_tbl || face < 0) return -1;
    for (i = 0; i < PATRON_COUNT; i++) {
        const unsigned char* r = g_tbl + i * PATRON_SIZE;
        if (*(const int*)(r + OFF_FACE) == face &&
            *(const int*)(r + OFF_GENDER) == gender) return i;
    }
    return -1;
}

int Patron_Year(int row)
{
    if (!g_tbl || row < 0 || row >= PATRON_COUNT) return 0;
    return YEAR_BASE + *(const int*)(g_tbl + row * PATRON_SIZE + OFF_YEAR);
}

int Patron_SetYear(int row, int year)
{
    unsigned char* p;
    DWORD old = 0;

    if (!g_tbl || row < 0 || row >= PATRON_COUNT) return 0;
    if (year < PATRON_YEAR_MIN || year > PATRON_YEAR_MAX) return 0;

    // .rdata 라 읽기 전용이다. 잠깐만 열고 되돌린다. 파일이 아니라 메모리를 고치는 것이라
    // 게임을 끄면 원래대로 돌아간다.
    p = g_tbl + row * PATRON_SIZE + OFF_YEAR;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *(int*)p = year - YEAR_BASE;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}

// ---- 실행 중에만 있는 값(친밀도 · 자금) ----
// EXE 표(.rdata)가 아니라 .data 뒷부분이라 세이브를 불러와야 생긴다. 읽기 전에 확인한다.
static const int* LiveField(int row, int off)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    const unsigned char* p;
    if (!base || row < 0 || row >= PATRON_COUNT) return NULL;
    p = base + PATRON_LIVE_RVA + (unsigned)row * PATRON_LIVE_SZ + off;
    return Readable(p, sizeof(int)) ? (const int*)p : NULL;
}

int Patron_Intimacy(int row)
{
    const int* p = LiveField(row, 0);
    if (!p) return -1;
    // 세이브 전에는 커밋만 돼 있고 값이 안 채워진 자리라 말이 안 되는 수가 나온다.
    return (*p >= 0 && *p <= 1000) ? *p : -1;
}

int Patron_Money(int row)
{
    const int* p = LiveField(row, 4);
    if (!p) return -1;
    return (*p >= 0) ? *p : -1;
}
