#include "livechar.h"

#define LC_FACE   0x00
#define LC_AGE    0x04
#define LC_CITY   0xAC
#define LC_BLDG   0xB0
#define LC_NAME1  0xB4
#define LC_NAME2  0xC7
#define LC_NAME_LEN 19

#define YEAR_RVA  0x1A4D20u    // 현재날짜 "연". 여기도 .data 뒷부분이라 실행 중에만 있다
#define FAME_RVA  0x1B614Cu    // 주인공 명성치(4바이트). ce/CDS_95.CT "주인공 정보"
#define FAME_MAX  10000000
#define YEAR_MIN  1400
#define YEAR_MAX  1700

#define CITY_MAX  226
#define AGE_MIN   (-80)
#define AGE_MAX   120
#define FACE_MAX  600

// 275칸 중 이만큼은 말이 되어야 배열을 믿는다. 아직 세이브를 안 불러왔으면 0 으로 차 있어서
// 이 문턱을 못 넘고, 그러면 편집 UI 가 통째로 잠긴다.
#define MIN_VALID 150

static unsigned char* g_tbl = NULL;
static int            g_ready = 0;
static int            g_status = LIVECHAR_E_READ;
static int            g_named = 0;
static int            g_ok = 0;

int LiveChar_Ready(void)      { return g_ready; }
int LiveChar_Status(void)     { return g_status; }
int LiveChar_NamedCount(void) { return g_named; }
int LiveChar_OkCount(void)    { return g_ok; }

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

// cp949 고정길이 필드를 wchar 로. 널이 없을 수도 있어 길이로도 자른다.
static void ReadStr(const unsigned char* p, int len, wchar_t* out, int cap)
{
    char buf[32];
    int n = 0, w;
    out[0] = 0;
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    while (n < len && p[n]) { buf[n] = (char)p[n]; n++; }
    buf[n] = 0;
    if (!n) return;
    w = MultiByteToWideChar(949, 0, buf, n, out, cap - 1);
    if (w <= 0) { out[0] = 0; return; }
    if (w > cap - 1) w = cap - 1;
    out[w] = 0;
    while (w > 0 && out[w-1] == L' ') out[--w] = 0;
}

static void SlotName(int slot, wchar_t* out, int cap)
{
    const unsigned char* r = g_tbl + slot * LIVECHAR_SIZE;
    wchar_t a[32], b[32], full[80];
    out[0] = 0;
    ReadStr(r + LC_NAME1, LC_NAME_LEN, a, 32);
    ReadStr(r + LC_NAME2, LC_NAME_LEN, b, 32);
    if (!a[0] && !b[0]) return;
    if (a[0] && b[0]) wsprintfW(full, L"%s·%s", a, b);
    else              lstrcpyW(full, a[0] ? a : b);
    lstrcpynW(out, full, cap);
}

// 이름이 붙은 칸이 인물 레코드로 말이 되는지.
// 이름 없는 칸은 이 시나리오에서 안 쓰는 자리라 아무 값이나 들어 있을 수 있어 아예 건너뛴다
// (전부 검사하게 뒀더니 275칸 중 91칸이 빈 칸이라 문턱에 걸려 기능이 통째로 잠겼다).
static int SlotOk(int slot, int* named)
{
    const unsigned char* r = g_tbl + slot * LIVECHAR_SIZE;
    int face, age, city, bldg;
    wchar_t nm[64];

    *named = 0;
    SlotName(slot, nm, 64);
    if (!nm[0]) return 0;
    *named = 1;

    face = *(const int*)(r + LC_FACE);
    age  = *(const int*)(r + LC_AGE);
    city = *(const int*)(r + LC_CITY);
    bldg = *(const int*)(r + LC_BLDG);
    if (age < AGE_MIN || age > AGE_MAX) return 0;
    if (face < -1 || face > FACE_MAX) return 0;
    if (city != 255 && (city < 0 || city >= CITY_MAX)) return 0;
    if (bldg != 255 && (bldg < 0 || bldg > 5)) return 0;
    return 1;
}

int LiveChar_Load(void)
{
    const unsigned char* base;
    unsigned char* tbl;
    int i, ok = 0, named = 0;

    if (g_ready) return 1;

    base = (const unsigned char*)GetModuleHandleW(NULL);
    if (!base) { g_status = LIVECHAR_E_MODULE; return 0; }
    tbl = (unsigned char*)(base + LIVECHAR_RVA);
    if (!Readable(tbl, (SIZE_T)LIVECHAR_COUNT * LIVECHAR_SIZE) ||
        !Readable(base + YEAR_RVA, sizeof(int))) { g_status = LIVECHAR_E_READ; return 0; }

    g_tbl = tbl;
    for (i = 0; i < LIVECHAR_COUNT; i++) {
        int n = 0;
        if (SlotOk(i, &n)) ok++;    // ok = 이름이 있고 내용도 말이 되는 칸
        named += n;                 // named = 이름이 있는 칸
    }
    g_named = named;
    g_ok = ok;
    // 세이브를 아직 안 불러왔으면 배열이 통째로 0 이라 이름이 하나도 안 나온다.
    if (named == 0)      { g_tbl = NULL; g_status = LIVECHAR_E_EMPTY; return 0; }
    // 쓰지 않는 칸은 쓰레기로 차 있고, 그 바이트가 cp949 로 풀리면 이름이 "있는 것처럼"
    // 보인다(savedata.c 가 세이브에서 겪은 것과 같은 함정). 그래서 이름 수가 아니라
    // 필드까지 말이 되는 칸 수로 판정한다.
    if (ok < MIN_VALID)  { g_tbl = NULL; g_status = LIVECHAR_E_SLOTS; return 0; }

    { int y = *(const int*)(base + YEAR_RVA);
      if (y < YEAR_MIN || y > YEAR_MAX) { g_tbl = NULL; g_status = LIVECHAR_E_YEAR; return 0; } }

    g_status = LIVECHAR_OK;
    g_ready = 1;
    OutputDebugStringW(L"[CharacterUtilKR] 인물 배열(실행 중) 확보.");
    return 1;
}

int LiveChar_Year(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    int y;
    if (!base || !g_tbl) return 0;
    y = *(const int*)(base + YEAR_RVA);
    return (y >= YEAR_MIN && y <= YEAR_MAX) ? y : 0;
}

// 인물 배열과 무관하게 읽을 수 있으므로 g_ready 를 따지지 않는다
// (배열 검사에 실패해도 명성은 보여줄 수 있어야 한다).
int LiveChar_PlayerFame(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    int v;
    if (!base) return -1;
    if (!Readable(base + FAME_RVA, sizeof(int))) return -1;
    v = *(const int*)(base + FAME_RVA);
    return (v >= 0 && v <= FAME_MAX) ? v : -1;
}

// 표기 흔들림(가운뎃점/공백/마침표)을 걷어낸 비교용 키. chardb.c 의 NormName 과 같은 뜻이다.
static void NormName(const wchar_t* s, wchar_t* out, int cap)
{
    int n = 0;
    if (cap <= 0) return;
    while (*s && n < cap - 1) {
        if (*s != L'·' && *s != L'・' && *s != L' ' && *s != L'.') out[n++] = *s;
        s++;
    }
    out[n] = 0;
}

// 이름으로 찾고, 여러 개가 걸릴 때만 얼굴로 가른다.
// 얼굴을 조건으로 먼저 걸지 않는 이유: 세이브의 얼굴코드는 이름 역추적에 실패하면 -1 이 되고
// (savedata.c ResolveFace), 실행 중 배열의 얼굴 필드가 같은 번호 체계라는 보장도 없다.
// 그걸 필수 조건으로 걸면 멀쩡한 인물까지 통째로 못 찾게 된다. 얼굴은 초상화용으로 충분하다
// (못 찾으면 Face_Draw 가 빈 액자로 그린다).
int LiveChar_Find(const wchar_t* name, int faceCode)
{
    wchar_t want[96], have[96], nm[64];
    int i, hit = -1, hits = 0, faceHit = -1, faceHits = 0;

    if (!g_ready || !name || !name[0]) return -1;
    NormName(name, want, 96);
    if (!want[0]) return -1;

    for (i = 0; i < LIVECHAR_COUNT; i++) {
        const unsigned char* r = g_tbl + i * LIVECHAR_SIZE;
        SlotName(i, nm, 64);
        if (!nm[0]) continue;
        NormName(nm, have, 96);
        if (lstrcmpW(want, have) != 0) continue;
        hits++; hit = i;
        if (faceCode >= 0 && *(const int*)(r + LC_FACE) == faceCode) { faceHits++; faceHit = i; }
    }

    if (hits == 1) return hit;              // 이름이 유일하면 그걸로 끝
    if (faceHits == 1) return faceHit;      // 동명이인이면 얼굴로 가른다
    return -1;                              // 그래도 못 가리면 쓰지 않는다
}

int LiveChar_Age(int slot)
{
    if (!g_ready || slot < 0 || slot >= LIVECHAR_COUNT) return -9999;
    return *(const int*)(g_tbl + slot * LIVECHAR_SIZE + LC_AGE);
}

#define LC_SKILL0 0x38   /* 특기 1(항해술). 이후 4바이트씩 27개 */

static int* SkillPtr(int slot, int id)
{
    if (!g_ready || slot < 0 || slot >= LIVECHAR_COUNT) return NULL;
    if (id < 1 || id > LIVECHAR_SKILL_N) return NULL;
    return (int*)(g_tbl + slot * LIVECHAR_SIZE + LC_SKILL0 + (id - 1) * 4);
}

int LiveChar_Skill(int slot, int id)
{
    const int* p = SkillPtr(slot, id);
    if (!p) return -1;
    return (*p >= 0 && *p <= LIVECHAR_SKILL_MAX) ? *p : -1;
}

int LiveChar_SetSkill(int slot, int id, int lv)
{
    int* p = SkillPtr(slot, id);
    DWORD old = 0;
    if (!p || lv < 0 || lv > LIVECHAR_SKILL_MAX) return 0;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *p = lv;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}

int LiveChar_SetBirthYear(int slot, int year)
{
    unsigned char* p;
    DWORD old = 0;
    int now;

    if (!g_ready || slot < 0 || slot >= LIVECHAR_COUNT) return 0;
    if (year < LIVECHAR_YEAR_MIN || year > LIVECHAR_YEAR_MAX) return 0;
    now = LiveChar_Year();
    if (!now) return 0;

    // .data 라 대개 이미 쓰기 가능하지만, 판본에 따라 다를 수 있으니 열고 되돌린다.
    p = g_tbl + slot * LIVECHAR_SIZE + LC_AGE;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *(int*)p = now - year;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}
