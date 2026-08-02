#include "ship_stats.h"

// 칸별 레코드 오프셋(현재, 최대). 최대가 없는 칸은 -1.
static const struct { int cur, max, step; const wchar_t* label; } kField[SHIPSTAT_FIELD_N] = {
    { 0x0C, 0x10,  1, L"추진" },
    { 0x14, 0x18,  1, L"내구" },
    { 0x1C, 0x20, 50, L"중량" },
    { 0x24, 0x28,  5, L"용량" },
    { 0x2C, 0x30,  1, L"포탑" },
    { 0x34,   -1,  5, L"선원" },   // 실제값 = 50 + 코드 x 5 → 화면 단위 5
    { 0x38,   -1, 1000, L"자금" }, // 실제값 = 코드 x 1000 → 화면 단위 1000
    { 0x08,   -1,  1, L"등장" },   // 조선소 발전도 1 기준 연도 = 1470 + 코드
};

static unsigned char* g_tbl = NULL;
static int  g_snap[SHIPSTAT_N][SHIPSTAT_SIZE / 4];
static wchar_t g_name[SHIPSTAT_N][32];

int ShipStat_Ready(void) { return g_tbl != NULL; }
const wchar_t* ShipStat_FieldName(int f) { return (f >= 0 && f < SHIPSTAT_FIELD_N) ? kField[f].label : L""; }
int ShipStat_HasMax(int f) { return (f >= 0 && f < SHIPSTAT_FIELD_N) && kField[f].max >= 0; }
int ShipStat_Step(int f)   { return (f >= 0 && f < SHIPSTAT_FIELD_N) ? kField[f].step : 1; }

const wchar_t* ShipStat_Name(int ship)
{
    return (g_tbl && ship >= 0 && ship < SHIPSTAT_N) ? g_name[ship] : L"";
}

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

// cp949 이름을 포인터에서 꺼낸다. 모듈 안을 가리키지 않으면 실패.
static int ReadName(const unsigned char* base, SIZE_T imgSize, unsigned ptr, wchar_t* out, int cap)
{
    const char* s = (const char*)(UINT_PTR)ptr;
    char buf[32];
    int n = 0, w;
    out[0] = 0;
    if (ptr < (unsigned)(UINT_PTR)base || ptr >= (unsigned)(UINT_PTR)base + (unsigned)imgSize) return 0;
    while (n < (int)sizeof(buf) - 1 && s[n]) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (!n) return 0;
    w = MultiByteToWideChar(949, 0, buf, n, out, cap - 1);
    if (w <= 0) return 0;
    out[w] = 0;
    return 1;
}

int ShipStat_Load(void)
{
    const IMAGE_DOS_HEADER* dos;
    const IMAGE_NT_HEADERS32* nt;
    const unsigned char* base;
    SIZE_T imgSize;
    unsigned char* tbl;
    int i, k;

    if (g_tbl) return 1;
    dos = (const IMAGE_DOS_HEADER*)GetModuleHandleW(NULL);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (const IMAGE_NT_HEADERS32*)((const unsigned char*)dos + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    base = (const unsigned char*)dos;
    imgSize = nt->OptionalHeader.SizeOfImage;

    tbl = (unsigned char*)base + SHIPSTAT_RVA;
    if (!Readable(tbl, (SIZE_T)SHIPSTAT_N * SHIPSTAT_SIZE)) return 0;

    // 8칸 전부 이름이 읽히고 값이 말이 되어야 표를 믿는다.
    for (i = 0; i < SHIPSTAT_N; i++) {
        const unsigned char* r = tbl + i * SHIPSTAT_SIZE;
        wchar_t nm[32];
        if (!ReadName(base, imgSize, *(const unsigned*)r, nm, 32)) return 0;
        for (k = 0; k < SHIPSTAT_FIELD_N; k++) {
            int v = *(const int*)(r + kField[k].cur);
            if (v < 0 || v > 100000) return 0;
        }
    }

    g_tbl = tbl;
    for (i = 0; i < SHIPSTAT_N; i++) {
        const unsigned char* r = tbl + i * SHIPSTAT_SIZE;
        ReadName(base, imgSize, *(const unsigned*)r, g_name[i], 32);
        for (k = 0; k < SHIPSTAT_SIZE / 4; k++) g_snap[i][k] = *(const int*)(r + k * 4);
    }
    OutputDebugStringW(L"[ShipSkinKR] 함선 성능표 8종 로드.");
    return 1;
}

// 코드 <-> 화면 값
static int ToShow(int field, int raw)
{
    if (field == SF_CREW) return 50 + raw * 5;
    if (field == SF_COST) return raw * 1000;
    if (field == SF_YEAR) return 1470 + raw;
    return raw;
}
static int ToRaw(int field, int show)
{
    if (field == SF_CREW) return (show - 50) / 5;
    if (field == SF_COST) return show / 1000;
    if (field == SF_YEAR) return show - 1470;
    return show;
}

static int* Cell(int ship, int field, int hi)
{
    int off;
    if (!g_tbl || ship < 0 || ship >= SHIPSTAT_N) return NULL;
    if (field < 0 || field >= SHIPSTAT_FIELD_N) return NULL;
    off = (hi && kField[field].max >= 0) ? kField[field].max : kField[field].cur;
    return (int*)(g_tbl + ship * SHIPSTAT_SIZE + off);
}

int ShipStat_Get(int ship, int field, int hi)
{
    const int* p = Cell(ship, field, hi);
    return p ? ToShow(field, *p) : 0;
}

int ShipStat_Set(int ship, int field, int hi, int v)
{
    int* p = Cell(ship, field, hi);
    DWORD old = 0;
    int raw;
    if (!p) return 0;
    raw = ToRaw(field, v);
    if (raw < 0) raw = 0;
    if (raw > 60000) raw = 60000;
    // .rdata 라 읽기 전용이다. 잠깐만 열고 되돌린다. 파일이 아니라 메모리를 고치는 것이라
    // 게임을 끄면 원래대로 돌아간다.
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *p = raw;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}

// ---- 조선소 목록 즉시 갱신 ----
// 0x44B420~0x44B437 의 루프를 그대로 옮긴 것:
//     push i; call 0x429950; add esp,4;  mov ecx,eax;  call 0x42A340
#define YEAR_RVA      0x1A4D20u
#define CITY_GET_RVA  0x29950u    // __cdecl   도시객체* (int idx). 도시배열 0x5863A8, 92바이트 x 226
#define CITY_SHIP_RVA 0x2A340u    // __thiscall void (도시객체*)  — 스택 인자 없음
#define CITY_N        226

typedef void* (__cdecl    *CityGetFn)(int);
typedef void  (__fastcall *CityShipFn)(void* self, void* unused);   // ecx 만 쓰므로 thiscall 대용

int ShipStat_RefreshYards(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    CityGetFn  get;
    CityShipFn calc;
    int year, i, n = 0;

    if (!g_tbl || !base) return 0;
    if (!Readable(base + YEAR_RVA, sizeof(int))) return 0;
    year = *(const int*)(base + YEAR_RVA);
    if (year < 1400 || year > 1700) return 0;   // 아직 세이브를 안 불러왔다

    get  = (CityGetFn)(UINT_PTR)(base + CITY_GET_RVA);
    calc = (CityShipFn)(UINT_PTR)(base + CITY_SHIP_RVA);
    for (i = 0; i < CITY_N; i++) {
        void* c = get(i);
        // 0x42A340 은 첫머리에서 [this+0x1C] 의 0x40 비트(조선소 있음)를 보고 없으면 바로 빠진다.
        if (c) { calc(c, NULL); n++; }
    }
    return n;
}

void ShipStat_Restore(void)
{
    int i, k;
    DWORD old = 0;
    if (!g_tbl) return;
    if (!VirtualProtect(g_tbl, (SIZE_T)SHIPSTAT_N * SHIPSTAT_SIZE, PAGE_READWRITE, &old)) return;
    for (i = 0; i < SHIPSTAT_N; i++)
        for (k = 0; k < SHIPSTAT_SIZE / 4; k++)
            *(int*)(g_tbl + i * SHIPSTAT_SIZE + k * 4) = g_snap[i][k];
    VirtualProtect(g_tbl, (SIZE_T)SHIPSTAT_N * SHIPSTAT_SIZE, old, &old);
}
