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

// 레코드 하나의 이름("이름·성"). 인물 배열의 칸과 주인공 레코드가 배치가 같아 둘 다 쓴다.
static void RecName(const unsigned char* r, wchar_t* out, int cap)
{
    wchar_t a[32], b[32], full[80];
    out[0] = 0;
    ReadStr(r + LC_NAME1, LC_NAME_LEN, a, 32);
    ReadStr(r + LC_NAME2, LC_NAME_LEN, b, 32);
    if (!a[0] && !b[0]) return;
    if (a[0] && b[0]) wsprintfW(full, L"%s·%s", a, b);
    else              lstrcpyW(full, a[0] ? a : b);
    lstrcpynW(out, full, cap);
}

static void SlotName(int slot, wchar_t* out, int cap)
{
    RecName(g_tbl + slot * LIVECHAR_SIZE, out, cap);
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

// 인물 배열과 상관없이 읽는다 — 배열 검사에 실패해도 연도는 알 수 있어야 한다
// (주인공 탭은 배열을 안 쓰는데 나이를 생년에서 계산하려면 이 값이 필요하다).
int LiveChar_Year(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    int y;
    if (!base || !Readable(base + YEAR_RVA, sizeof(int))) return 0;
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

void LiveChar_NameAt(int slot, wchar_t* out, int cap)
{
    out[0] = 0;
    if (!g_ready || slot < 0 || slot >= LIVECHAR_COUNT || cap <= 0) return;
    SlotName(slot, out, cap);
}

// ---- 고용상태 ----
// CE 표가 이 필드를 안 짚어 줘서 레코드 안에서 직접 찾는다.
// 후보 오프셋마다 "세이브의 고용상태와 같은가"를 전부 세어 보고, 거의 다 맞는 자리를 고른다.
// 잘못 짚으면 엉뚱한 값을 덮어쓰게 되므로 조건을 빡빡하게 건다:
//   1) 275칸 전부 값이 0~3 안에 있어야 하고
//   2) 대조한 인물의 95% 이상이 맞아야 하며
//   3) 값이 두 가지 이상 나와야 한다(전부 0 인 빈 구역에 걸리지 않도록)

#define HIRE_SCAN_FROM 0xDA        /* 이름/성이 끝나는 자리 */
#define HIRE_MAX       3

static int g_hireOff = -1;

int LiveChar_HireOffset(void) { return g_hireOff; }

static int HireFieldSane(int off)
{
    int i;
    for (i = 0; i < LIVECHAR_COUNT; i++) {
        int v = *(const int*)(g_tbl + i * LIVECHAR_SIZE + off);
        if (v < 0 || v > HIRE_MAX) return 0;
    }
    return 1;
}

int LiveChar_CalibrateHire(const int* hire, const int* slot, int n)
{
    int off, bestOff = -1, bestHit = 0;

    if (!g_ready) return 0;
    if (g_hireOff >= 0) return 1;

    for (off = HIRE_SCAN_FROM; off + 4 <= LIVECHAR_SIZE; off++) {
        int i, hit = 0, tot = 0, first = -1, varied = 0;
        if (!HireFieldSane(off)) continue;
        for (i = 0; i < n; i++) {
            int v;
            if (slot[i] < 0) continue;
            v = *(const int*)(g_tbl + slot[i] * LIVECHAR_SIZE + off);
            tot++;
            if (v == hire[i]) hit++;
            if (first < 0) first = hire[i];
            else if (hire[i] != first) varied = 1;
        }
        if (tot < 20 || !varied) continue;
        if (hit * 100 < tot * 95) continue;
        if (hit > bestHit) { bestHit = hit; bestOff = off; }
    }
    if (bestOff < 0) return 0;
    g_hireOff = bestOff;
    return 1;
}

int LiveChar_Hire(int slot)
{
    if (!g_ready || g_hireOff < 0 || slot < 0 || slot >= LIVECHAR_COUNT) return -1;
    return *(const int*)(g_tbl + slot * LIVECHAR_SIZE + g_hireOff);
}

int LiveChar_SetHire(int slot, int v)
{
    unsigned char* p;
    DWORD old = 0;
    if (!g_ready || g_hireOff < 0 || slot < 0 || slot >= LIVECHAR_COUNT) return 0;
    if (v < 0 || v > HIRE_MAX) return 0;
    p = g_tbl + slot * LIVECHAR_SIZE + g_hireOff;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *(int*)p = v;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}

// ---- 부관/항해사/측량사/통역 ----

#define CREW_RVA  0x1B61A0u
#define CREW_BASE 4096            /* 값 = CREW_BASE + 인물 칸 번호 */
#define CREW_NONE 0xFFFFFFFFu

const wchar_t* LiveChar_CrewLabel(int which)
{
    static const wchar_t* kLabel[LIVECHAR_CREW_N] = { L"부관", L"항해사", L"측량사", L"통역" };
    return (which >= 0 && which < LIVECHAR_CREW_N) ? kLabel[which] : L"";
}

static unsigned* CrewPtr(int which)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    unsigned char* p;
    if (!base || which < 0 || which >= LIVECHAR_CREW_N) return NULL;
    p = (unsigned char*)base + CREW_RVA + which * 4;
    if (!Readable(p, sizeof(unsigned))) return NULL;
    return (unsigned*)p;
}

int LiveChar_Crew(int which)
{
    const unsigned* p = CrewPtr(which);
    unsigned v;
    if (!p) return -1;
    v = *p;
    if (v == CREW_NONE || v < CREW_BASE) return -1;
    v -= CREW_BASE;
    return (v < LIVECHAR_COUNT) ? (int)v : -1;
}

int LiveChar_SetCrew(int which, int slot)
{
    unsigned* p = CrewPtr(which);
    DWORD old = 0;
    unsigned v;
    if (!p) return 0;
    if (slot >= LIVECHAR_COUNT) return 0;
    v = (slot < 0) ? CREW_NONE : (unsigned)(CREW_BASE + slot);
    if (!VirtualProtect(p, sizeof(unsigned), PAGE_READWRITE, &old)) return 0;
    *p = v;
    VirtualProtect(p, sizeof(unsigned), old, &old);
    return 1;
}

// ================================================================== 주인공 레코드
// 배치는 livechar.h 참고. 인물 배열과 같은 구조라 LC_* 오프셋을 그대로 쓴다.

#define LC_GENDER 0x08
#define LC_BLOOD  0x10
#define LC_JOB    0x14
#define LC_FAME   0xA4
#define LC_INFAMY 0xA8
#define LC_BORNY  0xE0        /* 인물 배열에는 없다 — 이름/성 뒤에 붙는 주인공 전용 자리 */

static unsigned char* g_pl      = NULL;
static int            g_plState = LIVECHAR_E_READ;

int Player_Ready(void)  { return g_pl != NULL; }
int Player_Status(void) { return g_plState; }

int Player_Load(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    unsigned char* r;
    wchar_t nm[64];
    int face, gender;

    g_pl = NULL;
    if (!base) { g_plState = LIVECHAR_E_MODULE; return 0; }
    r = (unsigned char*)(base + PLAYER_RVA);
    if (!Readable(r, LIVECHAR_SIZE)) { g_plState = LIVECHAR_E_READ; return 0; }

    // 아직 인물을 안 만들었으면(=세이브 전) 이름 자리가 통째로 비어 있다.
    RecName(r, nm, 64);
    if (!nm[0]) { g_plState = LIVECHAR_E_EMPTY; return 0; }

    // 이름처럼 보이는 쓰레기에 속지 않도록 나머지 필드도 인물 레코드로 말이 되는지 본다.
    // (나이는 안 본다 — 주인공은 생년/월/일을 따로 들고 있어 +0x04 를 안 쓸 수도 있다.)
    face   = *(const int*)(r + LC_FACE);
    gender = *(const int*)(r + LC_GENDER);
    if (face < -1 || face > FACE_MAX)   { g_plState = LIVECHAR_E_SLOTS; return 0; }
    if (gender != 0 && gender != 1)     { g_plState = LIVECHAR_E_SLOTS; return 0; }

    g_pl = r;
    g_plState = LIVECHAR_OK;
    return 1;
}

void Player_Name(wchar_t* out, int cap)
{
    if (cap <= 0) return;
    out[0] = 0;
    if (g_pl) RecName(g_pl, out, cap);
}

static int PlField(int off, int lo, int hi, int bad)
{
    int v;
    if (!g_pl) return bad;
    v = *(const int*)(g_pl + off);
    return (v >= lo && v <= hi) ? v : bad;
}

int Player_Face(void)      { return PlField(LC_FACE,   -1, FACE_MAX, -1); }
int Player_Gender(void)    { return PlField(LC_GENDER,  0, 1,        -1); }
int Player_Blood(void)     { return PlField(LC_BLOOD,   0, 3,        -1); }
int Player_Job(void)       { return PlField(LC_JOB,     0, 63,       -1); }
int Player_Age(void)       { return PlField(LC_AGE, AGE_MIN, AGE_MAX, -9999); }
int Player_BirthYear(void) { return PlField(LC_BORNY, YEAR_MIN, YEAR_MAX, 0); }
int Player_Fame(void)      { return PlField(LC_FAME,   0, FAME_MAX,  -1); }
int Player_Infamy(void)    { return PlField(LC_INFAMY, 0, FAME_MAX,  -1); }

// 명성·악명 더하기. 0 아래로 안 내려가고 FAME_MAX 위로 안 올라간다.
static int PlAdd(int off, int delta)
{
    unsigned char* p;
    DWORD old = 0;
    int v;
    if (!g_pl) return -1;
    p = g_pl + off;
    v = *(const int*)p;
    if (v < 0 || v > FAME_MAX) v = 0;      // 쓰레기가 들어 있으면 0 부터 센다
    v += delta;
    if (v < 0) v = 0;
    if (v > FAME_MAX) v = FAME_MAX;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return -1;
    *(int*)p = v;
    VirtualProtect(p, sizeof(int), old, &old);
    return v;
}

int Player_AddFame(int delta)   { return PlAdd(LC_FAME,   delta); }
int Player_AddInfamy(int delta) { return PlAdd(LC_INFAMY, delta); }

int Player_SetGender(int g)
{
    unsigned char* p;
    DWORD old = 0;
    if (!g_pl || (g != 0 && g != 1)) return 0;
    p = g_pl + LC_GENDER;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *(int*)p = g;
    VirtualProtect(p, sizeof(int), old, &old);
    return 1;
}

// 초상화 교체. 게임은 이 값으로 MALE.CDS / FEMALE.CDS 에서 얼굴을 꺼내므로,
// 성별(+0x08)에 맞는 표의 얼굴코드를 줘야 한다(고르는 쪽에서 그 표만 보여준다).
// 세이브할 때 이 레코드가 그대로 저장되므로 여급/스폰서와 달리 따로 기억해 둘 필요가 없다.
int Player_SetFace(int code)
{
    unsigned char* p;
    DWORD old = 0;
    if (!g_pl || code < 0 || code > FACE_MAX) return 0;
    p = g_pl + LC_FACE;
    if (!VirtualProtect(p, sizeof(int), PAGE_READWRITE, &old)) return 0;
    *(int*)p = code;
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
