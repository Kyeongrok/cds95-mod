#include "skilldb.h"
#include "cities_data.h"   // TradeUtilKR/src — kCities[226] 도시 이름

#define SKILL_TBL_RVA 0x00160A10u   // VA 0x560A10 — 기능 이름 13칸
#define LANG_TBL_RVA  0x00160A48u   // VA 0x560A48 — 언어 이름 14칸

#define CITY_N ((int)(sizeof(kCities)/sizeof(kCities[0])))

static HINSTANCE g_self   = NULL;
static BYTE*     g_base   = NULL;
static BYTE*     g_tbl    = NULL;   // 정적 표(.rdata)
static BYTE*     g_live   = NULL;   // 런타임 배열(.data)
static int       g_status = SKDB_E_MODULE;
static unsigned  g_orig[BLD_COUNT];

// EXE 안 이름표를 못 읽을 때 쓰는 예비. 순서는 비트 번호 그대로다.
static const wchar_t* kSkillFallback[SKILL_N] = {
    L"항해술", L"운용술", L"검술", L"포술", L"사격술", L"의학", L"웅변",
    L"측량", L"역사학", L"회계", L"조선기술", L"신학", L"과학"
};
static const wchar_t* kLangFallback[LANG_N] = {
    L"스페인어", L"포르투갈어", L"로망스어", L"게르만어", L"슬라브·그리스어",
    L"아랍어", L"페르시아어", L"중국어", L"힌두어", L"위굴어",
    L"아프리카토착어", L"중남미토착어", L"동남아시아토착어", L"동아시아토착어"
};
static wchar_t g_skillName[SKILL_N][32];
static wchar_t g_langName[LANG_N][32];

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// 그 자리를 읽어도 되는지. .data 뒷부분은 세이브 전에 커밋만 돼 있을 수 있어서
// 읽기 전에 반드시 확인한다(patrons.c 와 같은 방식).
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

// EXE 안 CP949 문자열을 와이드로. 여러 번 이어 쓰므로 버퍼를 여덟 개 돌려 쓴다.
static const wchar_t* Cp949(const char* s)
{
    static wchar_t ring[8][128];
    static int k = 0;
    wchar_t* out = ring[k = (k + 1) % 8];
    out[0] = 0;
    if (!s || !Readable(s, 1)) return out;
    MultiByteToWideChar(949, 0, s, -1, out, 128);
    out[127] = 0;
    return out;
}

static BYTE* Row(int k)
{
    if (!g_tbl || k < 0 || k >= BLD_COUNT) return NULL;
    return g_tbl + (unsigned)k * BLD_SIZE;
}

static int RowOk(const BYTE* r)
{
    int city = *(const int*)(r + BLD_OFF_CITY);
    int code = *(const int*)(r + BLD_OFF_CODE);
    unsigned mask = *(const unsigned*)(r + BLD_OFF_MASK);
    if (city < 0 || city >= CITY_N) return 0;
    if (code < 0 || code > 15) return 0;
    if (mask & ~MASK_ALL) return 0;          // 27비트 넘는 자리는 원본에 하나도 없다
    return 1;
}

// EXE 안 이름표를 읽어 둔다. 한 칸이라도 이상하면 그 칸만 예비 이름을 쓴다.
//
// DllMain 에서 부르지 않는다 — MultiByteToWideChar(949) 는 NLS 를 건드릴 수 있어서
// 로더 락 안에서 부르기 껄끄럽다. 창을 처음 열 때(=이름이 처음 필요할 때) 부른다.
static int g_namesLoaded = 0;
static void LoadNameTables(void)
{
    int i;
    const char** skill;
    const char** lang;
    if (g_namesLoaded || !g_base) return;
    g_namesLoaded = 1;
    skill = (const char**)(g_base + SKILL_TBL_RVA);
    lang  = (const char**)(g_base + LANG_TBL_RVA);
    for (i = 0; i < SKILL_N; i++) {
        const wchar_t* s = L"";
        if (Readable(skill + i, sizeof(char*))) s = Cp949(skill[i]);
        lstrcpynW(g_skillName[i], (s && s[0]) ? s : kSkillFallback[i], 32);
    }
    for (i = 0; i < LANG_N; i++) {
        const wchar_t* s = L"";
        if (Readable(lang + i, sizeof(char*))) s = Cp949(lang[i]);
        lstrcpynW(g_langName[i], (s && s[0]) ? s : kLangFallback[i], 32);
    }
}

int SkillDb_Ready(void)  { return g_tbl != NULL; }
int SkillDb_Status(void) { return g_status; }
int SkillDb_Count(void)  { return BLD_COUNT; }

int SkillDb_Load(HINSTANCE self)
{
    BYTE* tbl;
    int i;

    if (self) g_self = self;
    if (g_tbl) return 1;

    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) { g_status = SKDB_E_MODULE; return 0; }

    tbl = g_base + BLD_RVA;
    if (!Readable(tbl, (SIZE_T)(BLD_COUNT + 1) * BLD_SIZE)) { g_status = SKDB_E_READ; return 0; }

    for (i = 0; i < BLD_COUNT; i++)
        if (!RowOk(tbl + (unsigned)i * BLD_SIZE)) {
            LogW(L"[SkillUtilKR] 건물표 %d행이 말이 안 된다 — 다른 빌드로 보인다.", i);
            g_status = SKDB_E_ROWS;
            return 0;
        }
    // 1509행째까지 말이 되면 표가 밀려 잡힌 것이다. 길이가 딱 1508이어야 한다
    // (원본에서 그 자리는 code=189 라 걸린다). patrons.c 와 같은 방식.
    if (RowOk(tbl + (unsigned)BLD_COUNT * BLD_SIZE)) {
        LogW(L"[SkillUtilKR] 건물표 끝이 안 맞는다 — 표가 밀려 잡혔다.");
        g_status = SKDB_E_TAIL;
        return 0;
    }

    g_tbl  = tbl;
    g_live = g_base + BLD_LIVE_RVA;
    for (i = 0; i < BLD_COUNT; i++)
        g_orig[i] = *(const unsigned*)(tbl + (unsigned)i * BLD_SIZE + BLD_OFF_MASK);

    g_status = SKDB_OK;
    LogW(L"[SkillUtilKR] 건물표 %d행 로드 (VA 0x%08X).", BLD_COUNT, (unsigned)(UINT_PTR)tbl);
    return 1;
}

int SkillDb_City(int k) { BYTE* r = Row(k); return r ? *(const int*)(r + BLD_OFF_CITY) : -1; }
int SkillDb_Code(int k) { BYTE* r = Row(k); return r ? *(const int*)(r + BLD_OFF_CODE) : -1; }

const wchar_t* SkillDb_Name(int k)
{
    BYTE* r = Row(k);
    return r ? Cp949(*(const char* const*)(r + BLD_OFF_NAME)) : L"";
}
const wchar_t* SkillDb_KindName(int k)
{
    BYTE* r = Row(k);
    return r ? Cp949(*(const char* const*)(r + BLD_OFF_KIND)) : L"";
}
const wchar_t* SkillDb_CityName(int k)
{
    int c = SkillDb_City(k);
    return (c >= 0 && c < CITY_N) ? kCities[c].name : L"?";
}

unsigned SkillDb_Mask(int k)
{
    BYTE* r = Row(k);
    return r ? *(const unsigned*)(r + BLD_OFF_MASK) : 0u;
}
unsigned SkillDb_OrigMask(int k)
{
    return (k >= 0 && k < BLD_COUNT) ? g_orig[k] : 0u;
}
int SkillDb_Changed(int k)
{
    return SkillDb_Ready() && k >= 0 && k < BLD_COUNT && SkillDb_Mask(k) != g_orig[k];
}

int SkillDb_GameLoaded(void)
{
    const int* year;
    if (!g_base) return 0;
    year = (const int*)(g_base + GAME_YEAR_RVA);
    if (!Readable(year, sizeof(int))) return 0;
    // 세이브 전에는 0채움 대역이라 0 이다. 게임 연도는 1480 언저리에서만 논다.
    return (*year >= 1400 && *year <= 1700);
}

unsigned SkillDb_LiveMask(int k)
{
    const unsigned* p;
    if (!g_live || k < 0 || k >= BLD_COUNT) return 0u;
    if (!SkillDb_GameLoaded()) return 0u;
    p = (const unsigned*)(g_live + (unsigned)k * BLD_LIVE_SZ);
    return Readable(p, sizeof(unsigned)) ? *p : 0u;
}

int SkillDb_SetMask(int k, unsigned mask)
{
    BYTE* r = Row(k);
    DWORD old = 0;
    unsigned* p;

    if (!r) return 0;
    mask &= MASK_ALL;

    // .rdata 는 읽기전용이라 잠깐만 열고 되돌린다. 파일이 아니라 메모리를 고치는 것이라
    // 게임을 끄면 원래대로 간다 — 그래서 skills.json 이 있다.
    p = (unsigned*)(r + BLD_OFF_MASK);
    if (!VirtualProtect(p, sizeof(unsigned), PAGE_READWRITE, &old)) return 0;
    *p = mask;
    VirtualProtect(p, sizeof(unsigned), old, &old);

    // 지금 하는 게임에도 먹이려면 런타임 사본까지 고쳐야 한다. 새 게임 때 정적 표에서
    // 복사되는 값이라, 세이브를 안 불러온 상태면 건드릴 것이 없다.
    if (SkillDb_GameLoaded()) {
        unsigned* q = (unsigned*)(g_live + (unsigned)k * BLD_LIVE_SZ);
        if (Readable(q, sizeof(unsigned))) *q = mask;
    }
    return 1;
}

const wchar_t* SkillDb_SkillName(int bit)
{
    LoadNameTables();
    return (bit >= 0 && bit < SKILL_N) ? g_skillName[bit] : L"?";
}
const wchar_t* SkillDb_LangName(int bit)
{
    LoadNameTables();
    return (bit >= 0 && bit < LANG_N) ? g_langName[bit] : L"?";
}

// ------------------------------------------------------------------ skills.json

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다.
// (PatchUtilKR · DialogUtilKR 과 같은 규칙 — 데이터는 한 자리에 모아 둬야 서로 찾는다.)
static void UpToDataDir(wchar_t* dir)
{
    wchar_t tmp[MAX_PATH];
    int n, i, cut2 = -1, cut1 = -1;
    lstrcpynW(tmp, dir, MAX_PATH);
    n = lstrlenW(tmp);
    if (n && tmp[n-1] == L'\\') tmp[--n] = 0;
    for (i = n - 1; i >= 0; i--) {
        if (tmp[i] != L'\\') continue;
        if (cut2 < 0) cut2 = i;
        else { cut1 = i; break; }
    }
    if (cut1 < 0 || cut2 <= cut1) return;
    tmp[cut2] = 0;
    if (lstrcmpiW(tmp + cut1 + 1, L"plugins") != 0) return;
    tmp[cut1 + 1] = 0;
    lstrcpyW(dir, tmp);
}

void SkillDb_JsonPath(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    out[0] = 0;
    GetModuleFileNameW(g_self, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"skills.json");
}

static void AppendU8(char* buf, int cap, int* len, const wchar_t* w)
{
    int room = cap - *len - 1;
    int n;
    if (room <= 0) return;
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf + *len, room, NULL, NULL);
    if (n > 0) *len += n - 1;     // 널 뺀 길이
}

#define JSON_ROW_CAP 320          // 한 줄 최대 — 이름 둘이 다 한글이어도 넉넉하다

int SkillDb_SaveJson(void)
{
    wchar_t path[MAX_PATH];
    char*   buf;
    int     len = 0, n = 0, left, cap, k;
    HANDLE  h;
    DWORD   wrote = 0;

    if (!SkillDb_Ready()) return 0;
    SkillDb_JsonPath(path, MAX_PATH);

    for (k = 0; k < BLD_COUNT; k++) if (SkillDb_Changed(k)) n++;
    if (n == 0) { DeleteFileW(path); return 0; }

    cap = n * JSON_ROW_CAP + 64;
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)cap);
    if (!buf) return 0;
    left = n;
    len += wsprintfA(buf + len, "[\n");
    for (k = 0; k < BLD_COUNT; k++) {
        if (!SkillDb_Changed(k)) continue;
        len += wsprintfA(buf + len, "  { \"city\": %d, \"code\": %d, \"mask\": \"0x%07X\", \"name\": \"",
                         SkillDb_City(k), SkillDb_Code(k), SkillDb_Mask(k));
        AppendU8(buf, cap, &len, SkillDb_CityName(k));
        len += wsprintfA(buf + len, " ");
        AppendU8(buf, cap, &len, SkillDb_Name(k));
        len += wsprintfA(buf + len, "\" }%s\n", (--left > 0) ? "," : "");
    }
    len += wsprintfA(buf + len, "]\n");

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, buf, (DWORD)len, &wrote, NULL);
        CloseHandle(h);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return (h != INVALID_HANDLE_VALUE);
}

// (도시, 건물코드) 로 줄을 찾는다. 이 쌍은 표 안에서 유일하다(1504개 전부 다르다).
static int FindRow(int city, int code)
{
    int k;
    for (k = 0; k < BLD_COUNT; k++)
        if (SkillDb_City(k) == city && SkillDb_Code(k) == code) return k;
    return -1;
}

// 아주 작은 훑개 — "이름" 다음의 수를 읽는다. 없으면 0 을 돌려주고 *ok = 0.
static const char* SkipTo(const char* p, const char* end, const char* key)
{
    int kl = lstrlenA(key);
    while (p < end) {
        if (*p == '"' && (end - p) > kl + 1 && !memcmp(p + 1, key, (SIZE_T)kl) && p[kl + 1] == '"') {
            p += kl + 2;
            while (p < end && (*p == ' ' || *p == ':' || *p == '\t')) p++;
            return p;
        }
        if (*p == '}') return NULL;
        p++;
    }
    return NULL;
}
static int ReadNum(const char* p, const char* end, int* out)
{
    int v = 0, any = 0, hex = 0, neg = 0;
    if (p >= end) return 0;
    if (*p == '"') p++;
    if (*p == '-') { neg = 1; p++; }
    if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { hex = 1; p += 2; }
    while (p < end) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (hex && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (hex && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        v = v * (hex ? 16 : 10) + d;
        any = 1; p++;
    }
    if (!any) return 0;
    *out = neg ? -v : v;
    return 1;
}

int SkillDb_ApplyJson(void)
{
    wchar_t path[MAX_PATH];
    HANDLE  h;
    DWORD   size, got = 0;
    char*   buf;
    const char *p, *end;
    int     applied = 0;

    if (!SkillDb_Ready()) return 0;
    SkillDb_JsonPath(path, MAX_PATH);

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) { CloseHandle(h); return 0; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(h); return 0; }
    ReadFile(h, buf, size, &got, NULL);
    CloseHandle(h);
    buf[got] = 0;

    p = buf; end = buf + got;
    while (p < end) {
        const char *q, *obj;
        int city = -1, code = -1, mask = 0, k;
        while (p < end && *p != '{') p++;
        if (p >= end) break;
        obj = p + 1;
        q = obj; while (q < end && *q != '}') q++;      // 이 덩어리의 끝

        { const char* f = SkipTo(obj, q, "city"); if (!f || !ReadNum(f, q, &city)) { p = q + 1; continue; } }
        { const char* f = SkipTo(obj, q, "code"); if (!f || !ReadNum(f, q, &code)) { p = q + 1; continue; } }
        { const char* f = SkipTo(obj, q, "mask"); if (!f || !ReadNum(f, q, &mask)) { p = q + 1; continue; } }

        k = FindRow(city, code);
        if (k >= 0 && SkillDb_SetMask(k, (unsigned)mask)) applied++;
        else LogW(L"[SkillUtilKR] skills.json: 도시 %d 건물 %d 를 못 찾았다.", city, code);
        p = q + 1;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (applied) LogW(L"[SkillUtilKR] skills.json 에서 %d줄 적용.", applied);
    return applied;
}
