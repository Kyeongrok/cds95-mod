#include "hulldb.h"
#include "cities_data.h"   // TradeUtilKR/src — kCities[226]

#define CITY_N ((int)(sizeof(kCities)/sizeof(kCities[0])))

static BYTE* g_base = NULL;
static BYTE* g_tbl  = NULL;
static int   g_status = HULLDB_E_MODULE;

static int Readable(const void* p, SIZE_T n)
{
    const BYTE* q = (const BYTE*)p;
    const BYTE* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static const wchar_t* Cp949(const char* s)
{
    static wchar_t ring[4][64];
    static int k = 0;
    wchar_t* out = ring[k = (k + 1) % 4];
    out[0] = 0;
    if (!s || !Readable(s, 1)) return out;
    MultiByteToWideChar(949, 0, s, -1, out, 64);
    out[63] = 0;
    return out;
}

static BYTE* Row(int k)
{
    if (!g_tbl || k < 0 || k >= HULL_N) return NULL;
    return g_tbl + (unsigned)k * HULL_SIZE;
}

static int Field(int k, int off)
{
    BYTE* r = Row(k);
    return r ? *(const int*)(r + off) : 0;
}

static int RowOk(const BYTE* r)
{
    const char* nm = *(const char* const*)(r + HL_NAME);
    int d = *(const int*)(r + HL_DURA), s = *(const int*)(r + HL_SPEED);
    int v = *(const int*)(r + HL_VOLUME), w = *(const int*)(r + HL_WEIGHT);
    int c = *(const int*)(r + HL_CREW), g = *(const int*)(r + HL_GUNS);
    if ((UINT_PTR)nm < 0x400000 || (UINT_PTR)nm > 0x700000) return 0;
    if (d < 1 || d > 500) return 0;
    if (s < 1 || s > 500) return 0;
    if (v < 1 || v > 20000) return 0;
    if (w < 1 || w > 200000) return 0;
    if (c < 0 || c > 2000) return 0;
    if (g < 0 || g > 500) return 0;
    return 1;
}

int Hull_Ready(void)  { return g_tbl != NULL; }
int Hull_Status(void) { return g_status; }

int Hull_Load(void)
{
    BYTE* tbl;
    int i;

    if (g_tbl) return 1;
    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) { g_status = HULLDB_E_MODULE; return 0; }

    tbl = g_base + HULL_RVA;
    if (!Readable(tbl, (SIZE_T)(HULL_N + 1) * HULL_SIZE)) { g_status = HULLDB_E_READ; return 0; }
    for (i = 0; i < HULL_N; i++)
        if (!RowOk(tbl + (unsigned)i * HULL_SIZE)) { g_status = HULLDB_E_ROWS; return 0; }
    // 9행째까지 말이 되면 표가 밀려 잡힌 것이다(원본에서 그 자리는 쓰레기다).
    if (RowOk(tbl + (unsigned)HULL_N * HULL_SIZE)) { g_status = HULLDB_E_TAIL; return 0; }

    g_tbl = tbl;
    g_status = HULLDB_OK;
    OutputDebugStringW(L"[ShipInfoKR] 선체표 8종 로드.");
    return 1;
}

const wchar_t* Hull_Name(int k)
{
    BYTE* r = Row(k);
    return r ? Cp949(*(const char* const*)(r + HL_NAME)) : L"";
}
int Hull_Dura(int k)   { return Field(k, HL_DURA); }
int Hull_Speed(int k)  { return Field(k, HL_SPEED); }
int Hull_Volume(int k) { return Field(k, HL_VOLUME); }
int Hull_Weight(int k) { return Field(k, HL_WEIGHT); }
int Hull_Guns(int k)   { return Field(k, HL_GUNS); }
int Hull_Crew(int k)   { return Field(k, HL_CREW) + HULL_CREW_ADD; }

// 도시 배열은 .data 의 0채움 대역이라 세이브를 불러와야 값이 선다.
int Hull_CitiesReady(void)
{
    const BYTE* p;
    if (!g_base) return 0;
    p = g_base + CITY_ARR_RVA;
    if (!Readable(p, (SIZE_T)CITY_N * CITY_ARR_SZ)) return 0;
    {   // 전부 0 이면 아직 안 채워진 것이다 — 파는 선체가 하나도 없을 수는 없다.
        int i;
        for (i = 0; i < CITY_N; i++)
            if (*(const unsigned short*)(p + (unsigned)i * CITY_ARR_SZ + CITY_SHIP_OFF)) return 1;
    }
    return 0;
}

int Hull_CitySells(int city, int k)
{
    const BYTE* p;
    unsigned short m;
    if (!g_base || city < 0 || city >= CITY_N || k < 0 || k >= HULL_N) return -1;
    if (!Hull_CitiesReady()) return -1;
    p = g_base + CITY_ARR_RVA + (unsigned)city * CITY_ARR_SZ + CITY_SHIP_OFF;
    m = *(const unsigned short*)p;
    return (m >> k) & 1;
}
