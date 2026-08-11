#include "marketdb.h"
#include <MinHook.h>
#include "goods_names.h"   // TradeUtilKR/src — kTradeGoods[70]
#include "cities_data.h"   // TradeUtilKR/src — kCities[226]

// 자리와 규칙은 marketdb.h 에 적어 뒀다.

#define GOOD_PIC_BASE 134  // 교역품 그림 = ITEM.CDS 134 + 종류 (TradeUtilKR 과 같다)

static unsigned char* g_base = NULL;
static MktRow   g_row[16];
static int      g_rowN = 0;
static MktCargo g_cargo[MKT_CARGO_MAX];
static int      g_cargoN = 0;

static int Readable(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}
static int Writable(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!Readable(p, n)) return 0;
    VirtualQuery(p, &mbi, sizeof(mbi));
    return (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static unsigned char* Base(void)
{
    if (!g_base) g_base = (unsigned char*)GetModuleHandleW(NULL);
    return g_base;
}
int Mkt_Ready(void) { return Base() != NULL; }

// ── 게임 교역소가 열려 있는 동안은 짐이 없다 ────────────────────────────────────
// 게임은 교역 대화를 열 때 짐 여덟 칸을 통째로 비운다 — 0x481580 이 매각 목록을 만든
// 직후(0x4816AE) 곧바로 0x4B57F0(ClearCargo)을 부르고, 그 함수는 {-1,0,-1,0} 을
// 여덟 칸에 되쓴다. 물건은 그동안 대화창이 들고 있다가 [결정] 때 0x4B5830(AddCargo)
// 으로 되돌아온다. 그래서 그 사이에 우리 창을 열면 "실은 것이 없습니다" 가 뜬다.
//
// 더 나쁜 것은 그 사이에 우리가 사고파는 것이다 — 대화창은 칸이 비어 있다고 믿고 있어서
// 우리가 칸을 차지해 두면 되돌려 넣을 자리가 모자라 게임 쪽 물건이 갈 곳을 잃는다.
// 그래서 아예 잠근다.
//
// 0x481580 은 그 안에 모달 루프(0x481776~ 0x459CC0 펌프)를 품고 있어서
// 들어갔다 나오는 동안이 곧 "교역소가 열려 있는 동안" 이다.
#define MKT_TRADEFN_RVA 0x00081580u
static const unsigned char kTradeSig[] = {
    0x83, 0xEC, 0x2C,                    // sub esp, 2Ch
    0x53, 0x56, 0x57,                    // push ebx / esi / edi
    0x8B, 0xF1,                          // mov esi, ecx
    0x55,                                // push ebp
    0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00   // push 0 x3  (BuildSaleList 개수 호출)
};

typedef int (__fastcall *TradeFn)(void*, void*);
static TradeFn g_origTrade = NULL;
static long    g_tradeDepth = 0;

static int __fastcall DetourTrade(void* thisptr, void* edx)
{
    int r;
    InterlockedIncrement(&g_tradeDepth);
    r = g_origTrade(thisptr, edx);
    InterlockedDecrement(&g_tradeDepth);
    return r;
}

int Mkt_TradeOpen(void) { return g_tradeDepth > 0; }

void Mkt_HookInstall(void)
{
    unsigned char* fn;
    unsigned i;
    if (g_origTrade || !Base()) return;
    fn = g_base + MKT_TRADEFN_RVA;
    if (!Readable(fn, sizeof(kTradeSig))) return;
    for (i = 0; i < sizeof(kTradeSig); i++)
        if (fn[i] != kTradeSig[i]) return;      // 다른 빌드다 — 안 건다
    if (MH_CreateHook(fn, &DetourTrade, (LPVOID*)&g_origTrade) != MH_OK) { g_origTrade = NULL; return; }
    if (MH_EnableHook(fn) != MH_OK) g_origTrade = NULL;
}

void Mkt_HookRemove(void)
{
    if (!g_origTrade) return;
    MH_DisableHook(g_base + MKT_TRADEFN_RVA);
    g_origTrade = NULL;
}

static int* CityField(int city, int off)
{
    unsigned char* p;
    if (!Base() || city < 0 || city >= MKT_CITY_N) return NULL;
    p = g_base + MKT_CITY_RVA + (unsigned)city * MKT_CITY_SZ + off;
    return Readable(p, 4) ? (int*)p : NULL;
}

static int Region(int city)
{
    const unsigned char* p;
    if (!Base() || city < 0 || city >= MKT_CITY_N) return -1;
    p = g_base + MKT_REC_RVA + (unsigned)city * MKT_REC_SZ + MKT_REC_REGION;
    if (!Readable(p, 4)) return -1;
    { int v = *(const int*)p; return (v >= 0 && v < MKT_REGION_N) ? v : -1; }
}

// 그 지역에서 그 교역품의 기준가. 게임의 0x42E3C0 이 읽는 격자 그대로.
static int BasePrice(int region, int kind)
{
    const int* p;
    if (region < 0 || kind < 0 || kind >= MKT_GOODS_N || !Base()) return -1;
    p = (const int*)(g_base + MKT_PRICE_RVA + ((unsigned)region + 34u * (unsigned)kind) * 4);
    return Readable(p, 4) ? *p : -1;
}

// 기준가 -> 단가. 게임이 3/2 를 곱한다.
static int UnitPrice(int base) { return base > 0 ? base * 3 / 2 : -1; }

// ── 게임의 가격 함수를 그대로 부른다 ──────────────────────────────────────────
// 0x00480890 __thiscall Price(this, 교역품종류, 기준가또는-1)
//   판매목록 빌더(0x480CC0)가 0x480D98 에서 (종류, -1) 로 부르는 그 함수다.
//   지역 기준가 표를 읽고, 도시 특산가로 낮추고, 품목 분류(향신료 · 카카오 · 커피 ·
//   담배 · 차 …)와 그 항로를 발견한 뒤 지난 날수까지 반영한다. 우리가 베끼던
//   "지역 기준가 x 시세/100" 어림값으로는 절대 못 맞추던 부분이다.
//   this 는 도시 id(+0x90)와 연결 내륙도시 목록(+0xB0 배열 · +0xB8 개수)만 쓰므로
//   그 두 자리만 채운 껍데기를 만들어 넘긴다.
#define MKT_PRICEFN_RVA 0x00080890u
static const unsigned char kPriceSig[] = {
    0x83, 0xEC, 0x20,                    // sub esp, 20h
    0xB8, 0x50, 0x71, 0x54, 0x00,        // mov eax, 547150h
    0x53, 0x8B, 0x10, 0x56               // push ebx / mov edx,[eax] / push esi
};

static void* PriceFn(void)
{
    unsigned char* p;
    unsigned i;
    if (!Base()) return NULL;
    p = g_base + MKT_PRICEFN_RVA;
    if (!Readable(p, sizeof(kPriceSig))) return NULL;
    for (i = 0; i < sizeof(kPriceSig); i++) if (p[i] != kPriceSig[i]) return NULL;
    return p;
}

// 그 도시에서 그 교역품의 기준가. 게임이 셈해 준 값 그대로. 못 부르면 -1.
int Mkt_GamePrice(int city, int kind)
{
    static unsigned char self[0x100];
    void* fn = PriceFn();
    int n = 0, r = 0, i;

    if (!fn || city < 0 || city >= MKT_CITY_N || kind < 0 || kind >= MKT_GOODS_N) return -1;

    ZeroMemory(self, sizeof(self));
    *(int*)(self + 0x90) = city;                      // 지금 서 있는 도시
    for (i = 0; i < 2; i++) {                         // 연결 내륙도시(세비야->톨레도 같은 것)
        const int* q = (const int*)(g_base + MKT_REC_RVA + (unsigned)city * MKT_REC_SZ
                                    + MKT_REC_INLAND + i * 4);
        if (!Readable(q, 4)) break;
        if (*q >= 0 && *q < MKT_CITY_N) *(int*)(self + 0xB0 + n++ * 4) = *q;
    }
    *(int*)(self + 0xB8) = n;

    {   // 부르는 규약이 ret 8 인지 cdecl 인지 눈으로 못 봤으므로 esp 를 손수 되돌린다.
        void* pself = self;
        __asm {
            mov  esi, esp
            push -1
            push kind
            mov  ecx, pself
            call fn
            mov  esp, esi
            mov  r, eax
        }
    }
    return (r > 0 && r < 1000000) ? r : -1;
}

// 판매 게이트. 0 이면 그 교역품은 어디서도 안 판다(미발견 지역 물품 등).
static int Sellable(int kind)
{
    const int* p;
    if (!Base() || kind < 0 || kind >= MKT_GOODS_N) return 0;
    p = (const int*)(g_base + MKT_GATE_RVA + (unsigned)kind * 4);
    return Readable(p, 4) && *p != 0;
}

// 지역 공통 교역품 n번째(0~4). 없으면 -1.
static int CommonGood(int region, int n)
{
    const int* p;
    if (region < 0 || n < 0 || n >= 5 || !Base()) return -1;
    p = (const int*)(g_base + MKT_COMMON_RVA + (unsigned)region * 20 + (unsigned)n * 4);
    if (!Readable(p, 4)) return -1;
    return (*p >= 0 && *p < MKT_GOODS_N) ? *p : -1;
}

// 지금 정박한 도시. 게임 객체(0x5B60A0)의 가상함수로 묻는다 — 0x477EB0 이 하는 그대로다.
// 항해 중이면 -1 을 돌려준다.
int Mkt_CurrentCity(void)
{
    // __thiscall 은 C 에서 못 쓰므로 __fastcall 로 부른다 — 첫 인자가 ecx 로 들어가는 것은
    // 같고, 받는 쪽은 인자가 없어 edx 를 안 본다(스택도 안 쓰므로 균형이 맞는다).
    typedef int (__fastcall *GetCityFn)(void* ecx, void* edx);
    unsigned char* self;
    void** vt;
    GetCityFn fn;
    int id;

    if (!Base()) return -1;
    self = g_base + 0x1B60A0u;
    if (!Readable(self, 4)) return -1;
    vt = *(void***)self;
    if (!Readable(vt, 0x30)) return -1;
    fn = (GetCityFn)vt[0x2C / 4];
    if (!Readable((void*)fn, 1)) return -1;
    id = fn(self, 0);
    return (id >= 0 && id < MKT_CITY_N) ? id : -1;
}

int Mkt_Money(void)
{
    int* p = Base() ? (int*)(g_base + MKT_MONEY_RVA) : NULL;
    return (p && Readable(p, 4)) ? *p : -1;
}

// 그 도시가 파는 것 — 게임 빌더(0x480CC0)의 세 갈래 중 둘을 그대로 옮겼다.
//   1) 지역 공통품 5칸 (재고는 도시 struct +0x44~0x54, 단가는 지역 기준가 표)
//   2) 그 도시 특산품 (재고 +0x18, 단가는 그 도시 +0x14)
//   3) 연결 내륙도시의 특산품 — 그 목록은 도시 record +0x10 · +0x14 에 있다
//      (세비야->톨레도, 카디스->코르도바. 게임의 "톨레도산 대포" 가 이것이다)
static void AddSpecialty(int from, int to);

int Mkt_BuildList(int city)
{
    int region = Region(city), n;
    const int* f;

    g_rowN = 0;
    if (!Base() || region < 0) return 0;

    for (n = 0; n < 5; n++) {
        int kind = CommonGood(region, n), stock;
        if (kind < 0) break;
        if (!Sellable(kind)) continue;
        f = CityField(city, 0x44 + n * 4);
        stock = f ? *f : 0;
        if (stock < 0) stock = 0;      // 재고 0 도 게임 구입창처럼 줄은 보여 준다
        g_row[g_rowN].kind   = kind;
        g_row[g_rowN].price  = Mkt_BuyPriceAt(city, kind);
        g_row[g_rowN].supply = stock;
        g_row[g_rowN].origin = city;
        g_row[g_rowN].slot   = n;
        g_rowN++;
    }

    AddSpecialty(city, city);          // 이 도시 특산품

    for (n = 0; n < 2; n++) {          // 연결 내륙도시 특산품(수입품)
        const int* q = (const int*)(g_base + MKT_REC_RVA + (unsigned)city * MKT_REC_SZ
                                    + MKT_REC_INLAND + n * 4);
        if (!Readable(q, 4)) break;
        if (*q >= 0 && *q < MKT_CITY_N) AddSpecialty(*q, city);
    }
    return g_rowN;
}

// 도시 from 의 특산품을 목록에 얹는다(원산지 = from). to 는 지금 서 있는 도시.
static void AddSpecialty(int from, int to)
{
    const int* f = CityField(from, 0x10);
    int spec = f ? *f : -1;
    int i;
    if (spec < 0 || spec >= MKT_GOODS_N || !Sellable(spec)) return;
    if (g_rowN >= (int)(sizeof(g_row)/sizeof(g_row[0]))) return;
    {
        const int* ps = CityField(from, 0x18);
        int stock = ps ? *ps : 0;
        if (stock < 0) stock = 0;      // 위와 같다 — 0 개도 줄은 나온다
        // 이미 공통품으로 들어와 있으면 게임도 그쪽을 뺀다(중복 제외).
        for (i = 0; i < g_rowN; i++) if (g_row[i].kind == spec) return;
        g_row[g_rowN].kind   = spec;
        // 값은 "지금 서 있는 도시" 기준으로 셈한다 — 게임 함수가 수입품(연결 내륙도시
        // 특산품)까지 그 안에서 알아서 처리한다.
        g_row[g_rowN].price  = Mkt_BuyPriceAt(to, spec);
        g_row[g_rowN].supply = stock;
        g_row[g_rowN].origin = from;
        g_row[g_rowN].slot   = -1;
        g_rowN++;
    }
}

const MktRow* Mkt_At(int i) { return (i >= 0 && i < g_rowN) ? &g_row[i] : NULL; }

int Mkt_LoadCargo(void)
{
    int slot;
    g_cargoN = 0;
    if (!Base()) return 0;
    // 짐은 함대가 통째로 쓰는 여덟 칸뿐이다(marketdb.h 참고). 배마다 여덟 칸이 아니다 —
    // 그렇게 읽으면 뒤에 붙은 남의 배열을 짐으로 착각해 없는 물건이 목록에 뜬다.
    for (slot = 0; slot < MKT_CARGO_SLOTS && g_cargoN < MKT_CARGO_MAX; slot++) {
        const int* p = (const int*)(g_base + MKT_CARGO_RVA + (unsigned)slot * 16);
        if (!Readable(p, 16)) break;
        // 빈 칸 판정은 게임 규칙 그대로다(0x4B5730) — 종류 != -1 이고 갯수 > 0.
        // 원산지도 넷째 칸도 게임은 안 본다. 그래서 여기서도 그것으로 줄을 버리지 않는다.
        // ("넷째 칸이 내구도라 0~100" 은 틀린 가정이었다 — 해적에게서 뺏은 트리폴리산
        //  장신구 88개가 {63, 88, 81, 210} 이라 그 검사에 걸려 통째로 안 보였다.)
        if (p[0] < 0 || p[0] >= MKT_GOODS_N) continue;
        if (p[1] <= 0 || p[1] > 9999) continue;
        g_cargo[g_cargoN].kind   = p[0];
        g_cargo[g_cargoN].count  = p[1];
        g_cargo[g_cargoN].origin = (p[2] >= 0 && p[2] < MKT_CITY_N) ? p[2] : -1;
        g_cargo[g_cargoN].cond   = p[3];
        g_cargo[g_cargoN].slot   = slot;
        g_cargoN++;
    }
    return g_cargoN;
}

// 함대 i 번 칸에 든 배. 없으면 NULL.
// 게임은 "가진 배" 를 훑지 않는다 — 함대 객체가 들고 있는 여덟 칸짜리 배 번호 목록만 본다
// (0x473DB0 / 0x473DC0). 짐칸 한도도 그 목록으로 센다(0x4743F0 · 0x4744B0).
static const unsigned char* FleetShip(int i)
{
    const int* slot;
    const unsigned char* s;
    int id;
    if (!Base() || i < 0 || i >= MKT_FLEET_SHIPS) return NULL;
    slot = (const int*)(g_base + MKT_FLEETLIST_RVA + (unsigned)i * 4);
    if (!Readable(slot, 4)) return NULL;
    id = *slot;
    if (id < 0 || id >= MKT_SHIP_N) return NULL;          // -1 이면 빈 칸
    s = g_base + MKT_SHIP_RVA + (unsigned)id * MKT_SHIP_SZ;
    return Readable(s, MKT_SHIP_SZ) ? s : NULL;
}

// 실은 대포가 먹는 중량. 게임 0x44C980 그대로 —
// 대포종류가 -1 이면 0, 아니면 현재 대포수 x 그 종류의 한 문 무게다.
static int CannonMass(const unsigned char* s)
{
    const int* rec;
    int kind = *(const int*)(s + MKT_SHIP_GUNTYPE);
    int cnt  = *(const int*)(s + MKT_SHIP_GUNNOW);
    if (kind < 0 || kind >= MKT_GUN_N || cnt <= 0) return 0;
    rec = (const int*)(g_base + MKT_GUN_RVA + (unsigned)kind * MKT_GUN_REC);
    if (!Readable(rec, MKT_GUN_REC)) return 0;
    if (rec[2] < 0 || rec[2] > 9999) return 0;
    return cnt * rec[2];
}

int Mkt_Hold(int* ships, int* mass, int* cap)
{
    int i, sm = 0, sc = 0, n = 0;
    if (ships) *ships = 0;
    if (mass)  *mass = 0;
    if (cap)   *cap = 0;
    if (!Base()) return 0;
    for (i = 0; i < MKT_FLEET_SHIPS; i++) {
        const unsigned char* s = FleetShip(i);
        int m, c;
        if (!s) continue;
        m = *(const int*)(s + MKT_SHIP_MASS);
        c = *(const int*)(s + MKT_SHIP_CAP);
        if (m < 0 || m > 99999 || c < 0 || c > 99999) continue;
        // 대포가 먹는 몫을 뺀다 — 게임도 그렇게 센다(0x44C8B0 중량 · 0x44C910 용량).
        m -= CannonMass(s);
        c -= *(const int*)(s + MKT_SHIP_GUNMX);
        if (m < 0) m = 0;
        if (c < 0) c = 0;
        sm += m; sc += c; n++;
    }
    if (!n) return 0;
    if (ships) *ships = n;
    if (mass)  *mass = sm;
    if (cap)   *cap = sc;
    return 1;
}

// 물·식량·자재·포탄이 먹은 몫. 날값은 함대칸 +0x0C 에 한 벌뿐이고,
// 중량과 용량은 거기서 배수를 달리해 셈한다 — 게임 0x474330(중량) · 0x474430(용량) 그대로다.
// 네 칸을 그냥 더하던 예전 셈은 틀렸다(marketdb.h 참고).
static int Supply(int mass)
{
    const int* p;
    int i;
    if (!Base()) return -1;
    p = (const int*)(g_base + MKT_FLEET_RVA + MKT_FLEET_SUPPLY);
    if (!Readable(p, 16)) return -1;
    for (i = 0; i < 4; i++)
        if (p[i] < 0 || p[i] > 999999) return -1;
    if (mass)
        return (p[0] + 9) / 10 * 10 + (p[1] + 9) / 10 * 5 + p[2] * 5 + p[3] * 20;
    return (p[0] + 9) / 10 + (p[1] + 9) / 10 + p[2] + p[3];
}
int Mkt_SupplyMass(void)   { return Supply(1); }
int Mkt_SupplyVolume(void) { return Supply(0); }

int Mkt_CitySise(int city)
{
    const int* p = CityField(city, 0x0C);
    return (p && *p > 0 && *p < 10000) ? *p : -1;
}
int Mkt_CityState(int city)
{
    const int* p = CityField(city, 0x40);
    return p ? *p : -1;
}
int Mkt_CitySpecial(int city)
{
    const int* p = CityField(city, 0x10);
    return (p && *p >= 0 && *p < MKT_GOODS_N) ? *p : -1;
}

void Mkt_CargoRaw(int slot, int out[4])
{
    const int* p;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (!Base() || slot < 0 || slot >= MKT_CARGO_N) return;
    p = (const int*)(g_base + MKT_CARGO_RVA + (unsigned)slot * 16);
    if (!Readable(p, 16)) return;
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
}

const MktCargo* Mkt_CargoAt(int i) { return (i >= 0 && i < g_cargoN) ? &g_cargo[i] : NULL; }

// 같은 교역품 + 같은 원산지 칸을 찾고, 없으면 빈 칸을 찾는다. 없으면 -1(짐칸이 꽉 찼다).
static int CargoSlotFor(int kind, int origin)
{
    int slot, empty = -1;
    for (slot = 0; slot < MKT_CARGO_SLOTS; slot++) {
        int* p = (int*)(g_base + MKT_CARGO_RVA + (unsigned)slot * 16);
        if (!Readable(p, 16)) break;
        if (p[0] == kind && p[2] == origin && p[1] > 0 && p[1] <= 9999) return slot;
        if (empty < 0 && (p[1] <= 0 || p[0] < 0 || p[0] >= MKT_GOODS_N)) empty = slot;
    }
    return empty;
}

int Mkt_Buy(int city, int row, int qty)
{
    const MktRow* r = Mkt_At(row);
    int cost, *money, *stock, slot;
    int* c;

    if (!Base() || !r || qty <= 0) return MKT_E_READ;
    if (qty > r->supply) return MKT_E_SUPPLY;
    if (r->price <= 0) return MKT_E_READ;

    cost = r->price * qty;
    money = (int*)(g_base + MKT_MONEY_RVA);
    if (!Writable(money, 4)) return MKT_E_READ;
    if (*money < cost) return MKT_E_MONEY;

    // ★ 재고는 "그 물건을 대는 도시" 에서 깎아야 한다. 수입품(연결 내륙도시 특산품)은
    //    지금 서 있는 도시가 아니라 원산지 것이다 — 톨레도산 대포를 세비야에서 사면
    //    줄어드는 것은 톨레도의 특산품 재고다. r->origin 이 곧 그 도시다
    //    (공통품과 이 도시 특산품은 origin 이 이 도시라 예전과 같다).
    stock = (r->slot >= 0) ? CityField(r->origin, 0x44 + r->slot * 4)
                           : CityField(r->origin, 0x18);
    (void)city;
    if (!stock || !Writable(stock, 4)) return MKT_E_READ;

    slot = CargoSlotFor(r->kind, r->origin);
    if (slot < 0) return MKT_E_FULL;
    c = (int*)(g_base + MKT_CARGO_RVA + (unsigned)slot * 16);
    if (!Writable(c, 16)) return MKT_E_READ;

    // 여기서부터 실제로 쓴다 — 앞의 검사에서 다 걸러 냈으므로 중간에 실패하지 않는다.
    if (c[0] == r->kind && c[2] == r->origin && c[1] > 0) {
        c[1] += qty;
    } else {
        c[0] = r->kind; c[1] = qty; c[2] = r->origin; c[3] = 100;
    }
    *stock -= qty;
    *money -= cost;
    return cost;
}

// 구입 단가 = 매각가의 3/2. 시세를 여기서 곱하면 안 된다 — 아래를 보라.
int Mkt_BuyPriceAt(int city, int kind)
{
    return UnitPrice(Mkt_SellPrice(city, kind));
}

// 매각가 = 게임 함수 0x480890 이 돌려주는 값 그대로다. 시세는 그 안에 이미 들어 있다.
//   게임 매각창으로 확인: 리스본 시세 127 · 대포 기준가 155 -> 155x1.27 = 196,
//   게임 매각창이 그대로 196 이었다. 같은 순간 게임 구입창은 196x3/2 = 294 였다.
//   즉 매각가 = 0x480890 값, 구입 단가 = 그 3/2 — 양쪽 다 게임 숫자와 맞췄다.
//   ★ 한동안 여기에 "x 시세/100" 을 한 번 더 곱하고 있었다. 시세가 100 언저리일 때는
//     티가 안 나다가 시세가 오르면 값이 통째로 부풀었다. 리스본 시세 130 에서 잡았다:
//       품목    기준가   0x480890   게임 구입창   덧곱하던 값
//       돌소금    22       28          42           54
//       올리브유  18       23          34           43
//       총        80      104         156          202
//       대포     155      201         301          391
//     0x480890 결과가 기준가의 정확히 1.30 배다 — 시세가 이미 반영돼 있다는 뜻이다.
//     그리고 게임 구입창 = 그 값의 3/2 다. 네 품목 모두 딱 맞는다.
//   0x480890 은 지역 기준가에서 시작해 도시 특산가로 낮추고, 품목 분류(향신료 · 카카오 ·
//   커피 · 담배 · 차 …)와 그 항로를 발견한 뒤 지난 날수, 그리고 시세까지 반영한다.
int Mkt_SellPrice(int city, int kind)
{
    int g = Mkt_GamePrice(city, kind);
    if (g > 0) return g;
    {   // 게임 함수를 못 부를 때의 어림값 — 이쪽은 시세를 손수 곱해 준다.
        const int* sise = CityField(city, 0x0C);
        int s = (sise && *sise > 0) ? *sise : 100;
        int b = BasePrice(Region(city), kind);
        return b > 0 ? b * s / 100 : -1;
    }
}

// 짐 칸에서 덜어 내고 돈을 받는다. 다 팔면 그 칸을 지우고 뒤를 당겨 붙인다
// (짐은 앞에서부터 채워지므로 구멍을 남기지 않는다).
int Mkt_Sell(int city, int idx, int qty)
{
    const MktCargo* c = Mkt_CargoAt(idx);
    int price, gain, *money, *p, i;

    if (!Base() || !c || qty <= 0) return MKT_E_READ;
    if (qty > c->count) return MKT_E_SUPPLY;
    price = Mkt_SellPrice(city, c->kind);
    if (price <= 0) return MKT_E_READ;

    money = (int*)(g_base + MKT_MONEY_RVA);
    p = (int*)(g_base + MKT_CARGO_RVA + (unsigned)c->slot * 16);
    if (!Writable(money, 4) || !Writable(p, 16)) return MKT_E_READ;
    if (p[0] != c->kind || p[1] < qty) return MKT_E_READ;      // 그 사이 바뀌었으면 그만둔다

    gain = price * qty;
    p[1] -= qty;
    if (p[1] <= 0) {
        // 다 팔았으면 뒤를 당겨 붙인다 — 짐 칸은 함대 통틀어 여덟이다.
        int last = MKT_CARGO_SLOTS - 1;
        for (i = c->slot; i < last; i++) {
            int* a = (int*)(g_base + MKT_CARGO_RVA + (unsigned)i * 16);
            int* b = a + 4;
            if (!Writable(a, 16) || !Readable(b, 16)) break;
            a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; a[3] = b[3];
            if (b[0] < 0 || b[0] >= MKT_GOODS_N || b[1] <= 0) break;
        }
        {   // 마지막 칸은 비워 둔다 — 안 그러면 끝 칸이 두 번 남는다.
            int* z = (int*)(g_base + MKT_CARGO_RVA + (unsigned)last * 16);
            if (Writable(z, 16)) { z[0] = -1; z[1] = 0; z[2] = -1; z[3] = 0; }
        }
    }
    *money += gain;
    return gain;
}

const wchar_t* Mkt_GoodsName(int kind)
{
    return (kind >= 0 && kind < MKT_GOODS_N) ? kTradeGoods[kind] : L"?";
}
const wchar_t* Mkt_CityName(int city)
{
    return (city >= 0 && city < MKT_CITY_N) ? kCities[city].name : L"?";
}
int Mkt_GoodsPic(int kind)
{
    return (kind >= 0 && kind < MKT_GOODS_N) ? GOOD_PIC_BASE + kind : -1;
}

// 한 개 무게 — 교역품 레코드 +0x70. 게임 구입창의 "중량" 칸과 같은 값이다.
int Mkt_GoodsMass(int kind)
{
    const int* p;
    if (!Base() || kind < 0 || kind >= MKT_GOODS_N) return -1;
    p = (const int*)(g_base + MKT_PRICE_RVA + (unsigned)kind * MKT_GOODS_REC + MKT_GOODS_MASS);
    if (!Readable(p, 4)) return -1;
    return (*p > 0 && *p < 10000) ? *p : -1;
}
