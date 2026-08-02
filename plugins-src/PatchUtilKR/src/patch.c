#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>   // ShellExecuteW — patches.json 을 기본 편집기로 열기
#include "patch.h"

// PatchUtilKR — cds-helper ExePatch(정적 파일 헥스 패치)의 런타임 메모리판.
//  patches.json(= cds-helper 커스텀 패치 스키마) → 파일오프셋을 로드된 cds_95 모듈의
//  가상주소로 변환 → VirtualProtect+메모리 쓰기로 적용/해제. 원본은 로드 시 메모리에서 스냅샷.

#define MAX_ADDRS   32
#define MAX_PATCHES 256
#define MAX_REGIONS 8
#define MAX_REGION_LEN 512

// 바이트열 패치 한 구간. 값 하나를 쓰는 기존 방식으로는 자리마다 값이 다른
// 코드 덩어리를 표현할 수 없어서(Addresses[] 는 전부 같은 값을 받는다) 따로 뒀다.
typedef struct {
    unsigned int  off;                       // 파일 오프셋
    int           len;
    unsigned char orig[MAX_REGION_LEN];
    unsigned char patched[MAX_REGION_LEN];
    int           mapped;
} Region;
#define ID_PATCH_OPEN 0xB500u     // "파일>패치" 메뉴 커맨드 (KR 예약대역 0xB000~0xCFFF)

// "파일" 드롭다운의 "패치" 항목 노출 스위치.
// 0 이면 메뉴 감시 스레드를 아예 띄우지 않아 게임 창 서브클래싱도 하지 않는다.
#define PATCHKR_SHOW_MENU 1

typedef struct {
    wchar_t      name[128];
    wchar_t      desc[256];
    unsigned int addrs[MAX_ADDRS];   // 파일 오프셋들
    int          naddr;
    int          hasArray;           // Addresses[] 로 채워졌는지
    int          byteSize;           // 1/2/4
    long long    value;              // number형 기록값
    int          isToggle;           // Type=="toggle"
    long long    originalValue;      // toggle OFF 기록값
    long long    patchedValue;       // toggle ON 기록값
    unsigned char snap[MAX_ADDRS][4];// 로드 시 원본 메모리 바이트
    int          mapped[MAX_ADDRS];  // 오프셋→메모리 변환 성공 여부
    int          applied;            // 현재 적용 상태
    Region       regs[MAX_REGIONS];  // Regions[] 로 적은 바이트열 패치
    int          nreg;
} Patch;

static HINSTANCE g_hinst = NULL;
static Patch     g_patches[MAX_PATCHES];
static int       g_npatch = 0;

// ------------------------------------------------------------------ 로그
static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// ------------------------------------------------------------------ PE: 파일오프셋 → 메모리주소
static BYTE*              g_base = NULL;
static IMAGE_NT_HEADERS*  g_nt   = NULL;

static void InitPE(void)
{
    IMAGE_DOS_HEADER* dos;
    g_base = (BYTE*)GetModuleHandleW(NULL);   // 메인 exe(cds_95) 로드 베이스
    dos = (IMAGE_DOS_HEADER*)g_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { g_nt = NULL; return; }
    g_nt = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    if (g_nt->Signature != IMAGE_NT_SIGNATURE) g_nt = NULL;
}

// cds-helper VaToFileOffset 의 역변환: 파일오프셋이 속한 섹션을 찾아 RVA로, 로드베이스에 더한다.
static BYTE* OffToMem(unsigned int off)
{
    IMAGE_SECTION_HEADER* s;
    int n, i;
    if (!g_nt) return NULL;
    if (off < g_nt->OptionalHeader.SizeOfHeaders) return g_base + off;  // 헤더 영역은 오프셋==RVA
    s = IMAGE_FIRST_SECTION(g_nt);
    n = g_nt->FileHeader.NumberOfSections;
    for (i = 0; i < n; i++) {
        DWORD rp = s[i].PointerToRawData;
        DWORD rs = s[i].SizeOfRawData;
        if (rs && off >= rp && off < rp + rs)
            return g_base + s[i].VirtualAddress + (off - rp);
    }
    return NULL;
}

// ------------------------------------------------------------------ 메모리 쓰기 / 값 변환
static BOOL WriteMem(BYTE* mem, const BYTE* bytes, int n)
{
    DWORD oldp;
    int i;
    if (!VirtualProtect(mem, n, PAGE_EXECUTE_READWRITE, &oldp)) return FALSE;
    for (i = 0; i < n; i++) mem[i] = bytes[i];
    VirtualProtect(mem, n, oldp, &oldp);
    FlushInstructionCache(GetCurrentProcess(), mem, n);
    return TRUE;
}

// "81 BC 24" / "81BC24" 를 바이트열로. 담은 개수를 돌려준다.
static int HexBytes(const char* s, unsigned char* out, int cap)
{
    int n = 0, hi = -1;
    for (; *s && n < cap; s++) {
        int v;
        if (*s >= '0' && *s <= '9') v = *s - '0';
        else if (*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') v = *s - 'A' + 10;
        else continue;                       // 공백/구분자는 건너뛴다
        if (hi < 0) hi = v;
        else { out[n++] = (unsigned char)((hi << 4) | v); hi = -1; }
    }
    return n;
}

static void ValueBytes(long long v, int n, BYTE* out)   // 리틀엔디안 (cds-helper BitConverter 와 동일)
{
    int i;
    for (i = 0; i < n; i++) out[i] = (BYTE)((v >> (8 * i)) & 0xFF);
}

static void SnapshotPatch(Patch* p)
{
    int i, b;
    for (i = 0; i < p->nreg; i++) p->regs[i].mapped = OffToMem(p->regs[i].off) ? 1 : 0;
    for (i = 0; i < p->naddr; i++) {
        BYTE* m = OffToMem(p->addrs[i]);
        if (m) { for (b = 0; b < p->byteSize; b++) p->snap[i][b] = m[b]; p->mapped[i] = 1; }
        else   { p->mapped[i] = 0; }
    }
}

// 토글 패치는 쓰기 전에 현재 바이트가 OriginalValue / PatchedValue 중 하나인지 확인한다.
// patches.json 의 주소는 특정 exe 를 기준으로 뽑은 값이라, 판본이 다른 exe 에 그대로 쓰면
// 아무 상관 없는 바이트를 덮어써서 게임을 망가뜨린다. 하나라도 어긋나면 그 항목 전체를
// 건너뛴다 — 일부만 적용되는 쪽이 더 위험하다.
// (number 형은 대조할 원본값이 json 에 없어서 검사하지 않는다.)
static BOOL ToggleTargetsLookRight(const Patch* p, int* badIdx)
{
    BYTE a[4], b[4];
    int i, k;
    // 바이트열 구간: 지금 바이트가 원본이거나 패치본이어야 한다.
    for (i = 0; i < p->nreg; i++) {
        const Region* r = &p->regs[i];
        BYTE* m;
        int k, okA = 1, okB = 1;
        if (!r->mapped) continue;
        m = OffToMem(r->off);
        if (!m) continue;
        for (k = 0; k < r->len; k++) {
            if (m[k] != r->orig[k])    okA = 0;
            if (m[k] != r->patched[k]) okB = 0;
        }
        if (!okA && !okB) { if (badIdx) *badIdx = -(i + 1); return FALSE; }
    }
    if (!p->isToggle) return TRUE;
    ValueBytes(p->originalValue, p->byteSize, a);
    ValueBytes(p->patchedValue,  p->byteSize, b);
    for (i = 0; i < p->naddr; i++) {
        BYTE* m;
        int okA = 1, okB = 1;
        if (!p->mapped[i]) continue;
        m = OffToMem(p->addrs[i]);
        if (!m) continue;
        for (k = 0; k < p->byteSize; k++) {
            if (m[k] != a[k]) okA = 0;
            if (m[k] != b[k]) okB = 0;
        }
        if (!okA && !okB) { if (badIdx) *badIdx = i; return FALSE; }
    }
    return TRUE;
}

BOOL Patch_SetApplied(int idx, BOOL on)
{
    Patch* p = &g_patches[idx];
    int i, bad = 0;

    if (!ToggleTargetsLookRight(p, &bad)) {
        wchar_t msg[640];
        LogW(L"[PatchUtilKR] 적용 거부: %s — 오프셋 0x%X 의 현재 값이 원본/패치값 어느 쪽도 아님",
             p->name[0] ? p->name : L"(무명)", p->addrs[bad]);
        wsprintfW(msg,
            L"[%s]\n\n"
            L"파일오프셋 0x%X 의 현재 값이 OriginalValue 도 PatchedValue 도 아닙니다.\n"
            L"이 patches.json 은 지금 실행 중인 cds_95.exe 와 다른 판본을 기준으로 만들어졌을 수 있습니다.\n\n"
            L"엉뚱한 곳을 덮어쓰지 않도록 이 항목은 적용하지 않았습니다.",
            p->name[0] ? p->name : L"(무명)", p->addrs[bad]);
        MessageBoxW(NULL, msg, L"PatchUtilKR — 주소가 맞지 않습니다", MB_OK | MB_ICONWARNING);
        return FALSE;
    }

    for (i = 0; i < p->nreg; i++) {
        Region* r = &p->regs[i];
        BYTE* m;
        if (!r->mapped) continue;
        m = OffToMem(r->off);
        if (m) WriteMem(m, on ? r->patched : r->orig, r->len);
    }

    for (i = 0; i < p->naddr; i++) {
        BYTE* m;
        BYTE  bytes[4];
        if (!p->mapped[i]) continue;
        m = OffToMem(p->addrs[i]);
        if (!m) continue;
        if (on) {
            ValueBytes(p->isToggle ? p->patchedValue : p->value, p->byteSize, bytes);
            WriteMem(m, bytes, p->byteSize);
        } else if (p->isToggle) {
            ValueBytes(p->originalValue, p->byteSize, bytes);
            WriteMem(m, bytes, p->byteSize);
        } else {
            WriteMem(m, p->snap[i], p->byteSize);   // number형 해제 = 원본 스냅샷 복원
        }
    }
    p->applied = on;
    return TRUE;
}

// ------------------------------------------------------------------ 미니 JSON 파서 (cds-helper 출력 전용)
static void Utf8ToW(const char* s, wchar_t* out, int outcch)
{
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, outcch) <= 0) out[0] = 0;
}

static BOOL ParseHex(const char* s, unsigned int* out)
{
    unsigned int v = 0; int any = 0;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        unsigned int d;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else break;
        v = v * 16 + d; any = 1;
    }
    if (!any) return FALSE;
    *out = v; return TRUE;
}

static void SkipWS(const char** pp)
{
    const char* p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    *pp = p;
}

static void SkipString(const char** pp)
{
    const char* p = *pp;
    if (*p != '"') return;
    p++;
    while (*p && *p != '"') { if (*p == '\\' && p[1]) p += 2; else p++; }
    if (*p == '"') p++;
    *pp = p;
}

static void SkipValue(const char** pp)
{
    const char* p;
    SkipWS(pp); p = *pp;
    if (*p == '"') { SkipString(pp); return; }
    if (*p == '{') {
        (*pp)++;
        for (;;) {
            SkipWS(pp);
            if (**pp == '}') { (*pp)++; break; }
            SkipString(pp); SkipWS(pp);
            if (**pp == ':') (*pp)++;
            SkipValue(pp); SkipWS(pp);
            if (**pp == ',') { (*pp)++; continue; }
            if (**pp == '}') { (*pp)++; }
            break;
        }
        return;
    }
    if (*p == '[') {
        (*pp)++;
        SkipWS(pp);
        if (**pp == ']') { (*pp)++; return; }
        for (;;) {
            SkipValue(pp); SkipWS(pp);
            if (**pp == ',') { (*pp)++; continue; }
            if (**pp == ']') { (*pp)++; }
            break;
        }
        return;
    }
    while (**pp && **pp != ',' && **pp != '}' && **pp != ']') (*pp)++;   // number/true/false/null
}

static void ParseStringInto(const char** pp, char* out, int outsz)
{
    const char* p = *pp;
    int oi = 0;
    if (*p != '"') { out[0] = 0; return; }
    p++;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
                case '"': c = '"'; break;   case '\\': c = '\\'; break;  case '/': c = '/'; break;
                case 'b': c = '\b'; break;  case 'f': c = '\f'; break;   case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;  case 't': c = '\t'; break;
                case 'u': {
                    unsigned int cp = 0; int k;
                    for (k = 0; k < 4 && *p; k++) {
                        char h = *p++;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned int)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned int)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned int)(h - 'A' + 10);
                    }
                    if (cp < 0x80) { if (oi < outsz - 1) out[oi++] = (char)cp; }
                    else if (cp < 0x800) { if (oi < outsz - 2) { out[oi++] = (char)(0xC0 | (cp >> 6)); out[oi++] = (char)(0x80 | (cp & 0x3F)); } }
                    else { if (oi < outsz - 3) { out[oi++] = (char)(0xE0 | (cp >> 12)); out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[oi++] = (char)(0x80 | (cp & 0x3F)); } }
                    continue;
                }
                default: c = e; break;
            }
        }
        if (oi < outsz - 1) out[oi++] = c;
    }
    if (*p == '"') p++;
    out[oi] = 0;
    *pp = p;
}

static long long ParseNumber(const char** pp)
{
    const char* p = *pp;
    long long sign = 1, val = 0;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
    *pp = p;
    return sign * val;
}

static void ParseAddresses(const char** pp, Patch* pt)
{
    if (**pp != '[') { SkipValue(pp); return; }
    (*pp)++;
    pt->naddr = 0;
    for (;;) {
        SkipWS(pp);
        if (**pp == ']') { (*pp)++; break; }
        if (**pp == '"') {
            char tmp[64]; unsigned int off;
            ParseStringInto(pp, tmp, sizeof(tmp));
            if (ParseHex(tmp, &off) && pt->naddr < MAX_ADDRS) pt->addrs[pt->naddr++] = off;
        } else SkipValue(pp);
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
        if (**pp == ']') { (*pp)++; break; }
        break;
    }
    pt->hasArray = 1;
}

// "Regions": [ { "Address": "0x8BFA9", "OriginalBytes": "81 BC ..", "PatchedBytes": "81 3D .." }, ... ]
static void ParseRegions(const char** pp, Patch* pt)
{
    if (**pp != '[') { SkipValue(pp); return; }
    (*pp)++;
    for (;;) {
        SkipWS(pp);
        if (**pp == ']') { (*pp)++; break; }
        if (**pp == '{') {
            Region r;
            char addr[64];
            int no = 0, np = 0;
            ZeroMemory(&r, sizeof(r));
            addr[0] = 0;
            (*pp)++;
            for (;;) {
                char key[48];
                SkipWS(pp);
                if (**pp == '}') { (*pp)++; break; }
                if (**pp != '"') { if (!**pp) return; (*pp)++; continue; }
                ParseStringInto(pp, key, sizeof(key));
                SkipWS(pp);
                if (**pp == ':') (*pp)++;
                SkipWS(pp);
                if (lstrcmpA(key, "Address") == 0) ParseStringInto(pp, addr, sizeof(addr));
                else if (lstrcmpA(key, "OriginalBytes") == 0) {
                    char h[MAX_REGION_LEN * 3 + 8];
                    ParseStringInto(pp, h, sizeof(h));
                    no = HexBytes(h, r.orig, MAX_REGION_LEN);
                } else if (lstrcmpA(key, "PatchedBytes") == 0) {
                    char h[MAX_REGION_LEN * 3 + 8];
                    ParseStringInto(pp, h, sizeof(h));
                    np = HexBytes(h, r.patched, MAX_REGION_LEN);
                } else SkipValue(pp);
                SkipWS(pp);
                if (**pp == ',') { (*pp)++; continue; }
            }
            // 두 바이트열 길이가 다르면 어느 쪽이 맞는지 알 수 없어 통째로 버린다.
            if (addr[0] && no > 0 && no == np && pt->nreg < MAX_REGIONS &&
                ParseHex(addr, &r.off)) {
                r.len = no;
                pt->regs[pt->nreg++] = r;
            }
        } else SkipValue(pp);
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
        if (**pp == ']') { (*pp)++; break; }
        break;
    }
}

static BOOL ParseObject(const char** pp, Patch* pt)
{
    char single[64];
    ZeroMemory(pt, sizeof(*pt));
    pt->byteSize = 1;
    single[0] = 0;
    SkipWS(pp);
    if (**pp != '{') return FALSE;
    (*pp)++;
    for (;;) {
        char key[48];
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') return FALSE;
        ParseStringInto(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);
        if (lstrcmpA(key, "Name") == 0)              { char u[256]; ParseStringInto(pp, u, sizeof(u)); Utf8ToW(u, pt->name, 128); }
        else if (lstrcmpA(key, "Description") == 0)  { char u[512]; ParseStringInto(pp, u, sizeof(u)); Utf8ToW(u, pt->desc, 256); }
        else if (lstrcmpA(key, "Address") == 0)      { ParseStringInto(pp, single, sizeof(single)); }
        else if (lstrcmpA(key, "Addresses") == 0)    { ParseAddresses(pp, pt); }
        else if (lstrcmpA(key, "Regions") == 0)      { ParseRegions(pp, pt); }
        else if (lstrcmpA(key, "ByteSize") == 0)     { pt->byteSize = (int)ParseNumber(pp); }
        else if (lstrcmpA(key, "Value") == 0)        { pt->value = ParseNumber(pp); }
        else if (lstrcmpA(key, "OriginalValue") == 0){ pt->originalValue = ParseNumber(pp); }
        else if (lstrcmpA(key, "PatchedValue") == 0) { pt->patchedValue = ParseNumber(pp); }
        else if (lstrcmpA(key, "Type") == 0)         { char t[16]; ParseStringInto(pp, t, sizeof(t)); pt->isToggle = (lstrcmpA(t, "toggle") == 0); }
        else                                         { SkipValue(pp); }
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
        if (**pp == '}') { (*pp)++; break; }
        break;
    }
    if (!pt->hasArray && single[0]) {           // 단일 Address (하위호환) — Addresses[] 없을 때만
        unsigned int off;
        if (ParseHex(single, &off)) { pt->addrs[0] = off; pt->naddr = 1; }
    }
    if (pt->byteSize != 1 && pt->byteSize != 2 && pt->byteSize != 4) pt->byteSize = 1;
    return pt->naddr > 0 || pt->nreg > 0;
}

static void ParseJson(const char* buf)
{
    const char* p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3; // BOM
    SkipWS(&p);
    if (*p != '[') { LogW(L"[PatchUtilKR] patches.json: 최상위 '[' 아님"); return; }
    p++;
    for (;;) {
        SkipWS(&p);
        if (*p == ']' || *p == 0) break;
        if (*p == '{') {
            Patch pt;
            if (ParseObject(&p, &pt) && g_npatch < MAX_PATCHES) g_patches[g_npatch++] = pt;
        } else SkipValue(&p);
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
        break;
    }
}

// ------------------------------------------------------------------ 로드
static char* ReadWholeFile(const wchar_t* path)
{
    HANDLE f;
    DWORD sz, rd = 0;
    char* buf;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(f, NULL);
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!buf) { CloseHandle(f); return NULL; }
    ReadFile(f, buf, sz, &rd, NULL);
    buf[rd] = 0;
    CloseHandle(f);
    return buf;
}

static void PatchesPath(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);      // ...\CDS95Util\PatchUtilKR.plugin
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    lstrcatW(out, L"patches.json");
}

void PatchCore_Load(void)
{
    wchar_t path[MAX_PATH];
    char* buf;
    int i;
    g_npatch = 0;
    if (!g_nt) InitPE();
    PatchesPath(path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) { LogW(L"[PatchUtilKR] patches.json 없음: %s", path); return; }
    ParseJson(buf);
    HeapFree(GetProcessHeap(), 0, buf);
    LogW(L"[PatchUtilKR] %d개 패치 로드: %s", g_npatch, path);
    for (i = 0; i < g_npatch; i++) {
        Patch* p = &g_patches[i];
        BYTE* m0;
        SnapshotPatch(p);
        // 로드 시 현재 메모리가 이미 적용값(toggle=PatchedValue, number=Value)과 같으면
        // 체크(ON)로 표시해 창이 실제 상태를 반영하게 한다. (snap[0]=로드시점 현재 메모리)
        if (p->nreg > 0) {                     // 바이트열 패치: 첫 구간이 패치본과 같으면 ON
            BYTE* m = p->regs[0].mapped ? OffToMem(p->regs[0].off) : NULL;
            BOOL match = FALSE;
            if (m) {
                int k; match = TRUE;
                for (k = 0; k < p->regs[0].len; k++)
                    if (m[k] != p->regs[0].patched[k]) { match = FALSE; break; }
            }
            p->applied = match;
        } else if (p->mapped[0]) {
            BYTE tb[4]; int b; BOOL match = TRUE;
            ValueBytes(p->isToggle ? p->patchedValue : p->value, p->byteSize, tb);
            for (b = 0; b < p->byteSize; b++) if (p->snap[0][b] != tb[b]) { match = FALSE; break; }
            p->applied = match;
        }
        m0 = p->naddr ? OffToMem(p->addrs[0]) : NULL;
        LogW(L"  [%d] %s off=0x%X x%d %s VA=0x%08X mapped=%d cur=0x%02X applied=%d",
             i, p->name[0] ? p->name : L"(무명)",
             p->naddr ? p->addrs[0] : 0, p->byteSize,
             p->isToggle ? L"toggle" : L"value",
             (unsigned int)(UINT_PTR)m0, p->naddr ? p->mapped[0] : 0,
             (m0 && p->mapped[0]) ? m0[0] : 0, p->applied);
    }
}

// ================================================================== UI (ListView 창)
#define WC_PATCH   L"PatchUtilKR_Window"
#define ID_LIST    1001
#define ID_RELOAD  1002
#define ID_OPEN    1003

// patches.json 을 기본 편집기로 연다. .json 에 연결 프로그램이 없는 PC 가 흔해서
// ShellExecute 가 실패하면(반환값 <= 32) 메모장으로 떨어뜨린다.
// 고친 뒤에는 옆의 "다시 읽기" 를 누르면 반영된다.
static void OpenPatchesFile(HWND owner)
{
    wchar_t path[MAX_PATH], cmd[MAX_PATH + 32];
    HINSTANCE r;
    PatchesPath(path, MAX_PATH);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(owner, path, L"patches.json 을 찾을 수 없습니다", MB_OK | MB_ICONWARNING);
        return;
    }
    r = ShellExecuteW(owner, L"open", path, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        STARTUPINFOW si; PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        wsprintfW(cmd, L"notepad.exe \"%s\"", path);
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        } else {
            MessageBoxW(owner, path, L"파일을 열지 못했습니다", MB_OK | MB_ICONWARNING);
        }
    }
}

static HWND g_win = NULL, g_list = NULL;
static BOOL g_populating = FALSE;

static void StateText(Patch* p, wchar_t* buf)
{
    if (p->isToggle)
        wsprintfW(buf, p->applied ? L"ON=%d" : L"OFF=%d", (int)(p->applied ? p->patchedValue : p->originalValue));
    else
        wsprintfW(buf, p->applied ? L"적용 %d" : L"원본", (int)p->value);
}

static void FillList(void)
{
    int i;
    g_populating = TRUE;
    ListView_DeleteAllItems(g_list);
    for (i = 0; i < g_npatch; i++) {
        Patch* p = &g_patches[i];
        LVITEMW it;
        wchar_t a[64], bs[8], st[48];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.iSubItem = 0;
        it.pszText = p->name[0] ? p->name : L"(무명)";
        it.lParam = i;
        ListView_InsertItem(g_list, &it);
        if (p->naddr > 1) wsprintfW(a, L"0x%X 외%d", p->addrs[0], p->naddr - 1);
        else              wsprintfW(a, L"0x%X", p->naddr ? p->addrs[0] : 0);
        if (p->naddr && !p->mapped[0]) lstrcatW(a, L" (X)");
        ListView_SetItemText(g_list, i, 1, a);
        wsprintfW(bs, L"%d", p->byteSize);
        ListView_SetItemText(g_list, i, 2, bs);
        ListView_SetItemText(g_list, i, 3, p->isToggle ? L"토글" : L"값");
        StateText(p, st);
        ListView_SetItemText(g_list, i, 4, st);
        ListView_SetItemText(g_list, i, 5, p->desc);
        ListView_SetCheckState(g_list, i, p->applied);
    }
    g_populating = FALSE;
}

static LRESULT CALLBACK PatchProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
        case WM_CREATE: {
            const wchar_t* titles[6] = { L"이름", L"주소", L"바이트", L"종류", L"상태", L"설명" };
            int widths[6] = { 180, 130, 52, 52, 90, 240 };
            LVCOLUMNW c;
            int i;
            g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                        0, 0, 10, 10, h, (HMENU)ID_LIST, g_hinst, NULL);
            ListView_SetExtendedListViewStyle(g_list,
                        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            ZeroMemory(&c, sizeof(c));
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            for (i = 0; i < 6; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }
            CreateWindowExW(0, L"BUTTON", L"patches.json 다시 읽기",
                        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"patches.json 열기",
                        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)ID_OPEN, g_hinst, NULL);
            FillList();
            return 0;
        }
        case WM_SIZE: {
            int cw = LOWORD(l), ch = HIWORD(l), bh = 30;
            MoveWindow(g_list, 0, 0, cw, ch - bh, TRUE);
            MoveWindow(GetDlgItem(h, ID_RELOAD), 6, ch - bh + 3, 180, 24, TRUE);
            MoveWindow(GetDlgItem(h, ID_OPEN), 192, ch - bh + 3, 150, 24, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == ID_RELOAD) {
                PatchCore_Load();   // 파일 재파싱 + 현재 메모리로 적용상태 재동기화(적용 중인 패치는 유지)
                FillList();
            } else if (LOWORD(w) == ID_OPEN) {
                OpenPatchesFile(h);
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR* nh = (NMHDR*)l;
            if (nh->idFrom == ID_LIST && nh->code == LVN_ITEMCHANGED && !g_populating) {
                NMLISTVIEW* nm = (NMLISTVIEW*)l;
                if (nm->uChanged & LVIF_STATE) {
                    BOOL was = ((nm->uOldState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                    BOOL is  = ((nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                    if (was != is && nm->iItem >= 0 && nm->iItem < g_npatch) {
                        Patch* p = &g_patches[nm->iItem];
                        if (is && (p->naddr == 0 || !p->mapped[0])) {
                            MessageBeep(MB_ICONWARNING);        // 매핑 실패 패치는 적용 불가
                            g_populating = TRUE;
                            ListView_SetCheckState(g_list, nm->iItem, FALSE);
                            g_populating = FALSE;
                        } else if (!Patch_SetApplied(nm->iItem, is)) {
                            // 주소 검증에 걸려 적용을 거부했다 — 체크 표시를 실제 상태로 되돌린다.
                            g_populating = TRUE;
                            ListView_SetCheckState(g_list, nm->iItem, p->applied);
                            g_populating = FALSE;
                        } else {
                            wchar_t st[48];
                            StateText(p, st);
                            ListView_SetItemText(g_list, nm->iItem, 4, st);
                            LogW(L"[PatchUtilKR] %s %s", p->name[0] ? p->name : L"(무명)", is ? L"적용" : L"해제");
                        }
                    }
                }
            }
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);   // 닫아도 적용상태는 유지 (창만 숨김)
            return 0;
        case WM_NCDESTROY:
            g_win = NULL; g_list = NULL;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void PatchWin_Show(HWND owner)
{
    static BOOL reg = FALSE;
    INITCOMMONCONTROLSEX ic;
    if (g_win) { ShowWindow(g_win, SW_SHOW); SetForegroundWindow(g_win); return; }
    ic.dwSize = sizeof(ic); ic.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&ic);
    if (!reg) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = PatchProc;
        wc.hInstance = g_hinst;
        wc.lpszClassName = WC_PATCH;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        reg = TRUE;
    }
    g_win = CreateWindowExW(0, WC_PATCH, L"PatchUtilKR — 패치",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 440,
                owner, NULL, g_hinst, NULL);
    ShowWindow(g_win, SW_SHOW);
    UpdateWindow(g_win);
}

// ================================================================== 메뉴 설치 + 서브클래싱
static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_PATCH_OPEN) { PatchWin_Show(h); return 0; }
    if (m == WM_NCDESTROY) {
        if (op) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
        return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0; (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}

static BOOL HasOurMenu(HMENU menu)
{
    int n = GetMenuItemCount(menu), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(menu, (UINT)i, s, 64, MF_BYPOSITION) > 0 && lstrcmpW(s, L"패치") == 0) return TRUE;
    return FALSE;
}

// 실제 라벨은 "파일 (&F)" 처럼 니모닉이 붙으므로 접두어로 매칭한다.
static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i; WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0] == L'파' && s[1] == L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

static BOOL FileMenuHasPluginItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        UINT id = GetMenuItemID(m, (UINT)i);
        if (id != (UINT)-1 && id >= 0xB000 && id <= 0xCFFF) return TRUE;
    }
    return FALSE;
}

static BOOL CollapseSeparators(HMENU m)
{
    BOOL changed = FALSE; int i;
    for (i = GetMenuItemCount(m) - 1; i > 0; i--) {
        UINT a = GetMenuState(m, (UINT)i, MF_BYPOSITION);
        UINT b = GetMenuState(m, (UINT)(i - 1), MF_BYPOSITION);
        if ((a & MF_SEPARATOR) && (b & MF_SEPARATOR)) { RemoveMenu(m, (UINT)i, MF_BYPOSITION); changed = TRUE; }
    }
    return changed;
}

static DWORD WINAPI MenuThread(LPVOID pv)
{
    (void)pv;
    OutputDebugStringW(L"[PatchUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!HasOurMenu(target)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(target, MF_STRING, ID_PATCH_OPEN, L"패치");
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[PatchUtilKR] 패치 menu installed.");
            }
            if (fileMenu && CollapseSeparators(fileMenu)) DrawMenuBar(g_gameHwnd);
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[PatchUtilKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void PatchKR_Init(HINSTANCE hinst)
{
    g_hinst = hinst;
    InitPE();
    PatchCore_Load();
#if PATCHKR_SHOW_MENU
    {
        HANDLE t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
#else
    LogW(L"[PatchUtilKR] 메뉴 비노출 (PATCHKR_SHOW_MENU=0) — patches.json 파싱만 하고 창은 띄우지 않습니다.");
#endif
}
