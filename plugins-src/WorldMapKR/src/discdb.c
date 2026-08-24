#include "discdb.h"
#include "discovery_coords.h"   // kDiscCoords — discoveries.json 이 없을 때 쓰는 기본값
#include "dccoord.h"            // DiscoveryEditKR/src — 표 자리와 좌표 칸 오프셋(상수만 빌려 쓴다)

// citydb.c 의 JSON 스캐너와 같은 방식이다. 그쪽은 도 단위 소수를 읽어야 해서 ReadFixed3 이
// 필요했지만 여기 좌표는 칸 번호(정수)라 더 단순하다.

static DiscPt g_disc[DISCDB_MAX];
static int    g_fromFile = 0;
static int    g_fromGame = 0;   // 실행 중인 게임 표에서 걷어온 줄 수

int DiscDb_FromFile(void) { return g_fromFile; }
int DiscDb_FromGame(void) { return g_fromGame; }

const DiscPt* DiscDb_At(int i)
{
    static const DiscPt empty = { DISCDB_NONE, DISCDB_NONE, DISCDB_NONE, DISCDB_NONE, { 0 } };
    if (i < 0 || i >= DISCDB_MAX) return &empty;
    return &g_disc[i];
}

int DiscDb_Marked(void)
{
    int i, n = 0;
    for (i = 0; i < DISCDB_MAX; i++) if (g_disc[i].x1 != DISCDB_NONE) n++;
    return n;
}

// 가운데를 남기고 양쪽을 잘라 낸다. 남극대륙(2500x150)·신대륙(481x751) 둘만 걸린다.
// 플러그인이 CDS95Util\\plugins\\<만든이>\\ 에 있으면 데이터는 그 위 CDS95Util 에 있다.
// 플러그인은 만든이별로 폴더를 나눠도 cities.json / quests.json / mods 같은 것은 한 자리에
// 모아 둬야 서로 찾을 수 있기 때문이다. 루트에 있는 플러그인은 이 함수가 아무 것도 안 한다.
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

// ---- 실행 중인 게임 표에서 고쳐진 줄을 걷어온다 ----
//
// DiscoveryEditKR 이 발견물 좌표를 고치면 그것은 게임 메모리(.rdata 0x11C540 의 +0x44~0x50)에
// 들어간다. 지도가 내장 표만 보면 게임은 옮겨 갔는데 마커는 옛 자리에 남는다. 그래서 지도를
// 열 때(그리고 R 을 누를 때)마다 실행 중인 표를 한 번 훑어 따라간다.
//
// ★ 다른 줄만 가져온다. 내장 표 kDiscCoords 가 바로 그 게임 원본값이라, 메모리 값이 그것과
//   다르면 "누가 실행 중에 고친 줄"이라는 뜻이다. 원본 그대로인 줄까지 덮어쓰면 사람이
//   discoveries.json 에 손으로 적어 둔 마커 보정이 매번 지워진다 — 그것은 남겨야 한다.
//   그래서 우선순위가 이렇게 된다:  고쳐진 게임 표 > discoveries.json > 내장 표.
static int Commit(const void* p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

static int LoadLive(void)
{
    const unsigned char* tbl;
    int i, n = 0;

    tbl = (const unsigned char*)GetModuleHandleW(NULL);
    if (!tbl) return 0;
    tbl += DC_RVA;
    if (!Commit(tbl, (SIZE_T)DC_N * DC_SZ)) return 0;

    for (i = 0; i < DISCDB_MAX && i < DC_N; i++) {
        const unsigned char* r = tbl + (unsigned)i * DC_SZ + DC_X1_OFF;
        int x1 = *(const int*)(r + 0), y1 = *(const int*)(r + 4);
        int x2 = *(const int*)(r + 8), y2 = *(const int*)(r + 12);

        if (x1 == kDiscCoords[i].x1 && y1 == kDiscCoords[i].y1 &&
            x2 == kDiscCoords[i].x2 && y2 == kDiscCoords[i].y2) continue;   // 원본 그대로다

        if (x1 == DC_NONE) {                          // 좌표를 지운 줄
            g_disc[i].x1 = g_disc[i].y1 = g_disc[i].x2 = g_disc[i].y2 = DISCDB_NONE;
            n++;
            continue;
        }
        // 지도 밖 값은 안 가져온다 — 표 자리가 어긋났을 때 마커가 사방으로 튀는 것을 막는다.
        if (x1 < 0 || x1 > DC_X_MAX || x2 < x1 || x2 > DC_X_MAX) continue;
        if (y1 < 0 || y1 > DC_Y_MAX || y2 < y1 || y2 > DC_Y_MAX) continue;
        g_disc[i].x1 = x1; g_disc[i].y1 = y1;
        g_disc[i].x2 = x2; g_disc[i].y2 = y2;
        n++;
    }
    return n;
}

static void ClampSpan(int* a, int* b)
{
    int span = *b - *a + 1, mid;
    if (span <= DISCDB_MAX_SPAN) return;
    mid = (*a + *b) / 2;
    *a = mid - DISCDB_MAX_SPAN / 2;
    *b = *a + DISCDB_MAX_SPAN - 1;
}

int DiscDb_DrawBox(int i, int* x1, int* y1, int* x2, int* y2)
{
    const DiscPt* d = DiscDb_At(i);
    if (d->x1 == DISCDB_NONE) return 0;
    *x1 = d->x1; *y1 = d->y1; *x2 = d->x2; *y2 = d->y2;
    ClampSpan(x1, x2);
    ClampSpan(y1, y2);
    return 1;
}

// ---- JSON 최소 스캐너 ----

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
    while (**pp && **pp != ',' && **pp != '}' && **pp != ']') (*pp)++;   // 숫자 / true / false / null
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

static void ParseDisc(const char** pp)
{
    char key[32], name[64];
    int id = -1, x1 = NOVAL, y1 = NOVAL, x2 = NOVAL, y2 = NOVAL;
    int haveName = 0;

    name[0] = 0;
    if (**pp != '{') { SkipValue(pp); return; }
    (*pp)++;
    for (;;) {
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') { if (!**pp) return; (*pp)++; continue; }
        ReadString(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);

        if      (lstrcmpA(key, "id") == 0)   { int v; if (ReadInt(pp, &v)) id = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "x1") == 0)   { int v; if (ReadInt(pp, &v)) x1 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "y1") == 0)   { int v; if (ReadInt(pp, &v)) y1 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "x2") == 0)   { int v; if (ReadInt(pp, &v)) x2 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "y2") == 0)   { int v; if (ReadInt(pp, &v)) y2 = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "name") == 0) { ReadString(pp, name, sizeof(name)); haveName = name[0] != 0; }
        else SkipValue(pp);

        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }

    if (id < 0 || id >= DISCDB_MAX) return;   // 게임에 없는 번호는 무시
    if (haveName)
        MultiByteToWideChar(CP_UTF8, 0, name, -1, g_disc[id].name,
                            (int)(sizeof(g_disc[id].name) / sizeof(wchar_t)));

    if (x1 == NOVAL || y1 == NOVAL || x1 < 0 || y1 < 0) {
        g_disc[id].x1 = g_disc[id].y1 = g_disc[id].x2 = g_disc[id].y2 = DISCDB_NONE;
        return;
    }
    if (x2 == NOVAL || x2 < x1) x2 = x1;      // 끝을 안 적으면 점 하나
    if (y2 == NOVAL || y2 < y1) y2 = y1;
    g_disc[id].x1 = x1; g_disc[id].y1 = y1;
    g_disc[id].x2 = x2; g_disc[id].y2 = y2;
}

// ---- 파일 ----

static void JsonPath(HINSTANCE hinst, wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(hinst, out, cch);     // ...\CDS95Util\WorldMapKR.plugin
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"discoveries.json");
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

// discoveries.json 을 읽어 덮어쓴다. 실제로 읽었으면 1.
static int LoadFile(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    char* buf;
    const char* p;
    int n = 0;

    JsonPath(hinst, path, MAX_PATH);
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
        if (*p == '{') { ParseDisc(&p); n++; }
        else SkipValue(&p);
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    OutputDebugStringW(n > 0 ? L"[WorldMapKR] discoveries.json 로드."
                             : L"[WorldMapKR] discoveries.json 비어 있음.");
    return n > 0;
}

// 셋을 이 차례로 깐다 — 뒤에 깔리는 것이 이긴다.
//   구운 표(게임 원본)  ->  discoveries.json(사람이 손본 마커)  ->  고쳐진 게임 표
// 지도를 열 때와 R 을 누를 때마다 불린다. 그래서 DiscoveryEditKR 로 좌표를 옮기고
// 지도를 다시 열면 마커가 알아서 따라와 있다.
void DiscDb_Load(HINSTANCE hinst)
{
    int i;

    for (i = 0; i < DISCDB_MAX; i++) {
        g_disc[i].x1 = kDiscCoords[i].x1;
        g_disc[i].y1 = kDiscCoords[i].y1;
        g_disc[i].x2 = kDiscCoords[i].x2;
        g_disc[i].y2 = kDiscCoords[i].y2;
        lstrcpynW(g_disc[i].name, kDiscCoords[i].name,
                  (int)(sizeof(g_disc[i].name) / sizeof(wchar_t)));
    }

    g_fromFile = LoadFile(hinst);
    g_fromGame = LoadLive();
    if (g_fromGame > 0) {
        wchar_t s[96];
        wsprintfW(s, L"[WorldMapKR] 실행 중인 게임 표에서 고쳐진 발견물 %d줄을 따라감.", g_fromGame);
        OutputDebugStringW(s);
    }
}
