#include "landwar.h"
#include <MinHook.h>

#define IMAGE_BASE 0x00400000u        // 표 안의 포인터는 이 바탕으로 박혀 있다

static BYTE* g_base = NULL;
static int   g_presets[LW_MINE_N];
static int   g_allowBig = 0;

static wchar_t g_name[LW_TYPE_N][32];
static wchar_t g_desc[LW_TYPE_N][256];
static int     g_tblOk = 0;

typedef void (__fastcall *LoadSprFn)(void* self, void* edx);
typedef int  (__fastcall *StatFn)(void* self, void* edx, int kind, int slot);
static LoadSprFn g_origLoad = NULL;

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

// 표에 박힌 절대 주소(바탕 0x400000)를 지금 모듈 자리로 옮긴다.
static BYTE* Fix(unsigned va)
{
    if (va < IMAGE_BASE) return NULL;
    return g_base + (va - IMAGE_BASE);
}

static BYTE* Unit(int slot)
{
    if (!g_base || slot < 0 || slot >= LW_UNIT_N) return NULL;
    return g_base + LW_UNITS_RVA + (unsigned)slot * LW_UNIT_SZ;
}

int LandWar_Ready(void) { return g_base != NULL; }

// CP949 로 박힌 이름·설명을 한 번만 옮겨 담는다.
static void LoadTables(void)
{
    const unsigned* nameTbl = (const unsigned*)(g_base + LW_NAME_RVA);
    const unsigned* descTbl = (const unsigned*)(g_base + LW_DESC_RVA);
    int t;

    if (g_tblOk) return;
    if (!Readable(nameTbl, LW_TYPE_N * 4) || !Readable(descTbl, LW_TYPE_N * 4)) return;

    for (t = 0; t < LW_TYPE_N; t++) {
        const char* n = (const char*)Fix(nameTbl[t]);
        const char* d = (const char*)Fix(descTbl[t]);
        g_name[t][0] = 0; g_desc[t][0] = 0;
        if (n && Readable(n, 2)) MultiByteToWideChar(949, 0, n, -1, g_name[t], 32);
        if (d && Readable(d, 2)) MultiByteToWideChar(949, 0, d, -1, g_desc[t], 256);
    }
    g_tblOk = 1;
}

int LandWar_Load(void)
{
    int i;
    if (g_base) return 1;
    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) return 0;
    if (!Readable(g_base + LW_UNITS_RVA, LW_UNIT_N * LW_UNIT_SZ)) { g_base = NULL; return 0; }
    for (i = 0; i < LW_MINE_N; i++) g_presets[i] = -1;
    LoadTables();
    LogW(L"[LandWarKR] 부대 배열 0x%08X, 병종표 %s",
         (unsigned)(UINT_PTR)(g_base + LW_UNITS_RVA), g_tblOk ? L"OK" : L"실패");
    return 1;
}

const wchar_t* LandWar_TypeName(int t)
{
    if (!g_tblOk || t < 0 || t >= LW_TYPE_N || !g_name[t][0]) return L"?";
    return g_name[t];
}
const wchar_t* LandWar_TypeDesc(int t)
{
    if (!g_tblOk || t < 0 || t >= LW_TYPE_N) return L"";
    return g_desc[t];
}

int LandWar_Type(int slot)
{
    BYTE* u = Unit(slot);
    if (!u) return -1;
    return *(const int*)(u + LW_TYPE_OFF);
}

int LandWar_Word(int slot, int idx)
{
    BYTE* u = Unit(slot);
    if (!u || idx < 0 || idx >= LW_UNIT_SZ / 4) return 0;
    return *(const int*)(u + idx * 4);
}

// 전투가 서 있나.
// 처음에는 "레코드가 전부 0 이면 전투 밖"으로 봤는데 틀렸다 — 전투 밖에서도 열두 칸의
// `+0x1C` 에 1 이 남아 있어서 늘 "전투 중"으로 나왔다. 그 바람에 전투 밖에서 게임의
// 능력치 함수를 불러 게임이 즉사했다. 그래서 **앞쪽 여섯 칸만** 본다
// (실측: 전투 밖에서는 0~5 가 전부 0 이다).
int LandWar_Active(void)
{
    int i, j;
    if (!g_base) return 0;
    for (i = 0; i < LW_UNIT_N; i++)
        for (j = 0; j < 6; j++)
            if (LandWar_Word(i, j) != 0) return 1;
    return 0;
}

// ※ 게임 함수를 그대로 부른다. **전투 밖에서 부르면 게임이 죽는다** — 객체가 안 서 있다.
// 지금은 창에서 안 쓴다(레코드를 그냥 읽어 보여 준다). 남겨는 두되 함부로 부르지 말 것.
int LandWar_Stat(int slot, int kind)
{
    StatFn fn;
    if (!g_base || slot < 0 || slot >= LW_UNIT_N) return -1;
    if (!LandWar_Active()) return -1;
    fn = (StatFn)(g_base + LW_STAT_RVA);
    return fn(g_base + LW_OBJ_RVA, NULL, kind, slot);
}

void LandWar_AllowBig(int on) { g_allowBig = on ? 1 : 0; }
int  LandWar_BigAllowed(void) { return g_allowBig; }

// 아군 칸(0~5)은 그림 자리가 36,864바이트뿐이라 큰 그림 병종을 넣으면 옆 칸이 깨진다.
int LandWar_TypeOkForSlot(int slot, int t)
{
    if (t < 0 || t >= LW_TYPE_N) return 0;
    if (slot >= LW_MINE_N) return 1;                 // 적 칸은 65,536 자리라 다 된다
    if (g_allowBig) return 1;
    return !(t == LW_BIG_A || t == LW_BIG_B);
}

int LandWar_SetType(int slot, int t)
{
    BYTE* u = Unit(slot);
    if (!u || !LandWar_TypeOkForSlot(slot, t)) return 0;
    *(int*)(u + LW_TYPE_OFF) = t;
    LogW(L"[LandWarKR] %d칸 병종 -> %d(%s)", slot, t, LandWar_TypeName(t));
    return 1;
}

void LandWar_SetPreset(int slot, int t)
{
    if (slot < 0 || slot >= LW_MINE_N) return;
    g_presets[slot] = (t >= 0 && t < LW_TYPE_N) ? t : -1;
    // 예약을 처음 걸 때만 훅을 건다 — 안 쓰는 사람에게는 코드를 아예 안 건드린다.
    if (g_presets[slot] >= 0) LandWar_HookInstall();
}
int  LandWar_Preset(int slot)
{
    return (slot >= 0 && slot < LW_MINE_N) ? g_presets[slot] : -1;
}
void LandWar_ClearPresets(void)
{
    int i;
    for (i = 0; i < LW_MINE_N; i++) g_presets[i] = -1;
}

// ---------------------------------------------------------------- 훅
// 0x0044A0D0 은 전투 시작 때 12칸을 돌며 `+0x04`(병종)로 그림을 읽는다.
// 그 직전에 예약한 병종을 눌러 써야 그림까지 그 병종으로 읽힌다.

static void __fastcall DetourLoadSpr(void* self, void* edx)
{
    int i, n = 0;
    for (i = 0; i < LW_MINE_N; i++) {
        int t = g_presets[i];
        if (t < 0) continue;
        if (!LandWar_TypeOkForSlot(i, t)) continue;
        {
            BYTE* u = Unit(i);
            if (!u) continue;
            *(int*)(u + LW_TYPE_OFF) = t;
            n++;
        }
    }
    if (n) LogW(L"[LandWarKR] 전투 시작 — 아군 %d칸을 예약한 병종으로 눌러 썼다", n);
    g_origLoad(self, edx);
}

// 0x0044A0D0 은 `mov eax, fs:[0]` (64 A1 00 00 00 00) 로 시작하는 SEH 함수다.
// 훅을 걸기 전에 그 여섯 바이트를 확인한다 — 주소를 한 자리라도 잘못 적으면
// 엉뚱한 코드 한복판을 덮어써서 게임이 그 자리에서 죽는다(실제로 한 번 그랬다).
static const unsigned char kLoadSprHead[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };

int LandWar_HookInstall(void)
{
    void* target;
    MH_STATUS st;

    if (!LandWar_Load()) return 0;
    if (g_origLoad) return 1;

    target = (void*)(g_base + LW_LOADSPR_RVA);
    if (!Readable(target, sizeof(kLoadSprHead)) ||
        memcmp(target, kLoadSprHead, sizeof(kLoadSprHead)) != 0) {
        LogW(L"[LandWarKR] 훅 자리(0x%08X)의 머리 바이트가 다르다 — 걸지 않는다",
             (unsigned)(UINT_PTR)target);
        return 0;
    }

    st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        LogW(L"[LandWarKR] MH_Initialize 실패 %d", (int)st);
        return 0;
    }
    if (MH_CreateHook(target, (void*)DetourLoadSpr, (void**)&g_origLoad) != MH_OK) return 0;
    if (MH_EnableHook(target) != MH_OK) { g_origLoad = NULL; return 0; }
    LogW(L"[LandWarKR] 부대 그림 읽기(0x%08X) 훅 설치", (unsigned)(UINT_PTR)target);
    return 1;
}
