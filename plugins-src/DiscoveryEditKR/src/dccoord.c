#include "dccoord.h"
#include "disc.h"      // HintUtilKR/src — 이름은 그쪽 표를 그대로 쓴다(두 벌 두지 않는다)

// 자리와 까닭은 dccoord.h 에 적어 뒀다. 여기서는 표를 읽고 쓰기만 한다.
// JSON 스캐너는 WorldMapKR 의 discdb.c 와 같은 것이다 — 파일 스키마를 같게 뒀으니
// 읽는 법도 같아야 한다.

static unsigned char* g_tbl = NULL;
static DcBox g_orig[DC_N];
static int   g_ready = 0;

int DC_Ready(void) { return g_ready; }
int DC_Judged(int i) { return i >= 0 && i < DC_JUDGED_N; }

static int Commit(const void* p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

static unsigned char* Row(int i)
{
    if (!g_tbl || i < 0 || i >= DC_N) return NULL;
    return g_tbl + (unsigned)i * DC_SZ + DC_X1_OFF;
}

// 표 자리가 맞는지 — 좌표 넷은 모두 -1 이거나 모두 지도 안이어야 한다.
// 자리가 밀리면 이름 포인터나 가치가 이 칸에 걸려 대뜸 말이 안 되는 수가 나온다.
static int RowOk(const unsigned char* r)
{
    int x1 = *(const int*)(r + 0), y1 = *(const int*)(r + 4);
    int x2 = *(const int*)(r + 8), y2 = *(const int*)(r + 12);
    if (x1 == DC_NONE && y1 == DC_NONE && x2 == DC_NONE && y2 == DC_NONE) return 1;
    if (x1 < 0 || x1 > DC_X_MAX || x2 < x1 || x2 > DC_X_MAX) return 0;
    if (y1 < 0 || y1 > DC_Y_MAX || y2 < y1 || y2 > DC_Y_MAX) return 0;
    return 1;
}

int DC_Load(void)
{
    unsigned char* base;
    int i, coord = 0;

    if (g_ready) return 1;
    base = (unsigned char*)GetModuleHandleW(NULL);
    if (!base) return 0;
    g_tbl = base + DC_RVA;
    if (!Commit(g_tbl, (SIZE_T)DC_N * DC_SZ)) { g_tbl = NULL; return 0; }

    for (i = 0; i < DC_N; i++)
        if (!RowOk(g_tbl + (unsigned)i * DC_SZ + DC_X1_OFF)) { g_tbl = NULL; return 0; }

    for (i = 0; i < DC_N; i++) {
        const unsigned char* r = g_tbl + (unsigned)i * DC_SZ + DC_X1_OFF;
        g_orig[i].x1 = *(const int*)(r + 0);
        g_orig[i].y1 = *(const int*)(r + 4);
        g_orig[i].x2 = *(const int*)(r + 8);
        g_orig[i].y2 = *(const int*)(r + 12);
        if (g_orig[i].x1 != DC_NONE) coord++;
    }
    // 274줄이 다 -1 이면 자리는 읽혔어도 표가 아니다(0 으로 채운 빈 페이지를 잡은 것).
    // 원본은 147개가 좌표를 가지고 있다.
    if (coord < 10) { g_tbl = NULL; return 0; }

    g_ready = 1;
    OutputDebugStringW(L"[DiscoveryEditKR] 발견물 좌표표 274줄 로드.");
    return 1;
}

int DC_Get(int i, DcBox* out)
{
    const unsigned char* r = Row(i);
    if (!r || !out) return 0;
    out->x1 = *(const int*)(r + 0);
    out->y1 = *(const int*)(r + 4);
    out->x2 = *(const int*)(r + 8);
    out->y2 = *(const int*)(r + 12);
    return 1;
}

int DC_Orig(int i, DcBox* out)
{
    if (!g_ready || i < 0 || i >= DC_N || !out) return 0;
    *out = g_orig[i];
    return 1;
}

int DC_Changed(int i)
{
    DcBox now;
    if (!DC_Get(i, &now)) return 0;
    return now.x1 != g_orig[i].x1 || now.y1 != g_orig[i].y1 ||
           now.x2 != g_orig[i].x2 || now.y2 != g_orig[i].y2;
}

int DC_ChangedCount(void)
{
    int i, n = 0;
    for (i = 0; i < DC_N; i++) if (DC_Changed(i)) n++;
    return n;
}

int DC_BoxOk(const DcBox* b)
{
    if (!b) return 0;
    if (b->x1 == DC_NONE && b->y1 == DC_NONE && b->x2 == DC_NONE && b->y2 == DC_NONE) return 1;
    if (b->x1 < 0 || b->x1 > DC_X_MAX) return 0;
    if (b->y1 < 0 || b->y1 > DC_Y_MAX) return 0;
    if (b->x2 < b->x1 || b->x2 > DC_X_MAX) return 0;
    if (b->y2 < b->y1 || b->y2 > DC_Y_MAX) return 0;
    return 1;
}

// .rdata 라 읽기 전용이다. 잠깐만 열고 되돌린다. 파일이 아니라 메모리를 고치는 것이라
// 게임을 끄면 원래대로 돌아간다 — 그래서 DC_Save 로 적어 둔다.
static int Poke(int i, const DcBox* b)
{
    unsigned char* r = Row(i);
    DWORD old = 0;
    if (!r) return 0;
    if (!VirtualProtect(r, 16, PAGE_READWRITE, &old)) return 0;
    *(int*)(r + 0)  = b->x1;
    *(int*)(r + 4)  = b->y1;
    *(int*)(r + 8)  = b->x2;
    *(int*)(r + 12) = b->y2;
    VirtualProtect(r, 16, old, &old);
    return 1;
}

int DC_Set(int i, const DcBox* b)
{
    if (!g_ready || i < 0 || i >= DC_N || !DC_BoxOk(b)) return 0;
    return Poke(i, b);
}

int DC_Revert(int i)
{
    if (!g_ready || i < 0 || i >= DC_N) return 0;
    return Poke(i, &g_orig[i]);
}

int DC_RevertAll(void)
{
    int i, n = 0;
    for (i = 0; i < DC_N; i++)
        if (DC_Changed(i) && DC_Revert(i)) n++;
    return n;
}

int DC_FleetCell(int* x, int* y)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    const int *plon, *plat;
    int lo, la;
    if (!base) return 0;
    plon = (const int*)(base + DC_LON_RVA);
    plat = (const int*)(base + DC_LAT_RVA);
    // .data 뒷부분이라 세이브를 불러와야 생긴다.
    if (!Commit(plon, sizeof(int)) || !Commit(plat, sizeof(int))) return 0;
    lo = *plon; la = *plat;
    if (lo < 0 || lo > DC_LON_MAX || la < 0 || la > DC_LAT_MAX) return 0;
    if (x) *x = (int)((long long)lo * (DC_X_MAX + 1) / DC_LON_MAX);
    if (y) *y = (int)((long long)la * (DC_Y_MAX + 1) / DC_LAT_MAX);
    return 1;
}

// ---------------------------------------------------------------- 파일

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다.
// (discdb.c 의 UpToDataDir 과 같은 규칙이다 — 데이터는 한 자리에 모아 둬야 서로 찾는다.)
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

void DC_JsonPath(HINSTANCE hinst, wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(hinst, out, cch);     // ...\CDS95Util\DiscoveryEditKR.plugin
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"disc_coords.json");
}

// ---- JSON 최소 스캐너 (discdb.c 와 같은 것) ----

static void SkipWS(const char** pp)
{
    while (**pp == ' ' || **pp == '\t' || **pp == '\r' || **pp == '\n') (*pp)++;
}

static void SkipString(const char** pp)
{
    if (**pp != '"') return;
    (*pp)++;
    while (**pp && **pp != '"') { if (**pp == '\\' && (*pp)[1]) (*pp)++; (*pp)++; }
    if (**pp == '"') (*pp)++;
}

static void SkipValue(const char** pp)
{
    SkipWS(pp);
    if (**pp == '"') { SkipString(pp); return; }
    if (**pp == '{' || **pp == '[') {
        char open = **pp, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (**pp) {
            if (**pp == '"') { SkipString(pp); continue; }
            if (**pp == open) depth++;
            else if (**pp == close) { depth--; (*pp)++; if (!depth) return; continue; }
            (*pp)++;
        }
        return;
    }
    while (**pp && **pp != ',' && **pp != '}' && **pp != ']') (*pp)++;
}

static void ReadString(const char** pp, char* out, int cap)
{
    int n = 0;
    out[0] = 0;
    if (**pp != '"') return;
    (*pp)++;
    while (**pp && **pp != '"') {
        char c = **pp;
        if (c == '\\' && (*pp)[1]) { (*pp)++; c = **pp; }
        if (n < cap - 1) out[n++] = c;
        (*pp)++;
    }
    if (**pp == '"') (*pp)++;
    out[n] = 0;
}

static int ReadInt(const char** pp, int* out)
{
    const char* p = *pp;
    int sign = 1, val = 0, digits = 0;
    SkipWS(&p);
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; digits++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }   // 소수점은 버린다
    *pp = p;
    if (!digits) return 0;
    *out = sign * val;
    return 1;
}

#define NOVAL 0x7FFFFFFF

// 한 줄을 읽어 표에 넣는다. 넣었으면 1.
static int ParseOne(const char** pp)
{
    char key[32];
    int id = -1, x1 = NOVAL, y1 = NOVAL, x2 = NOVAL, y2 = NOVAL;
    DcBox b;

    if (**pp != '{') { SkipValue(pp); return 0; }
    (*pp)++;
    for (;;) {
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') { if (!**pp) return 0; (*pp)++; continue; }
        ReadString(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);
        if      (lstrcmpA(key, "id") == 0) { int v; if (ReadInt(pp, &v)) id = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "x1") == 0) { int v; if (ReadInt(pp, &v)) x1 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "y1") == 0) { int v; if (ReadInt(pp, &v)) y1 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "x2") == 0) { int v; if (ReadInt(pp, &v)) x2 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "y2") == 0) { int v; if (ReadInt(pp, &v)) y2 = v; else SkipValue(pp); }
        else SkipValue(pp);                       // name 은 사람 보라고 적힌 것이라 안 쓴다
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }

    if (id < 0 || id >= DC_N) return 0;           // 게임에 없는 번호는 무시
    if (x1 == NOVAL || y1 == NOVAL || x1 < 0 || y1 < 0) {   // 좌표 없음으로 하라는 뜻
        b.x1 = b.y1 = b.x2 = b.y2 = DC_NONE;
        return DC_Set(id, &b);
    }
    if (x2 == NOVAL || x2 < x1) x2 = x1;          // 끝을 안 적으면 점 하나
    if (y2 == NOVAL || y2 < y1) y2 = y1;
    b.x1 = x1; b.y1 = y1; b.x2 = x2; b.y2 = y2;
    return DC_Set(id, &b);
}

static char* ReadWholeFile(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD sz, got = 0;
    char* buf;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 4u * 1024 * 1024) { CloseHandle(h); return NULL; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, buf, sz, &got, NULL)) { HeapFree(GetProcessHeap(), 0, buf); CloseHandle(h); return NULL; }
    buf[got] = 0;
    CloseHandle(h);
    return buf;
}

int DC_Apply(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    char* buf;
    const char* p;
    int n = 0;

    if (!DC_Load()) return 0;
    DC_JsonPath(hinst, path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) return 0;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                                   // UTF-8 BOM
    SkipWS(&p);
    if (*p != '[') { HeapFree(GetProcessHeap(), 0, buf); return 0; }
    p++;
    for (;;) {
        SkipWS(&p);
        if (*p == ']' || !*p) break;
        if (*p == '{') n += ParseOne(&p);
        else SkipValue(&p);
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return n;
}

// ---- 저장 ----

static void AppendA(HANDLE h, const char* s)
{
    DWORD w = 0;
    WriteFile(h, s, (DWORD)lstrlenA(s), &w, NULL);
}

int DC_Save(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    int i, n = 0;
    char line[512], nm[160];

    if (!g_ready) return -1;
    DC_JsonPath(hinst, path, MAX_PATH);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;

    AppendA(h, "[\r\n");
    for (i = 0; i < DC_N; i++) {
        DcBox b;
        if (!DC_Changed(i) || !DC_Get(i, &b)) continue;
        // 이름은 사람이 파일을 열어 보고 어느 줄인지 알라고 적는다(읽을 때는 안 쓴다).
        nm[0] = 0;
        if (Disc_Ready())
            WideCharToMultiByte(CP_UTF8, 0, Disc_Name(i), -1, nm, sizeof(nm), NULL, NULL);
        wsprintfA(line, "%s  {\"id\": %d, \"name\": \"%s\", \"x1\": %d, \"y1\": %d, \"x2\": %d, \"y2\": %d}",
                  n ? ",\r\n" : "", i, nm, b.x1, b.y1, b.x2, b.y2);
        AppendA(h, line);
        n++;
    }
    AppendA(h, "\r\n]\r\n");
    CloseHandle(h);
    return n;
}
