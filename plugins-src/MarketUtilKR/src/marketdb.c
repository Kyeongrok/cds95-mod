#include "marketdb.h"
#include "goods_names.h"   // TradeUtilKR/src — kTradeGoods[70]
#include "cities_data.h"   // TradeUtilKR/src — kCities[226]

// 자리와 규칙은 marketdb.h 에 적어 뒀다.

#define GOOD_PIC_BASE 134  // 교역품 그림 = ITEM.CDS 134 + 종류 (TradeUtilKR 과 같다)

static unsigned char* g_base = NULL;
static MktRow   g_row[16];
static int      g_rowN = 0;
static MktCargo g_cargo[MKT_CARGO_N];
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
        if (stock <= 0) continue;
        g_row[g_rowN].kind   = kind;
        g_row[g_rowN].price  = UnitPrice(BasePrice(region, kind));
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
    (void)to;
    if (spec < 0 || spec >= MKT_GOODS_N || !Sellable(spec)) return;
    if (g_rowN >= (int)(sizeof(g_row)/sizeof(g_row[0]))) return;
    {
        const int* pv = CityField(from, 0x14);
        const int* ps = CityField(from, 0x18);
        int stock = ps ? *ps : 0;
        if (stock <= 0) return;
        // 이미 공통품으로 들어와 있으면 게임도 그쪽을 뺀다(중복 제외).
        for (i = 0; i < g_rowN; i++) if (g_row[i].kind == spec) return;
        g_row[g_rowN].kind   = spec;
        g_row[g_rowN].price  = UnitPrice(pv ? *pv : -1);
        g_row[g_rowN].supply = stock;
        g_row[g_rowN].origin = from;
        g_row[g_rowN].slot   = -1;
        g_rowN++;
    }
}

const MktRow* Mkt_At(int i) { return (i >= 0 && i < g_rowN) ? &g_row[i] : NULL; }

int Mkt_LoadCargo(void)
{
    int i;
    g_cargoN = 0;
    if (!Base()) return 0;
    // 종류 · 갯수 · 원산지 셋만 본다. 넷째 칸(내구도로 적힌 자리)은 뜻이 확실치 않아 안 본다.
    // 빈 칸은 건너뛰되, 말이 안 되는 칸이 잇달아 셋 나오면 거기서 끝으로 본다
    // (끝을 안 지키면 뒤쪽 딴 자료가 "밀 5022432개" 처럼 딸려 나온다).
    { int miss = 0;
      for (i = 0; i < MKT_CARGO_N && miss < 3; i++) {
        const int* p = (const int*)(g_base + MKT_CARGO_RVA + (unsigned)i * 16);
        if (!Readable(p, 16)) break;
        if (p[0] < 0 || p[0] >= MKT_GOODS_N ||
            p[1] <= 0 || p[1] > 9999 ||
            p[2] < 0 || p[2] >= MKT_CITY_N) { miss++; continue; }
        miss = 0;
        g_cargo[g_cargoN].kind   = p[0];
        g_cargo[g_cargoN].count  = p[1];
        g_cargo[g_cargoN].origin = p[2];
        g_cargo[g_cargoN].cond   = p[3];
        g_cargo[g_cargoN].slot   = i;
        g_cargoN++;
      } }
    return g_cargoN;
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

// 같은 교역품 + 같은 원산지 칸을 찾고, 없으면 빈 칸을 찾는다. 없으면 -1.
static int CargoSlotFor(int kind, int origin)
{
    int i, empty = -1;
    for (i = 0; i < MKT_CARGO_N; i++) {
        int* p = (int*)(g_base + MKT_CARGO_RVA + (unsigned)i * 16);
        if (!Readable(p, 16)) break;
        if (p[0] == kind && p[2] == origin && p[1] > 0 && p[1] <= 9999) return i;
        if (empty < 0 && (p[1] <= 0 || p[0] < 0 || p[0] >= MKT_GOODS_N)) empty = i;
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

// 그 도시의 특산품이면 그 도시 기준가, 아니면 지역 기준가. 둘 다 1.5배가 사는 값이다.
int Mkt_BuyPriceAt(int city, int kind)
{
    const int* spec = CityField(city, 0x10);
    if (spec && *spec == kind) {
        const int* pv = CityField(city, 0x14);
        return UnitPrice(pv ? *pv : -1);
    }
    return UnitPrice(BasePrice(Region(city), kind));
}

int Mkt_SellPrice(int city, int kind)
{
    int base = BasePrice(Region(city), kind);
    const int* sise = CityField(city, 0x0C);
    int s = (sise && *sise > 0) ? *sise : 100;
    if (base <= 0) return -1;
    return base * s / 100;
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
        for (i = c->slot; i < MKT_CARGO_N - 1; i++) {
            int* a = (int*)(g_base + MKT_CARGO_RVA + (unsigned)i * 16);
            int* b = a + 4;
            if (!Writable(a, 16) || !Readable(b, 16)) break;
            a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; a[3] = b[3];
            if (b[0] < 0 || b[0] >= MKT_GOODS_N || b[1] <= 0) break;
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
