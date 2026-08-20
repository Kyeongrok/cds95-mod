#include "disc.h"
#include "discinst.h"     // 앞 107개는 인스턴스에 "내가 발견/보고" 가 그대로 남는다
#include "hintdb.h"       // 인스턴스가 없는 줄만 힌트 배열로 가른다
#include "disc_hint.h"    // kDiscHint[274] — 발견물 -> 힌트 이음표

// 자리는 disc.h 에, 이음표를 만든 법은 disc_hint.h 에 적어 뒀다. 여기서는 읽기만 한다.

static int  g_ready = 0;
static int  g_links = 0;
static wchar_t g_name[DISC_N][40];
static int  g_cat[DISC_N];
static int  g_value[DISC_N];

static int Commit(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

static void Cp949(const char* s, wchar_t* out, int cch)
{
    out[0] = 0;
    if (!s || !Commit(s, 1)) return;
    MultiByteToWideChar(949, 0, s, -1, out, cch);
}

int Disc_Load(void)
{
    unsigned char* base;
    const unsigned char* rec;
    int i, ok = 0;

    if (g_ready) return 1;
    DInst_Load();
    base = (unsigned char*)GetModuleHandleW(NULL);
    if (!base) return 0;
    rec = base + DISC_RVA;
    if (!Commit(rec, DISC_N * DISC_SZ)) return 0;

    for (i = 0; i < DISC_N; i++) {
        const unsigned char* r = rec + i * DISC_SZ;
        Cp949(*(const char* const*)(r + 0x00), g_name[i], 40);
        g_cat[i]   = *(const int*)(r + 0x04);
        g_value[i] = *(const int*)(r + 0x18);
        if (g_cat[i] < 0 || g_cat[i] > 7) g_cat[i] = -1;
        if (g_name[i][0]) ok++;
        if (kDiscHint[i] >= 0) g_links++;
    }
    if (ok < DISC_N / 2) return 0;       // 대부분이 이름을 못 내면 표 자리가 어긋난 것이다
    g_ready = 1;
    return 1;
}

int Disc_Ready(void)     { return g_ready; }
int Disc_Live(void)      { return HintDb_Live(); }
int Disc_Count(void)     { return DISC_N; }
int Disc_LinkCount(void) { return g_links; }

const wchar_t* Disc_Name(int i)
{
    return (g_ready && i >= 0 && i < DISC_N && g_name[i][0]) ? g_name[i] : L"?";
}
int Disc_Cat(int i)   { return (g_ready && i >= 0 && i < DISC_N) ? g_cat[i] : -1; }
int Disc_Value(int i) { return (g_ready && i >= 0 && i < DISC_N) ? g_value[i] : -1; }

int Disc_HintId(int i)
{
    return (i >= 0 && i < DISC_N) ? kDiscHint[i] : -1;
}

int Disc_Inst(int i)  { return DInst_OfDisc(i); }

int Disc_Taken(int i)
{
    int inst = DInst_OfDisc(i);
    if (inst < 0 || !DInst_Live()) return -1;
    return DInst_Filled(inst, DINST_OTHER) ? 1 : 0;
}

// 인스턴스가 있는 줄은 사람 칸으로 가른다 — 발견한 그 순간에 0번 칸이 채워지고,
// 후원자에게 보고하면 2번 칸이 채워진다. 힌트 배열의 발견 비트는 보고까지 마쳐야
// 켜지므로(discinst.h 머리 참고) 여기서 앞세우지 않는다.
//
// 인스턴스가 없는 줄(교역품·비보 따위)만 힌트 상태의 비트로 가른다.
// 8=1000 아직 / 13=1101 힌트 취득 / 15=1111 발견 완료 이고, 11=1011 과 7=0111 도 나온다
// — bit1 이 곧 "찾았다"(=보고까지 했다) 다. bit3 은 "힌트가 있는 줄" 이라 안 쓴다.
int Disc_Found(int i)
{
    int h, st, inst;
    if (i < 0 || i >= DISC_N) return DISC_UNKNOWN;

    inst = DInst_OfDisc(i);
    if (inst >= 0) {
        if (!DInst_Live()) return DISC_UNKNOWN;
        if (DInst_Filled(inst, DINST_REPORT)) return DISC_REPORTED;
        if (DInst_Filled(inst, DINST_ME))     return DISC_FOUND;
        h = kDiscHint[i];
        st = (h >= 0) ? HintDb_State(h) : -1;
        return (st > 0 && (st & 1)) ? DISC_HINTED : DISC_NOT;
    }

    h = kDiscHint[i];
    if (h < 0) return DISC_NOLINK;
    st = HintDb_State(h);
    if (st < 0) return DISC_UNKNOWN;
    if (st & 2) return DISC_REPORTED;
    if (st & 1) return DISC_HINTED;
    return DISC_NOT;
}
