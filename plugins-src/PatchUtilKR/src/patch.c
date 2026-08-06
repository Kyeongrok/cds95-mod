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
#define MAX_CHOICES 24              // 값 선택형 항목의 후보 개수 상한

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
    // 이 패치가 어느 파일에서 왔나. 빈 값이면 CDS95Util\patches.json 이고,
    // 그 밖은 mods\<만든이>\patches\<파일>.json 에서 온 것이다(모드 이름을 적는다).
    wchar_t      src[64];
    unsigned int addrs[MAX_ADDRS];   // 파일 오프셋들
    int          naddr;
    int          hasArray;           // Addresses[] 로 채워졌는지
    int          byteSize;           // 1/2/4
    long long    value;              // number형 기록값
    int          isToggle;           // Type=="toggle"
    int          autoApply;          // AutoApply==true — 창을 열기 전, 플러그인 로드 때 바로 적용
    // Type=="choice" — 켜고 끄는 게 아니라 정해진 값 중 하나를 골라 쓰는 항목.
    // (입출항 일수처럼 "몇 일" 같은 수치는 토글로는 표현이 안 된다.)
    int          isChoice;
    int          choices[MAX_CHOICES];
    int          nchoice;
    int          vmin, vmax;         // 허용 범위. Choices 가 없으면 이 범위에서 목록을 만든다
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

// ---- 값 선택형(choice) ----
// 지금 메모리에 들어 있는 값. 못 읽으면 -1.
static int Patch_ReadValue(const Patch* p)
{
    BYTE* m;
    int v = 0, k;
    if (!p->naddr || !p->mapped[0]) return -1;
    m = OffToMem(p->addrs[0]);
    if (!m) return -1;
    for (k = 0; k < p->byteSize; k++) v |= (int)m[k] << (8 * k);
    return v;
}

// 고른 값을 모든 주소에 쓴다. 범위 밖이면 거절.
static BOOL Patch_SetValue(Patch* p, int v)
{
    int i;
    if (v < p->vmin || v > p->vmax) return FALSE;
    for (i = 0; i < p->naddr; i++) {
        BYTE* m;
        BYTE bytes[4];
        if (!p->mapped[i]) continue;
        m = OffToMem(p->addrs[i]);
        if (!m) continue;
        ValueBytes(v, p->byteSize, bytes);
        WriteMem(m, bytes, p->byteSize);
    }
    LogW(L"[PatchUtilKR] %s = %d", p->name[0] ? p->name : L"(무명)", v);
    return TRUE;
}

// 고를 값 목록. Choices 를 적었으면 그대로, 없으면 vmin~vmax 를 고르게 나눠 만든다.
static int Patch_BuildChoices(const Patch* p, int* out)
{
    int n = 0, i, step, v;
    if (p->nchoice > 0) {
        for (i = 0; i < p->nchoice; i++)
            if (p->choices[i] >= p->vmin && p->choices[i] <= p->vmax) out[n++] = p->choices[i];
        return n;
    }
    step = (p->vmax - p->vmin) / (MAX_CHOICES - 1);
    if (step < 1) step = 1;
    for (v = p->vmin; v <= p->vmax && n < MAX_CHOICES; v += step) out[n++] = v;
    return n;
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
        else if (lstrcmpA(key, "Type") == 0)         { char t[16]; ParseStringInto(pp, t, sizeof(t));
                                                       pt->isToggle = (lstrcmpA(t, "toggle") == 0);
                                                       pt->isChoice = (lstrcmpA(t, "choice") == 0); }
        else if (lstrcmpA(key, "Min") == 0)          { pt->vmin = (int)ParseNumber(pp); }
        else if (lstrcmpA(key, "Max") == 0)          { pt->vmax = (int)ParseNumber(pp); }
        else if (lstrcmpA(key, "Choices") == 0)      {   // [1, 3, 5, 10] — 고를 값 목록
            SkipWS(pp);
            if (**pp != '[') { SkipValue(pp); }
            else {
                (*pp)++;
                for (;;) {
                    SkipWS(pp);
                    if (**pp == ']') { (*pp)++; break; }
                    if (!**pp) break;
                    if (pt->nchoice < MAX_CHOICES) pt->choices[pt->nchoice++] = (int)ParseNumber(pp);
                    else SkipValue(pp);
                    SkipWS(pp);
                    if (**pp == ',') (*pp)++;
                }
            }
        }
        else if (lstrcmpA(key, "AutoApply") == 0)    { SkipWS(pp); pt->autoApply = (**pp == 't'); SkipValue(pp); }
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
    // 값 선택형인데 Min/Max 를 안 적었으면 Choices 범위, 그것도 없으면 바이트 폭 전체로 잡는다.
    if (pt->isChoice && pt->vmin == 0 && pt->vmax == 0) {
        if (pt->nchoice > 0) {
            int i;
            pt->vmin = pt->vmax = pt->choices[0];
            for (i = 1; i < pt->nchoice; i++) {
                if (pt->choices[i] < pt->vmin) pt->vmin = pt->choices[i];
                if (pt->choices[i] > pt->vmax) pt->vmax = pt->choices[i];
            }
        } else {
            pt->vmin = 0;
            pt->vmax = (pt->byteSize >= 4) ? 0x7FFFFFFF : (1 << (8 * (pt->byteSize ? pt->byteSize : 1))) - 1;
        }
    }
    return pt->naddr > 0 || pt->nreg > 0;
}

static void ParseJson(const char* buf)
{
    const char* p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3; // BOM
    SkipWS(&p);
    // 최상위가 [ 면 여러 개, { 면 하나짜리 파일이다. 패치 하나를 파일 하나로 올리는 사람이
    // 많을 테니 둘 다 받는다.
    if (*p == '{') {
        Patch pt;
        if (ParseObject(&p, &pt) && g_npatch < MAX_PATCHES) g_patches[g_npatch++] = pt;
        return;
    }
    if (*p != '[') { LogW(L"[PatchUtilKR] json: 최상위가 [ 도 { 도 아님"); return; }
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
    UpToDataDir(out);
    lstrcatW(out, L"patches.json");
}

// mods\<모드>\patch\*.json 을 전부 읽어 뒤에 잇는다.
//
// 패치를 만든 사람마다 .json 하나씩 올리고, 쓰는 사람은 그 파일을 모드 폴더에 넣기만 하면
// 되게 하려는 것이다. 기본 patches.json 은 그대로 두고 뒤에 덧붙이므로, 넣고 빼는 것이
// 기본 목록을 건드리지 않는다. 같은 이름이 겹쳐도 출처가 다르면 따로 선다.
static void LoadModPatches(void)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH], modDir[MAX_PATH], file[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t* q;
    wchar_t* slash;

    GetModuleFileNameW(g_hinst, dir, MAX_PATH);
    slash = dir;
    for (q = dir; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(dir);
    lstrcatW(dir, L"mods");

    wsprintfW(pat, L"%s\\*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        WIN32_FIND_DATAW f2;
        HANDLE h2;
        int nfile = 0;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        wsprintfW(modDir, L"%s\\%s\\patches", dir, fd.cFileName);
        wsprintfW(pat, L"%s\\*.json", modDir);
        h2 = FindFirstFileW(pat, &f2);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do {
            char* buf;
            int before = g_npatch, k;
            if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wsprintfW(file, L"%s\\%s", modDir, f2.cFileName);
            buf = ReadWholeFile(file);
            if (!buf) continue;
            ParseJson(buf);
            HeapFree(GetProcessHeap(), 0, buf);
            for (k = before; k < g_npatch; k++) {
                // 한 모드에 .json 이 여럿이면 파일 이름까지 밝힌다.
                if (nfile > 0) wsprintfW(g_patches[k].src, L"%s / %s", fd.cFileName, f2.cFileName);
                else           lstrcpynW(g_patches[k].src, fd.cFileName, 64);
            }
            if (g_npatch > before)
                LogW(L"[PatchUtilKR] %s\\%s — 패치 %d개", fd.cFileName, f2.cFileName, g_npatch - before);
            nfile++;
        } while (FindNextFileW(h2, &f2));
        FindClose(h2);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ------------------------------------------------------------------ 고른 상태 기억하기
// 이 플러그인은 메모리만 고치므로 게임을 끄면 전부 원래대로 돌아간다.
// 그래서 창에서 켜고 끈 것, 고른 값을 patches.state 에 적어 두고 다음 실행 때 그대로 다시 적용한다.
// 형식은 줄마다 "항목이름<탭>값" (UTF-8). 값은 토글/값형이면 on, 선택형이면 숫자다.
// 항목은 이름으로 맞춘다 — patches.json 의 순서가 바뀌어도 따라간다.
// 모드에서 온 패치는 "모드이름|항목이름" 으로 적는다. 모드끼리 이름이 겹쳐도 따로 기억하고,
// 기본 patches.json 것은 예전과 같은 줄이라 쓰던 patches.state 가 그대로 먹는다.
static void StatePath(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"patches.state");
}

static void SaveState(void)
{
    wchar_t path[MAX_PATH];
    char line[512];
    HANDLE f;
    DWORD wr;
    int i;
    // /utf-8 로 컴파일하므로 좁은 문자열 리터럴이 그대로 UTF-8 이다.
    static const char hdr[] =
        "# PatchUtilKR — 창에서 고른 상태. 게임을 다시 켜면 이대로 다시 적용한다.\r\n"
        "# 이 파일을 지우면 다음 실행부터 patches.json 기본값으로 돌아간다.\r\n";

    StatePath(path, MAX_PATH);
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { LogW(L"[PatchUtilKR] patches.state 쓰기 실패"); return; }
    WriteFile(f, hdr, (DWORD)(sizeof(hdr) - 1), &wr, NULL);
    for (i = 0; i < g_npatch; i++) {
        Patch* p = &g_patches[i];
        char nm[256];
        int n;
        if (!p->name[0]) continue;
        {   // 모드에서 온 것은 "모드이름|항목이름" 으로 적는다 — 이름이 겹쳐도 따로 기억한다.
            wchar_t key[224];
            if (p->src[0]) wsprintfW(key, L"%s|%s", p->src, p->name);
            else           lstrcpynW(key, p->name, 224);
            if (WideCharToMultiByte(CP_UTF8, 0, key, -1, nm, sizeof(nm), NULL, NULL) <= 0) continue;
        }
        if (p->isChoice) {
            int v = Patch_ReadValue(p);
            if (v < 0) continue;
            n = wsprintfA(line, "%s\t%d\r\n", nm, v);
        } else {
            if (!p->applied) continue;                 // 적힌 것만 다시 켠다 — 없으면 꺼진 것
            n = wsprintfA(line, "%s\ton\r\n", nm);
        }
        WriteFile(f, line, (DWORD)n, &wr, NULL);
    }
    CloseHandle(f);
}

// patches.state 를 읽어 그대로 다시 적용한다. PatchCore_Load 끝에서 부른다.
static void ApplySavedState(void)
{
    wchar_t path[MAX_PATH];
    char* buf;
    char* p;
    int nOn = 0, nVal = 0;

    StatePath(path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) return;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        char* line = p;
        char* tab;
        wchar_t wname[256];
        int i;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        { char* e = line + lstrlenA(line); while (e > line && (e[-1] == '\r' || e[-1] == ' ')) *--e = 0; }
        if (!line[0] || line[0] == '#') continue;
        tab = line;
        while (*tab && *tab != '\t') tab++;
        if (!*tab) continue;
        *tab++ = 0;
        if (MultiByteToWideChar(CP_UTF8, 0, line, -1, wname, 256) <= 0) continue;
        for (i = 0; i < g_npatch; i++) {
            Patch* q = &g_patches[i];
            wchar_t key[224];
            if (q->src[0]) wsprintfW(key, L"%s|%s", q->src, q->name);
            else           lstrcpynW(key, q->name, 224);
            if (lstrcmpW(key, wname) != 0) continue;
            if (q->isChoice) {
                int v = 0, k;
                for (k = 0; tab[k] >= '0' && tab[k] <= '9'; k++) v = v * 10 + (tab[k] - '0');
                if (k && Patch_SetValue(q, v)) nVal++;
            } else if (tab[0] == 'o' && tab[1] == 'n') {
                if (!q->applied && Patch_SetApplied(i, TRUE)) nOn++;
            }
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (nOn || nVal) LogW(L"[PatchUtilKR] 지난번 상태 복원: 켠 것 %d개, 값 %d개", nOn, nVal);
}

void PatchCore_Load(void)
{
    wchar_t path[MAX_PATH];
    char* buf;
    int i;
    g_npatch = 0;
    if (!g_nt) InitPE();
    // 패치는 전부 mods\<모드>\patch\*.json 에서 온다. 예전에는 CDS95Util\patches.json
    // 하나에 몰아 넣었는데, 그러면 누가 만든 것인지 안 보이고 남의 것을 받아 넣기도 번거롭다.
    // 만든 이별로 폴더를 나눠 두면 넣고 빼는 것이 파일 옮기기로 끝난다.
    (void)path; (void)buf;
    LoadModPatches();
    LogW(L"[PatchUtilKR] 패치 %d개 로드", g_npatch);
    for (i = 0; i < g_npatch; i++) {
        Patch* p = &g_patches[i];
        BYTE* m0;
        SnapshotPatch(p);
        // 로드 시 현재 메모리가 이미 적용값(toggle=PatchedValue, number=Value)과 같으면
        // 체크(ON)로 표시해 창이 실제 상태를 반영하게 한다. (snap[0]=로드시점 현재 메모리)
        if (p->isChoice) {
            p->applied = 0;                    // 선택형은 ON/OFF 가 아니라 값 하나다
        } else if (p->nreg > 0) {              // 바이트열 패치: 첫 구간이 패치본과 같으면 ON
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
    // AutoApply 항목은 여기서 바로 적용한다. 게임이 시작하면서 한 번만 읽고 마는 값
    // (화면 크기 선택지처럼)은 창을 열어 체크할 때쯤이면 이미 늦기 때문이다.
    // 이 함수는 플러그인 DllMain(DLL_PROCESS_ATTACH)에서 불리므로 게임 코드보다 앞선다.
    for (i = 0; i < g_npatch; i++) {
        Patch* p = &g_patches[i];
        if (!p->autoApply || p->applied || p->isChoice) continue;
        if (Patch_SetApplied(i, TRUE))
            LogW(L"[PatchUtilKR] 자동 적용: %s", p->name[0] ? p->name : L"(무명)");
        else
            LogW(L"[PatchUtilKR] 자동 적용 실패(주소 불일치): %s", p->name[0] ? p->name : L"(무명)");
    }
    // 지난번에 창에서 고른 상태를 그대로 되살린다. 메모리 패치라 게임을 끄면 다 날아가므로
    // 이걸 안 하면 켤 때마다 다시 눌러야 한다. AutoApply 와 같은 시점(DllMain)이다.
    ApplySavedState();
}

// ================================================================== UI (ListView 창)
#define WC_PATCH   L"PatchUtilKR_Window"
#define ID_LIST    1001
#define ID_RELOAD  1002
#define ID_OPEN    1003

// 패치가 들어 있는 mods 폴더를 탐색기로 연다. 받은 .json 을 여기 넣으면 끝이라
// 파일 하나를 여는 것보다 폴더를 여는 쪽이 쓸모 있다. 넣은 뒤에는 옆의 "다시 읽기".
static void OpenPatchesFile(HWND owner)
{
    wchar_t path[MAX_PATH];
    wchar_t* q;
    wchar_t* slash = path;
    GetModuleFileNameW(g_hinst, path, MAX_PATH);
    for (q = path; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(path);
    lstrcatW(path, L"mods");
    CreateDirectoryW(path, NULL);
    ShellExecuteW(owner, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static HWND g_win = NULL, g_list = NULL;
static BOOL g_populating = FALSE;

static void StateText(Patch* p, wchar_t* buf)
{
    if (p->isChoice) {
        int v = Patch_ReadValue(p);
        if (v < 0) wsprintfW(buf, L"(못 읽음)");
        else       wsprintfW(buf, L"%d  (%d~%d)", v, p->vmin, p->vmax);
    }
    else if (p->isToggle && !p->naddr && p->nreg)
        lstrcpyW(buf, p->applied ? L"ON (바이트열)" : L"OFF (바이트열)");   // 값이 아니라 코드 덩어리다
    else if (p->isToggle)
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
        // Regions[] 만 있는 패치는 주소/바이트수를 그쪽에서 가져온다(예전엔 0x0 / 0 으로 나왔다).
        if (p->naddr > 1)      wsprintfW(a, L"0x%X 외%d", p->addrs[0], p->naddr - 1);
        else if (p->naddr)     wsprintfW(a, L"0x%X", p->addrs[0]);
        else if (p->nreg > 1)  wsprintfW(a, L"0x%X 외%d", p->regs[0].off, p->nreg - 1);
        else if (p->nreg)      wsprintfW(a, L"0x%X", p->regs[0].off);
        else                   wsprintfW(a, L"-");
        if (p->naddr && !p->mapped[0]) lstrcatW(a, L" (X)");
        if (!p->naddr && p->nreg && !p->regs[0].mapped) lstrcatW(a, L" (X)");
        ListView_SetItemText(g_list, i, 1, p->src[0] ? p->src : L"기본");
        ListView_SetItemText(g_list, i, 2, a);
        if (!p->naddr && p->nreg) {
            int k, tot = 0;
            for (k = 0; k < p->nreg; k++) tot += p->regs[k].len;
            wsprintfW(bs, L"%d", tot);
        } else wsprintfW(bs, L"%d", p->byteSize);
        ListView_SetItemText(g_list, i, 3, bs);
        ListView_SetItemText(g_list, i, 4, p->isChoice ? L"선택" : (p->isToggle ? L"토글" : L"값"));
        StateText(p, st);
        ListView_SetItemText(g_list, i, 5, st);
        ListView_SetItemText(g_list, i, 6, p->desc);
        if (!p->isChoice) ListView_SetCheckState(g_list, i, p->applied);   // 선택형은 체크 개념이 없다
    }
    g_populating = FALSE;
}

static LRESULT CALLBACK PatchProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
        case WM_CREATE: {
            const wchar_t* titles[7] = { L"이름", L"출처", L"주소", L"바이트", L"종류", L"상태", L"설명" };
            int widths[7] = { 180, 110, 120, 52, 52, 90, 210 };
            LVCOLUMNW c;
            int i;
            g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                        0, 0, 10, 10, h, (HMENU)ID_LIST, g_hinst, NULL);
            ListView_SetExtendedListViewStyle(g_list,
                        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            ZeroMemory(&c, sizeof(c));
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            for (i = 0; i < 7; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }
            CreateWindowExW(0, L"BUTTON", L"다시 읽기",
                        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"패치 폴더 열기",
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
            // 값 선택형: 줄을 두 번 누르면 고를 값 목록이 뜬다.
            // (창 안에 콤보박스를 두는 대신 팝업 메뉴를 쓴다 — 새 창도, 포커스 다툼도 없다.)
            if (nh->idFrom == ID_LIST && (nh->code == NM_DBLCLK || nh->code == NM_RCLICK)) {
                int row = ((LPNMITEMACTIVATE)l)->iItem;
                if (row >= 0 && row < g_npatch && g_patches[row].isChoice) {
                    Patch* p = &g_patches[row];
                    int vals[MAX_CHOICES], n = Patch_BuildChoices(p, vals);
                    int cur = Patch_ReadValue(p), i, pick;
                    HMENU menu = CreatePopupMenu();
                    POINT pt;
                    for (i = 0; i < n; i++) {
                        wchar_t t[32];
                        wsprintfW(t, L"%d", vals[i]);
                        AppendMenuW(menu, MF_STRING | (vals[i] == cur ? MF_CHECKED : 0), (UINT_PTR)(i + 1), t);
                    }
                    GetCursorPos(&pt);
                    pick = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                               pt.x, pt.y, 0, h, NULL);
                    DestroyMenu(menu);
                    if (pick >= 1 && pick <= n && Patch_SetValue(p, vals[pick - 1])) {
                        wchar_t st[48];
                        StateText(p, st);
                        ListView_SetItemText(g_list, row, 4, st);
                        SaveState();          // 다음 실행 때 그대로 되살리도록 적어 둔다
                    }
                }
                return 0;
            }
            if (nh->idFrom == ID_LIST && nh->code == LVN_ITEMCHANGED && !g_populating) {
                NMLISTVIEW* nm = (NMLISTVIEW*)l;
                if (nm->uChanged & LVIF_STATE) {
                    BOOL was = ((nm->uOldState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                    BOOL is  = ((nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
                    if (was != is && nm->iItem >= 0 && nm->iItem < g_npatch) {
                        Patch* p = &g_patches[nm->iItem];
                        if (p->isChoice) {          // 선택형은 체크로 켜고 끄는 게 아니다
                            g_populating = TRUE;
                            ListView_SetCheckState(g_list, nm->iItem, FALSE);
                            g_populating = FALSE;
                            return 0;
                        }
                        // Regions[] 만 있는 패치는 naddr 가 0 이라, 예전처럼 naddr/mapped[0] 만
                        // 보면 늘 거부됐다(해적·화면크기 항목이 켜지지 않던 이유).
                        // 주소든 바이트열이든 메모리에 잡힌 대상이 하나라도 있으면 적용을 시도한다.
                        int usable = 0, k;
                        for (k = 0; k < p->naddr && !usable; k++) if (p->mapped[k]) usable = 1;
                        for (k = 0; k < p->nreg  && !usable; k++) if (p->regs[k].mapped) usable = 1;
                        if (is && !usable) {
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
                            SaveState();      // 다음 실행 때 그대로 되살리도록 적어 둔다
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
// 메뉴 감시 횟수. "모드" 서브메뉴는 ModUtilKR 이 만들게 두고, 그래도 없으면 두 번째
// 바퀴에서 우리가 만든다 — 셋이 동시에 만들면 "모드" 가 여러 개 생긴다.
static int     g_pass = 0;

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

// 이 메뉴(하위 메뉴까지)에 우리 항목이 이미 있나.
// 항목을 "모드" 서브메뉴로 옮긴 뒤로 파일 메뉴만 훑으면 늘 "없다" 가 나와서, 1초마다 또
// 달아 메뉴가 끝없이 늘어났다. 그래서 아래로 내려가며 본다.
static BOOL MenuHasId(HMENU m, UINT id)
{
    int n, i;
    if (!m) return FALSE;
    n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        if (sub) { if (MenuHasId(sub, id)) return TRUE; continue; }
        if (GetMenuItemID(m, (UINT)i) == id) return TRUE;
    }
    return FALSE;
}

// "파일 > 모드" 서브메뉴를 찾거나 만든다.
//
// 플러그인 관리 · 퀘스트 모드 · 패치가 각자 파일 메뉴에 항목을 달면 목록이 너무 길어진다.
// 셋을 이 하나 아래로 모은다. 서로를 모르는 별개 DLL 이라 먼저 뜬 쪽이 만들고 나머지는
// 찾아 붙는다. 겹쳐 생긴 빈 "모드" 는 보이는 대로 치운다(동시에 만들면 둘이 될 수 있다).
static HMENU FindOrCreateModMenu(HMENU fileMenu, BOOL mayCreate)
{
    int i;
    WCHAR s[64];
    HMENU first = NULL, sub;
    if (!fileMenu) return NULL;
    for (i = GetMenuItemCount(fileMenu) - 1; i >= 0; i--) {
        if (GetMenuStringW(fileMenu, (UINT)i, s, 64, MF_BYPOSITION) <= 0) continue;
        if (lstrcmpW(s, L"모드") != 0) continue;
        sub = GetSubMenu(fileMenu, (UINT)i);
        if (first && sub && GetMenuItemCount(sub) == 0) { RemoveMenu(fileMenu, (UINT)i, MF_BYPOSITION); continue; }
        first = sub;
    }
    if (first || !mayCreate) return first;
    sub = CreatePopupMenu();
    if (!sub) return NULL;
    AppendMenuW(fileMenu, MF_POPUP, (UINT_PTR)sub, L"모드");
    return sub;
}

static DWORD WINAPI MenuThread(LPVOID pv)
{
    (void)pv;
    OutputDebugStringW(L"[PatchUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!MenuHasId(target, ID_PATCH_OPEN)) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                {   // 파일 메뉴가 아니라 "모드" 아래에 붙인다
                    HMENU modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                    if (!modMenu) continue;      // 아직 "모드" 가 없다 — 다음 바퀴에 다시 본다
                    AppendMenuW(modMenu, MF_STRING, ID_PATCH_OPEN, L"패치");
                }
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
