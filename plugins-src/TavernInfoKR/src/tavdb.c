#include "tavdb.h"

// 자리와 까닭은 tavdb.h 에 적어 뒀다. 여기서는 표를 읽고 쓰기만 한다.
// JSON 스캐너는 WorldMapKR 의 discdb.c · DiscoveryEditKR 의 dccoord.c 와 같은 방식이다.

static unsigned char* g_tbl = NULL;
static TavRow g_orig[TAV_N];
static int    g_ready = 0;
static wchar_t g_text[256];

int TavDb_Ready(void) { return g_ready; }

static int Commit(const void* p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

static unsigned char* Rec(int i)
{
    if (!g_tbl || i < 0 || i >= TAV_N) return NULL;
    return g_tbl + (unsigned)i * TAV_SZ;
}

// 표 자리가 맞는지 — 코드는 0 이상, 도시는 -1 이거나 0~225, 대사는 읽을 수 있는 문자열.
static int RecOk(const unsigned char* r)
{
    int code = *(const int*)(r + TAV_CODE_OFF);
    const char* t;
    int k;
    if (code < 0 || code > TAV_CODE_MAX) return 0;
    for (k = 0; k < TAV_CITY_N; k++) {
        int c = *(const int*)(r + TAV_CITY_OFF + k * 4);
        if (c != TAV_NONE && (c < 0 || c > TAV_CITY_MAX)) return 0;
    }
    t = *(const char* const*)(r + TAV_TEXT_OFF);
    return t && Commit(t, 1);
}

int TavDb_Load(void)
{
    unsigned char* base;
    int i;

    if (g_ready) return 1;
    base = (unsigned char*)GetModuleHandleW(NULL);
    if (!base) return 0;
    g_tbl = base + TAV_RVA;
    if (!Commit(g_tbl, (SIZE_T)TAV_N * TAV_SZ)) { g_tbl = NULL; return 0; }

    for (i = 0; i < TAV_N; i++)
        if (!RecOk(g_tbl + (unsigned)i * TAV_SZ)) { g_tbl = NULL; return 0; }

    for (i = 0; i < TAV_N; i++) {
        const unsigned char* r = g_tbl + (unsigned)i * TAV_SZ;
        int k;
        g_orig[i].code = *(const int*)(r + TAV_CODE_OFF);
        for (k = 0; k < TAV_CITY_N; k++)
            g_orig[i].city[k] = *(const int*)(r + TAV_CITY_OFF + k * 4);
    }
    g_ready = 1;
    OutputDebugStringW(L"[TavernInfoKR] 술집 대사표 191줄 로드.");
    return 1;
}

int TavDb_Get(int i, TavRow* out)
{
    const unsigned char* r = Rec(i);
    int k;
    if (!r || !out) return 0;
    out->code = *(const int*)(r + TAV_CODE_OFF);
    for (k = 0; k < TAV_CITY_N; k++)
        out->city[k] = *(const int*)(r + TAV_CITY_OFF + k * 4);
    return 1;
}

int TavDb_Orig(int i, TavRow* out)
{
    if (!g_ready || i < 0 || i >= TAV_N || !out) return 0;
    *out = g_orig[i];
    return 1;
}

int TavDb_Changed(int i)
{
    TavRow now;
    int k;
    if (!TavDb_Get(i, &now)) return 0;
    if (now.code != g_orig[i].code) return 1;
    for (k = 0; k < TAV_CITY_N; k++)
        if (now.city[k] != g_orig[i].city[k]) return 1;
    return 0;
}

int TavDb_ChangedCount(void)
{
    int i, n = 0;
    for (i = 0; i < TAV_N; i++) if (TavDb_Changed(i)) n++;
    return n;
}

const wchar_t* TavDb_Text(int i)
{
    const unsigned char* r = Rec(i);
    const char* t;
    g_text[0] = 0;
    if (!r) return L"";
    t = *(const char* const*)(r + TAV_TEXT_OFF);
    if (!t || !Commit(t, 1)) return L"";
    MultiByteToWideChar(949, 0, t, -1, g_text, (int)(sizeof(g_text) / sizeof(wchar_t)));
    return g_text;
}

int TavDb_RowOk(const TavRow* r)
{
    int k;
    if (!r) return 0;
    if (r->code < 0 || r->code > TAV_CODE_MAX) return 0;
    for (k = 0; k < TAV_CITY_N; k++)
        if (r->city[k] != TAV_NONE && (r->city[k] < 0 || r->city[k] > TAV_CITY_MAX)) return 0;
    return 1;
}

// .rdata 라 읽기 전용이다. 잠깐만 열고 되돌린다. 메모리를 고치는 것이라 게임을 끄면
// 원래대로 돌아간다 — 그래서 TavDb_Save 로 적어 둔다.
static int Poke(int i, const TavRow* v)
{
    unsigned char* r = Rec(i);
    DWORD old = 0;
    int k;
    if (!r) return 0;
    if (!VirtualProtect(r, TAV_TEXT_OFF, PAGE_READWRITE, &old)) return 0;   // 대사 포인터는 안 건드린다
    *(int*)(r + TAV_CODE_OFF) = v->code;
    for (k = 0; k < TAV_CITY_N; k++)
        *(int*)(r + TAV_CITY_OFF + k * 4) = v->city[k];
    VirtualProtect(r, TAV_TEXT_OFF, old, &old);
    return 1;
}

int TavDb_Set(int i, const TavRow* r)
{
    if (!g_ready || i < 0 || i >= TAV_N || !TavDb_RowOk(r)) return 0;
    return Poke(i, r);
}

int TavDb_Revert(int i)
{
    if (!g_ready || i < 0 || i >= TAV_N) return 0;
    return Poke(i, &g_orig[i]);
}

int TavDb_RevertAll(void)
{
    int i, n = 0;
    for (i = 0; i < TAV_N; i++)
        if (TavDb_Changed(i) && TavDb_Revert(i)) n++;
    return n;
}

int TavDb_CurCity(void)
{
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    const int* p;
    if (!base) return -1;
    p = (const int*)(base + TAV_CUR_CITY_RVA);
    if (!Commit(p, sizeof(int))) return -1;
    return (*p >= 0 && *p <= TAV_CITY_MAX) ? *p : -1;
}

// ---------------------------------------------------------------- 파일

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다.
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

void TavDb_JsonPath(HINSTANCE hinst, wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"tavern_info.json");
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
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
    *pp = p;
    if (!digits) return 0;
    *out = sign * val;
    return 1;
}

#define NOVAL 0x7FFFFFFF

// "cities": [206, 204] 를 읽는다. 적은 만큼만 채우고 나머지는 -1 이다.
static void ReadCities(const char** pp, int* city, int* got)
{
    int n = 0;
    SkipWS(pp);
    if (**pp != '[') { SkipValue(pp); return; }
    (*pp)++;
    for (;;) {
        int v;
        SkipWS(pp);
        if (**pp == ']' || !**pp) { if (**pp) (*pp)++; break; }
        if (ReadInt(pp, &v)) { if (n < TAV_CITY_N) city[n++] = v; }
        else SkipValue(pp);
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }
    *got = 1;
    while (n < TAV_CITY_N) city[n++] = TAV_NONE;
}

static int ParseOne(const char** pp)
{
    char key[32];
    int id = -1, code = NOVAL, gotCity = 0;
    TavRow row;
    int k;

    for (k = 0; k < TAV_CITY_N; k++) row.city[k] = TAV_NONE;

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
        if      (lstrcmpA(key, "id") == 0)     { int v; if (ReadInt(pp, &v)) id = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "code") == 0)   { int v; if (ReadInt(pp, &v)) code = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "cities") == 0) ReadCities(pp, row.city, &gotCity);
        else SkipValue(pp);                    // text 는 사람 보라고 적힌 것이라 안 쓴다
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }

    if (id < 0 || id >= TAV_N) return 0;
    {
        TavRow now;
        if (!TavDb_Get(id, &now)) return 0;
        row.code = (code == NOVAL) ? now.code : code;      // 안 적은 것은 지금 값을 그대로 둔다
        if (!gotCity) for (k = 0; k < TAV_CITY_N; k++) row.city[k] = now.city[k];
    }
    return TavDb_Set(id, &row);
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

int TavDb_Apply(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    char* buf;
    const char* p;
    int n = 0;

    if (!TavDb_Load()) return 0;
    TavDb_JsonPath(hinst, path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) return 0;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
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

// JSON 문자열 안에서 탈이 나는 것만 막는다(따옴표·역슬래시·줄바꿈).
static void JsonEscape(const char* in, char* out, int cap)
{
    int n = 0;
    for (; *in && n < cap - 2; in++) {
        if (*in == '"' || *in == '\\') { out[n++] = '\\'; out[n++] = *in; }
        else if (*in == '\r' || *in == '\n') out[n++] = ' ';
        else out[n++] = *in;
    }
    out[n] = 0;
}

int TavDb_Save(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    int i, n = 0;
    char line[1024], txt[512], esc[600];

    if (!g_ready) return -1;
    TavDb_JsonPath(hinst, path, MAX_PATH);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;

    AppendA(h, "[\r\n");
    for (i = 0; i < TAV_N; i++) {
        TavRow r;
        char cities[64];
        int k, m = 0;
        if (!TavDb_Changed(i) || !TavDb_Get(i, &r)) continue;

        cities[0] = 0;
        for (k = 0; k < TAV_CITY_N; k++) {
            char one[16];
            if (r.city[k] == TAV_NONE) continue;
            wsprintfA(one, "%s%d", m ? ", " : "", r.city[k]);
            lstrcatA(cities, one);
            m++;
        }
        // 대사는 사람이 파일을 열어 보고 어느 줄인지 알라고 적는다(읽을 때는 안 쓴다).
        txt[0] = 0;
        WideCharToMultiByte(CP_UTF8, 0, TavDb_Text(i), -1, txt, sizeof(txt), NULL, NULL);
        JsonEscape(txt, esc, sizeof(esc));

        wsprintfA(line, "%s  {\"id\": %d, \"code\": %d, \"cities\": [%s], \"text\": \"%s\"}",
                  n ? ",\r\n" : "", i, r.code, cities, esc);
        AppendA(h, line);
        n++;
    }
    AppendA(h, "\r\n]\r\n");
    CloseHandle(h);
    return n;
}
