#include <math.h>
#include "bookdb.h"
#include "hintdb.h"    // HintUtilKR/src — 힌트 이름·분류·상태
#include "skilldb.h"   // SkillUtilKR/src — 건물표(도서관 유무) · 언어 이름표
#include "city_coords.h"  // WorldMapKR/src — kCityLonRaw / kCityLatRaw

static BYTE* g_base = NULL;
static BYTE* g_tbl  = NULL;   // 정적표
static BYTE* g_live = NULL;   // 런타임 배열
static int   g_status = BKDB_E_MODULE;

typedef int (__cdecl *ColorFn)(int, int);
typedef int (__cdecl *LangLvFn)(void*);

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

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
    static wchar_t ring[8][160];
    static int k = 0;
    wchar_t* out = ring[k = (k + 1) % 8];
    out[0] = 0;
    if (!s || !Readable(s, 1)) return out;
    MultiByteToWideChar(949, 0, s, -1, out, 160);
    out[159] = 0;
    return out;
}

static BYTE* Row(int k)
{
    if (!g_tbl || k < 0 || k >= BOOK_N) return NULL;
    return g_tbl + (unsigned)k * BOOK_SIZE;
}

static int RowOk(const BYTE* r)
{
    int lang = *(const int*)(r + BK_LANG);
    int year = *(const int*)(r + BK_YEAR);
    int i;
    if (lang < -1 || lang > 13) return 0;
    if (year < 0 || year > 200) return 0;
    for (i = 0; i < BOOK_SLOTS; i++) {
        int c = *(const int*)(r + BK_CITY0 + i * 4);
        int h = *(const int*)(r + BK_HINT0 + i * 4);
        if (c < -1 || c >= CITY_COORD_N) return 0;
        if (h < -1 || h >= HINT_N) return 0;
    }
    return 1;
}

int Book_Ready(void)  { return g_tbl != NULL; }
int Book_Status(void) { return g_status; }
int Book_Count(void)  { return BOOK_N; }

int Book_Load(void)
{
    BYTE* tbl;
    int i;

    if (g_tbl) return 1;
    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) { g_status = BKDB_E_MODULE; return 0; }

    tbl = g_base + BOOK_RVA;
    if (!Readable(tbl, (SIZE_T)(BOOK_N + 1) * BOOK_SIZE)) { g_status = BKDB_E_READ; return 0; }

    for (i = 0; i < BOOK_N; i++)
        if (!RowOk(tbl + (unsigned)i * BOOK_SIZE)) {
            LogW(L"[BookUtilKR] 서적표 %d행이 말이 안 된다 — 다른 빌드로 보인다.", i);
            g_status = BKDB_E_ROWS;
            return 0;
        }
    // 258행째까지 말이 되면 표가 밀려 잡힌 것이다. 원본에서 그 자리는 쓰레기다.
    if (RowOk(tbl + (unsigned)BOOK_N * BOOK_SIZE)) { g_status = BKDB_E_TAIL; return 0; }

    g_tbl  = tbl;
    g_live = g_base + BOOK_LIVE_RVA;
    g_status = BKDB_OK;
    LogW(L"[BookUtilKR] 서적표 %d권 로드 (VA 0x%08X).", BOOK_N, (unsigned)(UINT_PTR)tbl);
    return 1;
}

const wchar_t* Book_Title(int k)
{
    BYTE* r = Row(k);
    return r ? Cp949(*(const char* const*)(r + BK_TITLE)) : L"";
}
const wchar_t* Book_Author(int k)
{
    BYTE* r = Row(k);
    return r ? Cp949(*(const char* const*)(r + BK_AUTHOR)) : L"";
}
int Book_Lang(int k) { BYTE* r = Row(k); return r ? *(const int*)(r + BK_LANG) : -1; }
int Book_Year(int k) { BYTE* r = Row(k); return r ? BOOK_YEAR_BASE + *(const int*)(r + BK_YEAR) : 0; }

int Book_City(int k, int i)
{
    BYTE* r = Row(k);
    if (!r || i < 0 || i >= BOOK_SLOTS) return -1;
    return *(const int*)(r + BK_CITY0 + i * 4);
}
int Book_Hint(int k, int i)
{
    BYTE* r = Row(k);
    if (!r || i < 0 || i >= BOOK_SLOTS) return -1;
    return *(const int*)(r + BK_HINT0 + i * 4);
}
int Book_CityCount(int k)
{
    int i, n = 0;
    for (i = 0; i < BOOK_SLOTS; i++) if (Book_City(k, i) != -1) n++;
    return n;
}
int Book_HintCount(int k)
{
    int i, n = 0;
    for (i = 0; i < BOOK_SLOTS; i++) if (Book_Hint(k, i) != -1) n++;
    return n;
}

// 런타임 배열과 힌트 배열은 .data 의 0채움 대역이라 세이브를 불러와야 선다.
int Book_Live(void)
{
    return Book_Ready() && HintDb_Live() && Readable(g_live, (SIZE_T)BOOK_N * BOOK_LIVE_SZ);
}

int Book_Read(int k)
{
    const int* p;
    if (!Book_Live() || k < 0 || k >= BOOK_N) return -1;
    p = (const int*)(g_live + (unsigned)k * BOOK_LIVE_SZ + 0x40);
    return *p;
}

// 아직 못 얻은 힌트 수. 힌트 상태의 bit0(얻음)·bit1(발견)이 하나라도 서면 셈에서 뺀다.
int Book_NewHints(int k)
{
    int i, n = 0;
    if (!Book_Live()) return -1;
    for (i = 0; i < BOOK_SLOTS; i++) {
        int h = Book_Hint(k, i), st;
        if (h == -1) continue;
        st = HintDb_State(h);
        if (st < 0) continue;
        if (st & 3) continue;          // 0x4716A0 과 같은 잣대
        n++;
    }
    return n;
}

// 게임 함수를 그대로 부른다. 우리 창은 게임 메시지 루프에서 도니 같은 스레드다.
// 그래도 혹시 모르니 감싸 둔다 — 터지면 "모름" 으로 돌려준다.
int Book_Color(int k)
{
    ColorFn fn;
    int r;
    if (!Book_Live() || k < 0 || k >= BOOK_N) return BOOK_C_NONE;
    fn = (ColorFn)(g_base + FN_COLOR_RVA);
    __try { r = fn(k, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return BOOK_C_NONE; }
    return (r >= 0 && r <= 2) ? r : BOOK_C_NONE;
}

int Book_LangLevel(int k)
{
    LangLvFn fn;
    BYTE* rec;
    int r;
    if (!Book_Live() || k < 0 || k >= BOOK_N) return -1;
    rec = g_live + (unsigned)k * BOOK_LIVE_SZ;   // 게임도 런타임 레코드를 넘긴다
    fn = (LangLvFn)(g_base + FN_LANGLV_RVA);
    __try { r = fn(rec); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    return r;
}

int Book_CurrentCity(void)
{
    const int* p;
    if (!g_base) return -1;
    p = (const int*)(g_base + CUR_CITY_RVA);
    if (!Readable(p, sizeof(int))) return -1;
    return (*p >= 0 && *p < CITY_COORD_N) ? *p : -1;
}

int Book_CityHasLibrary(int city)
{
    int k;
    if (!SkillDb_Ready()) return -1;
    for (k = 0; k < SkillDb_Count(); k++)
        if (SkillDb_City(k) == city && SkillDb_Code(k) == 8) return 1;
    return 0;
}

int Book_CityDistance(int city)
{
    const int *lon, *lat;
    int dx, dy;
    if (!g_base || city < 0 || city >= CITY_COORD_N) return -1;
    lon = (const int*)(g_base + FLEET_LON_RVA);
    lat = (const int*)(g_base + FLEET_LAT_RVA);
    if (!Readable(lon, sizeof(int)) || !Readable(lat, sizeof(int))) return -1;
    if (*lon <= 0 && *lat <= 0) return -1;          // 세이브 전
    // 원본값(경도 0~40000 / 위도 0~20000)을 칸(값/16)으로 낮춰서 잰다. 그래야 안 넘친다.
    dx = (*lon - kCityLonRaw[city]) / 16;
    dy = (*lat - kCityLatRaw[city]) / 16;
    return (int)(0.5 + sqrt((double)dx * dx + (double)dy * dy));
}
