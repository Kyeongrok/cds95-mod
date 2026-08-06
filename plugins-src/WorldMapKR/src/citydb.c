#include "citydb.h"
#include "city_coords.h"   // kCityLat/kCityLon — cities.json 이 없을 때 쓰는 기본값
#include "cities_data.h"   // TradeUtilKR 의 kCities[226] — 기본 이름표

// cities.json 을 읽는 작은 스캐너. 필요한 건 id/name/latitude/longitude 넷뿐이라
// 온전한 JSON 파서를 두지 않고 값 건너뛰기 + 키 분기만으로 처리한다.
// (PatchUtilKR 에도 비슷한 코드가 있지만 그쪽은 자기 스키마에 얽혀 있어 나누지 않았다.)

static CityPt g_city[CITYDB_MAX];
static int    g_fromFile = 0;

int CityDb_FromFile(void) { return g_fromFile; }

const CityPt* CityDb_At(int i)
{
    static const CityPt empty = { CITYDB_NONE, CITYDB_NONE, 0, { 0 } };
    if (i < 0 || i >= CITYDB_MAX) return &empty;
    return &g_city[i];
}

int CityDb_Marked(void)
{
    int i, n = 0;
    for (i = 0; i < CITYDB_MAX; i++) if (g_city[i].lonRaw != CITYDB_NONE) n++;
    return n;
}

// ---- JSON 최소 스캐너 ----

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

// 문자열을 UTF-8 그대로 꺼낸다(\u 이스케이프는 이 파일에 안 쓰이므로 다루지 않는다).
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

// 숫자면 1/1000 단위로 담고 1, null 등이면 0.
// 게임 좌표 한 눈금이 정확히 0.009도라 소수 셋째 자리까지 읽어야 값이 딱 떨어진다
// (둘째 자리에서 자르면 도시마다 한 눈금씩 어긋난다 — 화면엔 안 보이지만 굳이 어긋낼 이유가 없다).
static int ReadFixed3(const char** pp, int* out)
{
    static const int kScale[4] = { 1000, 100, 10, 1 };
    const char* p = *pp;
    int sign = 1, val = 0, digits = 0, frac = 0, k;
    SkipWS(&p);
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; digits++; }
    if (*p == '.') {
        p++;
        for (k = 0; *p >= '0' && *p <= '9'; k++, p++) {
            if (k < 3) frac += (*p - '0') * kScale[k + 1];
            digits++;
        }
    }
    *pp = p;
    if (!digits) return 0;
    *out = sign * (val * 1000 + frac);
    return 1;
}

// 정수만 필요한 자리(id)용.
static int ReadInt(const char** pp, int* out)
{
    int v;
    if (!ReadFixed3(pp, &v)) return 0;
    *out = v / 1000;
    return 1;
}

// 객체 하나 = 도시 하나.
#define NOVAL 0x7FFFFFFF

static void ParseCity(const char** pp)
{
    char key[32], name[64];
    int id = -1, lat = NOVAL, lon = NOVAL;   // 1/1000 도 단위
    int haveName = 0, lib = -1;

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

        if (lstrcmpA(key, "id") == 0)             { int v; if (ReadInt(pp, &v))    id = v;  else SkipValue(pp); }
        else if (lstrcmpA(key, "latitude") == 0)  { int v; if (ReadFixed3(pp, &v)) lat = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "longitude") == 0) { int v; if (ReadFixed3(pp, &v)) lon = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "name") == 0)      { ReadString(pp, name, sizeof(name)); haveName = name[0] != 0; }
        else if (lstrcmpA(key, "hasLibrary") == 0) {
            SkipWS(pp);
            if      (**pp == 't') { lib = 1; SkipValue(pp); }
            else if (**pp == 'f') { lib = 0; SkipValue(pp); }
            else { int v; if (ReadFixed3(pp, &v)) lib = v ? 1 : 0; else SkipValue(pp); }
        }
        else SkipValue(pp);

        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }

    if (id < 0 || id >= CITYDB_MAX) return;   // 게임에 없는 번호는 무시
    // 위경도는 둘 다 있어야 쓴다. 하나라도 없으면 그 도시는 마커를 안 찍는다.
    if (lat == NOVAL || lon == NOVAL ||
        lat < -90000 || lat > 90000 || lon < -180000 || lon > 180000) {
        g_city[id].lonRaw = CITYDB_NONE;
        g_city[id].latRaw = CITYDB_NONE;
    } else {
        // 1/1000 도 -> 게임 원본 단위. 한 눈금이 0.009도라 9로 나누면 딱 떨어진다.
        g_city[id].lonRaw = 20000 + lon / 9;
        g_city[id].latRaw = 10000 - lat / 9;
    }
    if (lib >= 0) g_city[id].lib = lib;
    if (haveName)
        MultiByteToWideChar(CP_UTF8, 0, name, -1, g_city[id].name,
                            (int)(sizeof(g_city[id].name) / sizeof(wchar_t)));
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
    lstrcatW(out, L"cities.json");
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

void CityDb_Load(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    char* buf;
    const char* p;
    int i, n = 0;

    // 1) 구운 표를 깔아 둔다. cities.json 이 없거나 깨져 있어도 지도는 나온다.
    for (i = 0; i < CITYDB_MAX; i++) {
        g_city[i].lonRaw = kCityLonRaw[i];
        g_city[i].latRaw = kCityLatRaw[i];
        g_city[i].lib    = kCities[i].lib ? 1 : 0;
        lstrcpynW(g_city[i].name, kCities[i].name,
                  (int)(sizeof(g_city[i].name) / sizeof(wchar_t)));
    }
    g_fromFile = 0;

    // 2) 파일이 있으면 그 값으로 덮어쓴다.
    JsonPath(hinst, path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) return;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                                   // UTF-8 BOM
    SkipWS(&p);
    if (*p != '[') { HeapFree(GetProcessHeap(), 0, buf); return; }
    p++;
    for (;;) {
        SkipWS(&p);
        if (*p == ']' || !*p) break;
        if (*p == '{') { ParseCity(&p); n++; }
        else SkipValue(&p);
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (n > 0) g_fromFile = 1;
    OutputDebugStringW(g_fromFile ? L"[WorldMapKR] cities.json 로드." : L"[WorldMapKR] cities.json 비어 있음.");
}
