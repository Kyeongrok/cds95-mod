#include "disc.h"

// 자리는 disc.h 에 적어 뒀다. 여기서는 읽기만 한다.

static int  g_ready = 0;
static int  g_haveSave = 0;
static wchar_t g_name[DISC_N][40];
static int  g_cat[DISC_N];
static int  g_value[DISC_N];
static unsigned char g_found[DISC_N];    // DISC_NOT / FOUND / REPORTED

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

// 게임 폴더의 SAVEDATA.CDS. 실행 파일이 있는 자리에서 찾는다(faces.c 와 같은 관용구).
static void SavePathW(wchar_t* out, int cch)
{
    wchar_t* p;
    wchar_t* last;
    GetModuleFileNameW(NULL, out, cch);
    last = out;
    for (p = out; *p; p++) if (*p == L'\\' || *p == L'/') last = p;
    *last = 0;
    lstrcatW(out, L"\\SAVEDATA.CDS");
}

// 세이브에서 발견 여부만 훑는다. 파일 전체를 들고 있지 않는다(310KB 를 붙들 이유가 없다).
static int ReadSave(void)
{
    wchar_t path[MAX_PATH];
    HANDLE f;
    DWORD got = 0;
    unsigned char* buf;
    unsigned need = DISC_SAVE_OFF + (unsigned)DISC_SAVE_N * DISC_SAVE_SZ;
    int i, any = 0;

    SavePathW(path, MAX_PATH);
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, need);
    if (!buf) { CloseHandle(f); return 0; }
    if (!ReadFile(f, buf, need, &got, NULL) || got < need) {
        HeapFree(GetProcessHeap(), 0, buf); CloseHandle(f); return 0;
    }
    CloseHandle(f);

    for (i = 0; i < DISC_N; i++) g_found[i] = DISC_NOT;
    for (i = 0; i < DISC_SAVE_N && i < DISC_N; i++) {
        unsigned char b = buf[DISC_SAVE_OFF + (unsigned)i * DISC_SAVE_SZ];
        if (b & 0x40) { g_found[i] = (b & 0x80) ? DISC_REPORTED : DISC_FOUND; any++; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return 1;
}

int Disc_Load(void)
{
    unsigned char* base;
    const unsigned char* rec;
    int i, ok = 0;

    // 세이브는 매번 다시 읽는다 — 게임을 하다 저장했을 수 있다.
    g_haveSave = ReadSave();

    if (g_ready) return 1;
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
    }
    if (ok < DISC_N / 2) return 0;       // 대부분이 이름을 못 내면 표 자리가 어긋난 것이다
    g_ready = 1;
    return 1;
}

int Disc_Ready(void)    { return g_ready; }
int Disc_HaveSave(void) { return g_haveSave; }
int Disc_Count(void)    { return DISC_N; }

const wchar_t* Disc_Name(int i)
{
    return (g_ready && i >= 0 && i < DISC_N && g_name[i][0]) ? g_name[i] : L"?";
}
int Disc_Cat(int i)   { return (g_ready && i >= 0 && i < DISC_N) ? g_cat[i] : -1; }
int Disc_Value(int i) { return (g_ready && i >= 0 && i < DISC_N) ? g_value[i] : -1; }

int Disc_Found(int i)
{
    if (!g_haveSave || i < 0 || i >= DISC_N) return DISC_UNKNOWN;
    return g_found[i];
}
