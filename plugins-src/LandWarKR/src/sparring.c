#include "sparring.h"
#include "cities_data.h"     // TradeUtilKR/src — 도시 이름 226개(게임 도시 번호 차례)

// ---------------------------------------------------------------- 자리 (RVA = VA - 0x400000)
// 한 자리만 밀려도 엉뚱한 코드를 부르게 되니 오른쪽 VA 와 꼭 맞대어 볼 것.
#define SP_ENTRY_RVA    0x0004AA30u   // 0x0044AA30  육상전 한 판
#define SP_OBJ_RVA      0x001A47E8u   // 0x005A47E8  CLandWar (+0x98 아군 · +0x9C 적)
#define SP_SKIP_RVA     0x0004A830u   // 0x0044A830  갈래 1 「그냥 지나간다」 굴림
#define SP_ALLIED_RVA   0x001AA2B8u   // 0x005AA2B8  아군 함대 자리
#define SP_ENEMYVT      0x004C3620u   // 적 쪽 함대 자리의 함수표 (절대값 그대로 쓴다)
#define SP_REGION_RVA   0x00169EC0u   // 0x00569EC0  필드 지역 여덟 (32바이트)
#define SP_CITY_RVA     0x001863A8u   // 0x005863A8  도시 배열 (92바이트)
#define SP_NATION_RVA   0x001859C0u   // 0x005859C0  나라 형편 (16바이트) +0x00 수도 도시
#define SP_RAND_RVA     0x000B7C0Fu   // 0x004B7C0F  rand(n)
#define SP_MATE_RVA     0x0007CC60u   // 0x0047CC60  부하 레코드 얻기
#define SP_CHAR_RVA     0x0018BF90u   // 0x0058BF90  인물 배열 (284바이트 x 281)
#define SP_HERO_RVA     0x001B60A0u   // 0x005B60A0  주인공 레코드
#define SP_GOLD_RVA     0x001B6194u   // 소지금
#define SP_FAME_RVA     0x001B614Cu   // 명성
#define SP_INFAM_RVA    0x001B6150u   // 악명
#define SP_CURCITY_RVA  0x001B6154u   // 지금 도시. -1 이면 도시 밖
#define SP_FLEET_RVA    0x001B3928u   // 0x005B3928  내 함대
#define SP_CREW_RVA     0x000745F0u   // 0x004745F0  그 함대의 선원 수 __thiscall(함대)

#define SP_CITY_SZ      92
#define SP_CITY_SCALE   0x08
#define SP_CITY_CULT    0x58
#define SP_NATION_SZ    16
#define SP_NATION_N     78
#define SP_CHAR_SZ      284
#define SP_CHAR_N       281
#define SP_CHAR_NAME    0xBC          // 0x004319C0 이 `lea eax,[ecx+0xBC]` 를 낸다
#define SP_CHAR_NATION  0x14          // 국적
#define SP_ABIL_OFF     0x20          // +0x20 체력 · 지력 · 무력 · 매력 · 운
#define SP_ABIL_N       5
#define SP_LEADER0      246           // 0x0048BF1D 의 `lea ebx,[eax+esi*2+0xF6]`

typedef int  (__cdecl   *EntryFn)(int kind, void* allied, void* enemy, void* city, int terrain);
typedef int  (__cdecl   *RandFn)(int n);
typedef void*(__fastcall*MateFn)(void* self, void* edx, int a, int b);
typedef int  (__fastcall*CrewFn)(void* self, void* edx);

// 함대 자리 한 벌 — 아군(0x005AA2B8)도 적도 같은 꼴이다. 열여섯 바이트에 하나 더 붙여 둔다.
typedef struct { void* vt; int id; int spare; int men; int extra; } RefObj;

static BYTE* g_base = NULL;

// 필드 부대 한 벌.
typedef struct { int leader, lo, range; } FieldSet;
static FieldSet g_field[SP_FIELD_N];
static int      g_fieldOk = 0;

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
    MEMORY_BASIC_INFORMATION mi;
    if (!p || !VirtualQuery(p, &mi, sizeof(mi))) return 0;
    if (mi.State != MEM_COMMIT) return 0;
    return (SIZE_T)((BYTE*)mi.BaseAddress + mi.RegionSize - (BYTE*)p) >= n;
}

int Spar_Ready(void) { return g_base != NULL; }

int Spar_Load(void)
{
    const int* tbl;
    int r, v;

    if (g_base) return 1;
    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) return 0;

    tbl = (const int*)(g_base + SP_REGION_RVA);
    if (!Readable(tbl, SP_REGION_N * 32)) { g_base = NULL; return 0; }

    // 지역 한 칸 32바이트 — +0x00~+0x0C 네모, +0x10·+0x14 첫 벌, +0x18·+0x1C 둘째 벌.
    for (r = 0; r < SP_REGION_N; r++) {
        for (v = 0; v < 2; v++) {
            FieldSet* f = &g_field[r * 2 + v];
            f->leader = SP_LEADER0 + r * 2 + v;
            f->lo     = tbl[r * 8 + 4 + v * 2];
            f->range  = tbl[r * 8 + 5 + v * 2];
            if (f->lo < 0 || f->lo > 9999 || f->range < 0 || f->range > 9999) { f->lo = 0; f->range = 0; }
        }
    }
    g_fieldOk = 1;
    LogW(L"[LandWarKR] 모의전 — 지역표 0x%08X, 필드 부대 %d벌",
         (unsigned)(UINT_PTR)tbl, SP_FIELD_N);
    return 1;
}

// ---------------------------------------------------------------- 이름표

static const wchar_t* kTerrain[SP_TERRAIN_N] = { L"도시", L"초지", L"숲", L"황무지" };
static const int      kTerrainArg[SP_TERRAIN_N] = { 7, 2, 3, 4 };   // 0x0044A624 가 가르는 값

static const wchar_t* kCulture[SP_CULTURE_N] = {
    L"이베리아", L"북유럽", L"지중해", L"아프리카", L"이슬람", L"인도",
    L"중국", L"중앙아시아", L"동남아시아", L"일본", L"아메리카",
};

// 필드 지역 여덟 — 표의 네모 안에 드는 도시로 이름을 붙였다.
// (0 은 아테네·카이로·다마스쿠스 …, 7 은 도시가 하나도 안 드는 북아메리카 안쪽이다)
static const wchar_t* kRegion[SP_REGION_N] = {
    L"동지중해·근동", L"아프리카", L"인도·서아시아", L"중국",
    L"일본", L"중앙아시아", L"동남아시아", L"북아메리카",
};

// 문화권이 내는 진형 여덟 (0x004A1320 의 뜀표 0x004A13C0).
static const wchar_t* kShape[SP_CULTURE_N] = {
    L"제독 · 포병 · 총대 · 기병",           // 0 이베리아
    L"제독 · 포병 · 총대 · 기병",           // 1 북유럽
    L"제독 · 포병 · 총대 · 기병",           // 2 지중해
    L"족장 · 주술사 · 궁병 · 창병 · 경보병", // 3 아프리카
    L"장군 · 포병 · 낙타병 · 궁병",         // 4 이슬람
    L"장군 · 고승 · 코끼리병 · 궁병",       // 5 인도
    L"장군 · 화포병 · 궁병 · 기병",         // 6 중국
    L"장군 · 궁병 · 기병",                  // 7 중앙아시아
    L"족장 · 주술사 · 궁병 · 창병 · 경보병", // 8 동남아시아
    L"영주 · 사무라이 · 닌자",              // 9 일본
    L"족장 · 표범 · 인디오 · 궁병",         // 10 아메리카
};

const wchar_t* Spar_TerrainName(int t)
{ return (t >= 0 && t < SP_TERRAIN_N) ? kTerrain[t] : L"?"; }

const wchar_t* Spar_CultureName(int c)
{ return (c >= 0 && c < SP_CULTURE_N) ? kCulture[c] : L"?"; }

const wchar_t* Spar_RegionName(int r)
{ return (r >= 0 && r < SP_REGION_N) ? kRegion[r] : L"?"; }

const wchar_t* Spar_ShapeText(int culture)
{ return (culture >= 0 && culture < SP_CULTURE_N) ? kShape[culture] : L""; }

const wchar_t* Spar_CityName(int city)
{ return (city >= 0 && city < SP_CITY_N) ? kCities[city].name : L"?"; }

// ---------------------------------------------------------------- 도시 방어부대

static BYTE* CityRec(int city)
{
    BYTE* p;
    if (!g_base || city < 0 || city >= SP_CITY_N) return NULL;
    p = g_base + SP_CITY_RVA + (unsigned)city * SP_CITY_SZ;
    return Readable(p, SP_CITY_SZ) ? p : NULL;
}

int Spar_CityCulture(int city)
{
    const BYTE* p = CityRec(city);
    int c;
    if (!p) return -1;
    c = *(const int*)(p + SP_CITY_CULT);
    return (c >= 0 && c < SP_CULTURE_N) ? c : -1;
}

int Spar_CityScale(int city)
{
    const BYTE* p = CityRec(city);
    int s;
    if (!p) return -1;
    s = *(const int*)(p + SP_CITY_SCALE);
    return (s >= 0 && s <= 9) ? s : -1;
}

// 0x004A11D0 — n = 50 x 규모² + 100 + rand(50), 규모 2 이하면 곱하기 2.
static int CityMen(int city, int roll)
{
    int s = Spar_CityScale(city), n;
    if (s < 0) return 0;
    n = 50 * s * s + 100 + roll;
    if (s <= 2) n *= 2;
    return n;
}
int Spar_CityMenLo(int city) { return CityMen(city, 0); }
int Spar_CityMenHi(int city) { return CityMen(city, 49); }

// ---------------------------------------------------------------- 필드 부대

int Spar_FieldRegion(int slot) { return (slot >= 0 && slot < SP_FIELD_N) ? slot / 2 : -1; }

int Spar_FieldLeader(int slot)
{ return (g_fieldOk && slot >= 0 && slot < SP_FIELD_N) ? g_field[slot].leader : -1; }

int Spar_FieldMenLo(int slot)
{ return (g_fieldOk && slot >= 0 && slot < SP_FIELD_N) ? g_field[slot].lo : 0; }

int Spar_FieldMenHi(int slot)
{
    if (!g_fieldOk || slot < 0 || slot >= SP_FIELD_N) return 0;
    return g_field[slot].lo + (g_field[slot].range > 0 ? g_field[slot].range - 1 : 0);
}

static BYTE* CharRec(int idx)
{
    BYTE* p;
    if (!g_base || idx < 0 || idx >= SP_CHAR_N) return NULL;
    p = g_base + SP_CHAR_RVA + (unsigned)idx * SP_CHAR_SZ;
    return Readable(p, SP_CHAR_SZ) ? p : NULL;
}

int Spar_FieldLeaderName(int slot, wchar_t* out, int cap)
{
    const BYTE* rec = CharRec(Spar_FieldLeader(slot));
    char raw[24];
    int i;

    if (cap > 0) out[0] = 0;
    if (!rec || cap <= 0) return 0;
    CopyMemory(raw, rec + SP_CHAR_NAME, 19);
    raw[19] = 0;
    if (!raw[0]) return 0;
    for (i = 0; i < 19 && raw[i]; i++)
        if ((unsigned char)raw[i] < 0x20) return 0;      // 글자가 아니면 포기
    return MultiByteToWideChar(949, 0, raw, -1, out, cap) > 0 ? 1 : 0;
}

// 대장 국적 → 그 나라 수도 → 그 도시의 문화권 (0x00447070 이 걷는 길과 같다).
int Spar_FieldCulture(int slot)
{
    const BYTE* rec = CharRec(Spar_FieldLeader(slot));
    const BYTE* nat;
    int nation, capital;

    if (!rec) return -1;
    nation = *(const int*)(rec + SP_CHAR_NATION);
    if (nation < 0 || nation >= SP_NATION_N) return -1;
    nat = g_base + SP_NATION_RVA + (unsigned)nation * SP_NATION_SZ;
    if (!Readable(nat, SP_NATION_SZ)) return -1;
    capital = *(const int*)nat;
    return Spar_CityCulture(capital);
}

// ---------------------------------------------------------------- 내 병력

// 배치 화면에 뜨는 수는 **선원 + 1**(제독 자신)이다 — 0x0044A7C8 이 아군 자리의
// +0x0C 에 하나를 더해 CLandWar+0x38 에 적는다.
int Spar_FleetMen(void)
{
    const RefObj* live;
    int crew = -1;

    if (!g_base && !Spar_Load()) return -1;
    __try {
        CrewFn fn = (CrewFn)(g_base + SP_CREW_RVA);
        crew = fn(g_base + SP_FLEET_RVA, NULL);
    } __except (EXCEPTION_EXECUTE_HANDLER) { crew = -1; }

    // 게임 함수가 안 서면 아군 자리에 남아 있는 사본을 본다(걸음마다 함대에서 떠 온 값이다).
    if (crew < 0 || crew > 100000) {
        live = (const RefObj*)(g_base + SP_ALLIED_RVA);
        if (!Readable(live, sizeof(RefObj))) return -1;
        crew = live->men;
    }
    if (crew < 0 || crew > 100000) return -1;
    return crew + 1;
}

// ---------------------------------------------------------------- 판 벌이기

int Spar_CanRun(wchar_t* why, int cap)
{
    if (cap > 0) why[0] = 0;
    if (!g_base && !Spar_Load()) {
        if (cap > 0) lstrcpynW(why, L"게임 실행 파일이 우리가 아는 그것이 아닙니다.", cap);
        return 0;
    }
    if (!Readable(g_base + SP_CURCITY_RVA, 4) || !Readable(g_base + SP_ALLIED_RVA, 16)) {
        if (cap > 0) lstrcpynW(why, L"아직 세이브를 안 불러온 것 같습니다.", cap);
        return 0;
    }
    if (*(const int*)(g_base + SP_CURCITY_RVA) != -1) {
        if (cap > 0) lstrcpynW(why, L"도시 안입니다 — 밖으로 나온 뒤에 눌러 주세요.", cap);
        return 0;
    }
    return 1;
}

// 싸움이 건드리는 것들. 모의전은 싸움 뒤에 이것을 되돌린다.
// (선원은 안 넣었다 — 육상전은 함대 선원 수를 건드리지 않는다. 사상자는 판 위에서만 세고,
//  판이 끝나면 0x005AA2B8 의 사본에만 남는데 그 사본은 한 걸음마다 함대에서 다시 떠 온다.)
typedef struct {
    int   ok;
    int   gold, fame, infamy;
    int   hero[SP_ABIL_N];
    BYTE* mate;
    int   mateAbil[SP_ABIL_N];
} Snap;

static void Take(Snap* s)
{
    BYTE* hero = g_base + SP_HERO_RVA;
    int i;

    ZeroMemory(s, sizeof(*s));
    if (!Readable(g_base + SP_GOLD_RVA, 4) || !Readable(hero, SP_CHAR_SZ)) return;
    s->gold   = *(const int*)(g_base + SP_GOLD_RVA);
    s->fame   = *(const int*)(g_base + SP_FAME_RVA);
    s->infamy = *(const int*)(g_base + SP_INFAM_RVA);
    for (i = 0; i < SP_ABIL_N; i++) s->hero[i] = *(const int*)(hero + SP_ABIL_OFF + i * 4);

    // 무력은 부관도 오른다(0x00449730 이 0x0047CC60 으로 부하 0번을 떠 온다).
    __try {
        MateFn mate = (MateFn)(g_base + SP_MATE_RVA);
        BYTE* rec = (BYTE*)mate(hero, NULL, 0, 0);
        if (Readable(rec, SP_CHAR_SZ)) {
            s->mate = rec;
            for (i = 0; i < SP_ABIL_N; i++)
                s->mateAbil[i] = *(const int*)(rec + SP_ABIL_OFF + i * 4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { s->mate = NULL; }
    s->ok = 1;
}

static void Put(const Snap* s)
{
    BYTE* hero = g_base + SP_HERO_RVA;
    int i;
    if (!s->ok) return;
    *(int*)(g_base + SP_GOLD_RVA)  = s->gold;
    *(int*)(g_base + SP_FAME_RVA)  = s->fame;
    *(int*)(g_base + SP_INFAM_RVA) = s->infamy;
    for (i = 0; i < SP_ABIL_N; i++) *(int*)(hero + SP_ABIL_OFF + i * 4) = s->hero[i];
    if (s->mate)
        for (i = 0; i < SP_ABIL_N; i++) *(int*)(s->mate + SP_ABIL_OFF + i * 4) = s->mateAbil[i];
}

// 갈래 1 은 0x0044A830 이 「그냥 지나간다」를 굴린다. 모의전은 붙자고 부르는 것이라
// 부르는 동안만 눌러 둔다. 앞 다섯 바이트가 우리가 아는 그것일 때만 손댄다.
static const unsigned char kSkipHead[5]  = { 0x83, 0x7C, 0x24, 0x04, 0x01 };  // cmp [esp+4],1
static const unsigned char kSkipNever[5] = { 0x33, 0xC0, 0xC2, 0x08, 0x00 };  // xor eax,eax / ret 8

static int SkipOff(unsigned char* saved)
{
    BYTE* p = g_base + SP_SKIP_RVA;
    DWORD old;
    if (!Readable(p, 5) || memcmp(p, kSkipHead, 5) != 0) return 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    CopyMemory(saved, p, 5);
    CopyMemory(p, kSkipNever, 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}

static void SkipOn(const unsigned char* saved)
{
    BYTE* p = g_base + SP_SKIP_RVA;
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    CopyMemory(p, saved, 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

// 갈래 1·2 를 한 자리에서 부른다. 여기는 반드시 게임 스레드다 —
// 이 안에서 게임이 제 화면과 제 메시지 고리를 돌린다.
static int Run(int kind, void* enemy, void* city, int terrain, int restore, int myMen)
{
    EntryFn entry  = (EntryFn)(g_base + SP_ENTRY_RVA);
    void*   allied = g_base + SP_ALLIED_RVA;
    RefObj  mine;
    unsigned char saved[5];
    int patched = 0, r = -1;
    Snap snap;

    if (terrain < 0 || terrain >= SP_TERRAIN_N) terrain = 0;

    // 병력을 내가 정했으면 아군 자리를 **베껴서** 넘긴다. 0x005AA2B8 을 직접 고치면
    // 싸움이 끝난 뒤 되쓰기(0x004495D5 부상병 복귀)까지 거기에 남는다. 사본이면
    // 그 되쓰기가 이 스택 위에서 끝난다.
    if (myMen > 0 && Readable(allied, sizeof(RefObj))) {
        if (myMen < SP_MEN_MIN) myMen = SP_MEN_MIN;
        if (myMen > SP_MEN_MAX) myMen = SP_MEN_MAX;
        mine = *(const RefObj*)allied;
        mine.men = myMen - 1;            // 게임이 제독 하나를 더한다(0x0044A7CB)
        allied = &mine;
    }
    snap.ok = 0;
    if (restore) Take(&snap);
    if (kind == 1) patched = SkipOff(saved);

    __try {
        r = entry(kind, allied, enemy, city, kTerrainArg[terrain]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LogW(L"[LandWarKR] !! 모의전에서 예외 0x%08X", GetExceptionCode());
        r = -1;
    }

    if (patched) SkipOn(saved);

    // 우리가 넘긴 아군·적 자리는 이 함수의 스택이다. CLandWar 에 그대로 남겨 두면
    // 「육상전 부대」 창이 1초마다 +0x9C 를 따라가 사라진 자리를 읽는다(적장이 엉뚱하게
    // 뜬다). 판이 끝났으니 비워 둔다 — 다음 판은 0x0044A5B0 이 어차피 새로 채운다.
    if (Readable(g_base + SP_OBJ_RVA + 0xA0, 4)) {
        *(void**)(g_base + SP_OBJ_RVA + 0x98) = NULL;
        *(void**)(g_base + SP_OBJ_RVA + 0x9C) = NULL;
    }

    if (restore) Put(&snap);
    LogW(L"[LandWarKR] 모의전 갈래 %d · 싸움터 %s · 내 병력 %d → %d%s",
         kind, Spar_TerrainName(terrain), (myMen > 0) ? myMen : Spar_FleetMen(), r,
         restore ? L" (되돌림)" : L"");
    return r;
}

int Spar_RunCity(int city, int terrain, int restore, int myMen)
{
    BYTE* rec;
    wchar_t why[160];
    if (!Spar_CanRun(why, 160)) return -1;
    rec = CityRec(city);
    if (!rec) return -1;
    return Run(2, NULL, rec, terrain, restore, myMen);
}

int Spar_RunField(int slot, int terrain, int restore, int myMen)
{
    // 0x0048BF24 벌이 짓는 그 열여섯 바이트를 그대로 짓는다.
    RefObj foe;
    RandFn dice;
    wchar_t why[160];

    if (!Spar_CanRun(why, 160)) return -1;
    if (!g_fieldOk || slot < 0 || slot >= SP_FIELD_N) return -1;

    dice = (RandFn)(g_base + SP_RAND_RVA);
    foe.vt    = (void*)SP_ENEMYVT;
    foe.id    = (1 << 12) | g_field[slot].leader;      // 갈래 1(인물) · 번호
    foe.spare = 0;
    foe.men   = g_field[slot].lo;
    foe.extra = 0;
    __try {
        if (g_field[slot].range > 1) foe.men += dice(g_field[slot].range);
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
    if (foe.men < 1) foe.men = 1;

    return Run(1, &foe, NULL, terrain, restore, myMen);
}
