#include "landwar.h"
#include <MinHook.h>

#define IMAGE_BASE 0x00400000u        // 표 안의 포인터는 이 바탕으로 박혀 있다

typedef struct { int id; int type[LW_UNIT_N]; int used; } Preset;

static BYTE*  g_base = NULL;
static Preset g_pre[LW_PRESET_MAX];
static int    g_allowBig = 0;

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
    int i, j;
    if (g_base) return 1;
    g_base = (BYTE*)GetModuleHandleW(NULL);
    if (!g_base) return 0;
    if (!Readable(g_base + LW_UNITS_RVA, LW_UNIT_N * LW_UNIT_SZ)) { g_base = NULL; return 0; }
    for (i = 0; i < LW_PRESET_MAX; i++) {
        g_pre[i].used = 0; g_pre[i].id = LW_ID_COMMON;
        for (j = 0; j < LW_UNIT_N; j++) g_pre[i].type[j] = -1;
    }
    LoadTables();
    LandWar_LoadFile();
    LogW(L"[LandWarKR] 부대 배열 0x%08X, 병종표 %s, 예약 %d벌",
         (unsigned)(UINT_PTR)(g_base + LW_UNITS_RVA), g_tblOk ? L"OK" : L"실패",
         LandWar_PresetCount());
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
// "레코드가 전부 0 이면 전투 밖"으로 보면 틀린다 — 전투 밖에서도 열두 칸의 `+0x1C` 에 1 이
// 남아 있다. 그래서 **앞쪽 여섯 칸만** 본다(실측: 전투 밖에서는 0~5 가 전부 0).
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
int LandWar_Stat(int slot, int kind)
{
    StatFn fn;
    if (!g_base || slot < 0 || slot >= LW_UNIT_N) return -1;
    if (!LandWar_Active()) return -1;
    fn = (StatFn)(g_base + LW_STAT_RVA);
    return fn(g_base + LW_OBJ_RVA, NULL, kind, slot);
}

// ---------------------------------------------------------------- 적장

// CLandWar+0x9C 가 적 쪽 객체를 가리키고, 그 +4 가 캐릭터 id 다.
// 게임 함수는 부르지 않는다 — 포인터만 따라간다(전투 밖이면 못 읽어 -1 이 나온다).
int LandWar_EnemyId(void)
{
    BYTE* obj;
    if (!g_base) return -1;
    if (!Readable(g_base + LW_OBJ_RVA + LW_ENEMY_OFF, sizeof(void*))) return -1;
    obj = *(BYTE**)(g_base + LW_OBJ_RVA + LW_ENEMY_OFF);
    if (!Readable(obj, 8)) return -1;
    return *(const int*)(obj + 4);
}

// 갈래 1(인물 런타임 배열)이면 이름을 읽어 본다. 글자로 안 보이면 0.
int LandWar_EnemyName(int id, wchar_t* out, int cap)
{
    const BYTE* rec;
    char raw[24];
    int cat, idx, i;

    if (cap > 0) out[0] = 0;
    if (!g_base || id < 0 || cap <= 0) return 0;
    cat = id >> 12;
    idx = id & 0xFFF;
    if (cat != 1 || idx < 0 || idx > 400) return 0;

    rec = g_base + LW_CHAR_RVA + (unsigned)idx * LW_CHAR_SZ;
    if (!Readable(rec, LW_CHAR_SZ)) return 0;
    CopyMemory(raw, rec + LW_CHAR_NAME, 19);
    raw[19] = 0;
    for (i = 0; i < 19 && raw[i]; i++)
        if ((unsigned char)raw[i] < 0x20) return 0;   // 글자가 아니면 포기
    if (!raw[0]) return 0;
    return MultiByteToWideChar(949, 0, raw, -1, out, cap) > 0 ? 1 : 0;
}

// ---------------------------------------------------------------- 예약

static Preset* Find(int id, int make)
{
    int i, free1 = -1;
    for (i = 0; i < LW_PRESET_MAX; i++) {
        if (g_pre[i].used && g_pre[i].id == id) return &g_pre[i];
        if (!g_pre[i].used && free1 < 0) free1 = i;
    }
    if (!make || free1 < 0) return NULL;
    g_pre[free1].used = 1;
    g_pre[free1].id = id;
    for (i = 0; i < LW_UNIT_N; i++) g_pre[free1].type[i] = -1;
    return &g_pre[free1];
}

int LandWar_SetPreset(int id, int slot, int t)
{
    Preset* p;
    if (slot < 0 || slot >= LW_UNIT_N) return 0;
    if (t >= 0 && !LandWar_TypeOkForSlot(slot, t)) return 0;
    p = Find(id, 1);
    if (!p) return 0;
    p->type[slot] = (t >= 0 && t < LW_TYPE_N) ? t : -1;
    if (p->type[slot] >= 0) LandWar_HookInstall();   // 쓸 때가 되어서야 훅을 건다
    return 1;
}

int LandWar_Preset(int id, int slot)
{
    Preset* p = Find(id, 0);
    if (!p || slot < 0 || slot >= LW_UNIT_N) return -1;
    return p->type[slot];
}

void LandWar_ClearPreset(int id)
{
    Preset* p = Find(id, 0);
    int i;
    if (!p) return;
    p->used = 0;
    for (i = 0; i < LW_UNIT_N; i++) p->type[i] = -1;
}

int LandWar_PresetCount(void)
{
    int i, n = 0;
    for (i = 0; i < LW_PRESET_MAX; i++) if (g_pre[i].used) n++;
    return n;
}
int LandWar_PresetIdAt(int i)
{
    int k, n = 0;
    for (k = 0; k < LW_PRESET_MAX; k++) {
        if (!g_pre[k].used) continue;
        if (n == i) return g_pre[k].id;
        n++;
    }
    return LW_ID_COMMON;
}

// ---------------------------------------------------------------- 파일
// CDS95Util\landwar.txt — 한 줄이 한 벌이다.
//   <적장id> <12칸 병종>     id -1 = 모든 적장 공통, 병종 -1 = 그 칸은 그대로 둠

static void FilePath(wchar_t* out, int cch)
{
    wchar_t exe[MAX_PATH];
    int i, cut = -1;
    out[0] = 0;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == L'\\' || exe[i] == L'/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfW(out, L"%s\\CDS95Util\\landwar.txt", exe);
    (void)cch;
}

int LandWar_Save(void)
{
    wchar_t path[MAX_PATH];
    char line[256], one[16];
    HANDLE f;
    DWORD wr;
    int i, j;

    FilePath(path, MAX_PATH);
    if (!path[0]) return 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;

    lstrcpyA(line, "# LandWarKR - land battle unit presets\r\n"
                   "# <enemyId> <12 unit types>   id -1 = all enemies, type -1 = leave as is\r\n");
    WriteFile(f, line, (DWORD)lstrlenA(line), &wr, NULL);

    for (i = 0; i < LW_PRESET_MAX; i++) {
        if (!g_pre[i].used) continue;
        wsprintfA(line, "%d", g_pre[i].id);
        for (j = 0; j < LW_UNIT_N; j++) { wsprintfA(one, " %d", g_pre[i].type[j]); lstrcatA(line, one); }
        lstrcatA(line, "\r\n");
        WriteFile(f, line, (DWORD)lstrlenA(line), &wr, NULL);
    }
    CloseHandle(f);
    LogW(L"[LandWarKR] 예약 %d벌을 %s 에 남겼다", LandWar_PresetCount(), path);
    return 1;
}

int LandWar_LoadFile(void)
{
    wchar_t path[MAX_PATH];
    HANDLE f;
    DWORD size, got = 0;
    char *buf, *p;
    int n = 0;

    FilePath(path, MAX_PATH);
    if (!path[0]) return 0;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > 0x10000) { CloseHandle(f); return 0; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(f); return 0; }
    ReadFile(f, buf, size, &got, NULL);
    buf[got] = 0;
    CloseHandle(f);

    p = buf;
    while (*p) {
        int v[LW_UNIT_N + 1], k, neg, any;
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') p++;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (!*p) break;
        for (k = 0; k < LW_UNIT_N + 1; k++) {
            while (*p == ' ' || *p == '\t') p++;
            neg = 0; any = 0;
            if (*p == '-') { neg = 1; p++; }
            v[k] = 0;
            while (*p >= '0' && *p <= '9') { v[k] = v[k] * 10 + (*p - '0'); p++; any = 1; }
            if (!any) break;
            if (neg) v[k] = -v[k];
        }
        if (k == LW_UNIT_N + 1) {
            Preset* pr = Find(v[0], 1);
            if (pr) {
                int j;
                for (j = 0; j < LW_UNIT_N; j++)
                    pr->type[j] = (v[j + 1] >= 0 && v[j + 1] < LW_TYPE_N) ? v[j + 1] : -1;
                n++;
            }
        }
        while (*p && *p != '\n') p++;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (n) LandWar_HookInstall();
    return n;
}

// ---------------------------------------------------------------- 큰 그림 막기

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

// ---------------------------------------------------------------- 훅
// 0x0044A0D0 은 전투 시작 때 12칸을 돌며 `+0x04`(병종)로 그림을 읽는다.
// 게임이 편성을 다 뽑은 뒤라서, 여기서 눌러 쓰면 그림까지 그 병종으로 읽힌다.
// 이 적장 벌이 있으면 그것을, 없으면 공통 벌을 쓴다.

static void __fastcall DetourLoadSpr(void* self, void* edx)
{
    int id = LandWar_EnemyId();
    Preset* p = Find(id, 0);
    int i, n = 0, own = 0;

    if (p) own = 1; else p = Find(LW_ID_COMMON, 0);
    if (p) {
        for (i = 0; i < LW_UNIT_N; i++) {
            int t = p->type[i];
            BYTE* u;
            if (t < 0 || !LandWar_TypeOkForSlot(i, t)) continue;
            u = Unit(i);
            if (!u) continue;
            *(int*)(u + LW_TYPE_OFF) = t;
            n++;
        }
    }
    LogW(L"[LandWarKR] 전투 시작 — 적장 id 0x%X, %s벌로 %d칸 눌러 썼다",
         id, own ? L"이 적장" : L"공통", n);
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

    if (!g_base) return 0;
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
