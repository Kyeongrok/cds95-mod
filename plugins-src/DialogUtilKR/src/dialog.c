#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>   // ShellExecuteW — dialogs 폴더를 탐색기로 열기
#include "dialog.h"
#include "modmenu.h"   // common/ — 모드 창 등록부(걷어 간 항목을 여기서 본다)

// DialogUtilKR — 게임 문구를 파일로 갈아 끼운다.
//
// 게임의 한국어 문구는 EXE 안에 CP949 로 그대로 들어 있고(커스텀 인코딩이 아니다),
// 자리마다 다음 문구까지의 폭이 정해져 있다. 그래서 두 가지 길로 쓴다.
//   · 새 글이 그 폭 안에 들면 그 자리에 그대로 쓴다.
//   · 넘으면 우리가 잡은 메모리에 새 글을 두고, 그 문자열을 가리키던 포인터를 전부
//     새 주소로 돌린다. 포인터는 데이터 표(4바이트 정렬)와 코드의 imm32
//     (push / mov r32,imm32 / mov [mem],imm32) 에서 찾는다.
//
// 게임을 끄면 메모리만 되돌아가므로 EXE 는 그대로 남는다.

#define MAX_ENTRIES   512
#define MAX_TEXT      512      // CP949 바이트 기준 한 문구 한도
#define MAX_HITS      32       // 원문이 여러 자리에 있을 때 고칠 자리 한도
#define MAX_UNDO      4096
#define POOL_SIZE     0x10000  // 새 문구를 두는 메모리 한 덩어리(64KB)

#define ID_DLG_OPEN 0xC100u    // "파일>모드>대사" 메뉴 커맨드
                               // (Trade=0xB10x/0xC0xx, Char=0xB301, Ship=0xB410, Patch=0xB500,
                               //  Map=0xB600, Mod=0xB700, QMod=0xB800, Upd=0xB900, Fatigue=0xBA00,
                               //  Hotkey=0xBB00, Hint=0xBC00, Market=0xBD00, Save=0xBE00, Pic=0xBF00)
#define ID_LIST     1001
#define ID_RELOAD   1002
#define ID_OPENDIR  1003

typedef struct {
    wchar_t      src[64];        // 어느 파일에서 왔나 ("기본" 이면 CDS95Util\dialogs)
    wchar_t      note[128];      // 사람이 적어 둔 설명
    wchar_t      wfind[192];     // 보여주기용 원문
    wchar_t      wtext[192];     // 보여주기용 새 글
    char         find[MAX_TEXT]; // CP949 원문
    int          findLen;
    char         text[MAX_TEXT]; // CP949 새 글
    int          textLen;
    unsigned int off;            // 파일오프셋으로 자리를 직접 준 경우
    int          hasOff;
    int          all;            // 원문이 여러 자리에 있으면 전부 고칠까 (기본 1)
    int          enabled;        // 기본 1
    // 적용 결과
    int          nhit;           // 고친 자리 수
    int          nreloc;         // 그 중 재배치로 처리한 자리 수
    wchar_t      status[96];
} Entry;

typedef struct {
    BYTE* mem;
    int   len;
    BYTE* orig;      // 힙에 떠 둔 원본 바이트
} Undo;

static HINSTANCE g_hinst = NULL;
static Entry     g_entries[MAX_ENTRIES];
static int       g_nentry = 0;
static Undo      g_undo[MAX_UNDO];
static int       g_nundo = 0;

// ------------------------------------------------------------------ 로그 · 경로
static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다.
// (PatchUtilKR 과 같은 규칙 — 데이터는 한 자리에 모아 둬야 서로 찾는다.)
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

// CDS95Util 폴더(뒤에 역슬래시 포함)를 out 에 담는다.
static void DataDir(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
}

// ------------------------------------------------------------------ PE 섹션
typedef struct { BYTE* base; DWORD size; char name[9]; } Sec;

static BYTE*             g_base = NULL;
static IMAGE_NT_HEADERS* g_nt   = NULL;
static Sec               g_secs[16];
static int               g_nsec = 0;

static void InitPE(void)
{
    IMAGE_DOS_HEADER* dos;
    IMAGE_SECTION_HEADER* s;
    int n, i;
    g_nsec = 0;
    g_base = (BYTE*)GetModuleHandleW(NULL);       // 메인 exe(cds_95) 로드 베이스
    dos = (IMAGE_DOS_HEADER*)g_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) { g_nt = NULL; return; }
    g_nt = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    if (g_nt->Signature != IMAGE_NT_SIGNATURE) { g_nt = NULL; return; }
    s = IMAGE_FIRST_SECTION(g_nt);
    n = g_nt->FileHeader.NumberOfSections;
    for (i = 0; i < n && g_nsec < 16; i++) {
        DWORD vs = s[i].Misc.VirtualSize, rs = s[i].SizeOfRawData;
        DWORD use = (vs && vs < rs) ? vs : rs;    // 파일에 실린 만큼만 본다(BSS 는 빼고)
        int k;
        if (!use) continue;
        g_secs[g_nsec].base = g_base + s[i].VirtualAddress;
        g_secs[g_nsec].size = use;
        for (k = 0; k < 8; k++) g_secs[g_nsec].name[k] = (char)s[i].Name[k];
        g_secs[g_nsec].name[8] = 0;
        g_nsec++;
    }
}

// 파일오프셋 → 로드된 메모리 주소 (cds-helper VaToFileOffset 의 역변환)
static BYTE* OffToMem(unsigned int off)
{
    IMAGE_SECTION_HEADER* s;
    int n, i;
    if (!g_nt) return NULL;
    if (off < g_nt->OptionalHeader.SizeOfHeaders) return g_base + off;
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

static const Sec* FindSec(const char* name)
{
    int i;
    for (i = 0; i < g_nsec; i++) if (lstrcmpA(g_secs[i].name, name) == 0) return &g_secs[i];
    return NULL;
}

// ------------------------------------------------------------------ 메모리 쓰기 · 되돌리기
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

// 고치기 전 그 자리의 바이트를 적어 둔다. 창에서 "다시 읽기" 를 누르면 이걸로 되돌린 뒤
// 다시 적용한다 — 안 그러면 이미 바뀐 자리라 원문 검색이 실패한다.
static BOOL Remember(BYTE* mem, int len)
{
    BYTE* copy;
    if (g_nundo >= MAX_UNDO) return FALSE;
    copy = (BYTE*)HeapAlloc(GetProcessHeap(), 0, len);
    if (!copy) return FALSE;
    CopyMemory(copy, mem, len);
    g_undo[g_nundo].mem  = mem;
    g_undo[g_nundo].len  = len;
    g_undo[g_nundo].orig = copy;
    g_nundo++;
    return TRUE;
}

static void RevertAll(void)
{
    int i;
    for (i = g_nundo - 1; i >= 0; i--) {
        WriteMem(g_undo[i].mem, g_undo[i].orig, g_undo[i].len);
        HeapFree(GetProcessHeap(), 0, g_undo[i].orig);
    }
    g_nundo = 0;
}

// ------------------------------------------------------------------ 새 문구를 둘 메모리
// 되돌리기가 포인터를 원래대로 돌려놓으므로, 되돌린 뒤에는 이 덩어리를 그냥 다시 써도 된다.
static BYTE* g_pool = NULL;
static int   g_poolUsed = 0;

static BYTE* PoolAlloc(int n)
{
    if (!g_pool) {
        g_pool = (BYTE*)VirtualAlloc(NULL, POOL_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        g_poolUsed = 0;
        if (!g_pool) return NULL;
    }
    if (g_poolUsed + n > POOL_SIZE) return NULL;
    { BYTE* p = g_pool + g_poolUsed; g_poolUsed += (n + 3) & ~3; return p; }
}

// ------------------------------------------------------------------ 인코딩
// JSON 은 UTF-8 로 적는다. 게임 글꼴이 CP949 라 그 인코딩으로 바꿔 넣는다.
static int Utf8ToCp949(const char* s, char* out, int cap)
{
    wchar_t w[MAX_TEXT];
    int n;
    if (!s || !s[0]) { out[0] = 0; return 0; }
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, w, MAX_TEXT);
    if (n <= 0) { out[0] = 0; return 0; }
    n = WideCharToMultiByte(949, 0, w, -1, out, cap, NULL, NULL);
    return n > 0 ? n - 1 : 0;        // 널은 빼고 센다
}

static void Cp949ToW(const char* s, wchar_t* out, int outcch)
{
    if (MultiByteToWideChar(949, 0, s, -1, out, outcch) <= 0) out[0] = 0;
}

static void Utf8ToW(const char* s, wchar_t* out, int outcch)
{
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, outcch) <= 0) out[0] = 0;
}

// ------------------------------------------------------------------ JSON (PatchUtilKR 과 같은 손파서)
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
    while (**pp && **pp != ',' && **pp != '}' && **pp != ']') (*pp)++;
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

static BOOL ParseHex(const char* s, unsigned int* out)
{
    unsigned int v = 0;
    int any = 0;
    if (!s) return FALSE;
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

static BOOL ParseBool(const char** pp, int dflt)
{
    SkipWS(pp);
    if (**pp == 't') { SkipValue(pp); return 1; }
    if (**pp == 'f') { SkipValue(pp); return 0; }
    SkipValue(pp);
    return dflt;
}

// { "Find": "원문", "Text": "새 글", "Note": "…", "Address": "0x147A48", "All": true, "Enabled": true }
// 키는 한국어로도 받는다: 원문 / 대사 / 설명 / 주소.
static BOOL ParseEntry(const char** pp, Entry* e)
{
    char raw[MAX_TEXT * 2];
    if (**pp != '{') { SkipValue(pp); return FALSE; }
    (*pp)++;
    for (;;) {
        char key[48];
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') { if (!**pp) return FALSE; (*pp)++; continue; }
        ParseStringInto(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);
        if (lstrcmpA(key, "Find") == 0 || lstrcmpA(key, "원문") == 0) {
            ParseStringInto(pp, raw, sizeof(raw));
            e->findLen = Utf8ToCp949(raw, e->find, MAX_TEXT);
            Utf8ToW(raw, e->wfind, 192);
        } else if (lstrcmpA(key, "Text") == 0 || lstrcmpA(key, "대사") == 0) {
            ParseStringInto(pp, raw, sizeof(raw));
            e->textLen = Utf8ToCp949(raw, e->text, MAX_TEXT);
            Utf8ToW(raw, e->wtext, 192);
        } else if (lstrcmpA(key, "Note") == 0 || lstrcmpA(key, "설명") == 0) {
            ParseStringInto(pp, raw, sizeof(raw));
            Utf8ToW(raw, e->note, 128);
        } else if (lstrcmpA(key, "Address") == 0 || lstrcmpA(key, "주소") == 0) {
            char tmp[64];
            ParseStringInto(pp, tmp, sizeof(tmp));
            if (ParseHex(tmp, &e->off)) e->hasOff = 1;
        } else if (lstrcmpA(key, "All") == 0 || lstrcmpA(key, "전부") == 0) {
            e->all = ParseBool(pp, 1);
        } else if (lstrcmpA(key, "Enabled") == 0 || lstrcmpA(key, "켬") == 0) {
            e->enabled = ParseBool(pp, 1);
        } else SkipValue(pp);
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }
    return (e->findLen > 0 || e->hasOff) && e->textLen > 0;
}

static void ParseJson(const char* buf, const wchar_t* src)
{
    const char* p = buf;
    if (!p) return;
    if ((BYTE)p[0] == 0xEF && (BYTE)p[1] == 0xBB && (BYTE)p[2] == 0xBF) p += 3;   // BOM
    SkipWS(&p);
    if (*p != '[') return;
    p++;
    for (;;) {
        SkipWS(&p);
        if (*p == ']' || !*p) break;
        if (*p == '{') {
            Entry e;
            ZeroMemory(&e, sizeof(e));
            e.all = 1; e.enabled = 1;
            if (ParseEntry(&p, &e) && g_nentry < MAX_ENTRIES) {
                lstrcpynW(e.src, src, 64);
                g_entries[g_nentry++] = e;
            }
        } else SkipValue(&p);
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; break; }
        break;
    }
}

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

// 폴더 안의 *.json 을 전부 읽는다.
static void LoadFolder(const wchar_t* dir, const wchar_t* srcPrefix)
{
    wchar_t pat[MAX_PATH], file[MAX_PATH], src[64];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wsprintfW(pat, L"%s\\*.json", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char* buf;
        int before = g_nentry;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wsprintfW(file, L"%s\\%s", dir, fd.cFileName);
        buf = ReadWholeFile(file);
        if (!buf) continue;
        if (srcPrefix[0]) wsprintfW(src, L"%s / %s", srcPrefix, fd.cFileName);
        else              lstrcpynW(src, fd.cFileName, 64);
        ParseJson(buf, src);
        HeapFree(GetProcessHeap(), 0, buf);
        if (g_nentry > before) LogW(L"[DialogUtilKR] %s — 문구 %d개", src, g_nentry - before);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// mods\<만든이>\dialogs\*.json 도 읽는다 (patches 와 같은 규칙).
static void LoadModFolders(void)
{
    wchar_t root[MAX_PATH], pat[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    DataDir(root, MAX_PATH);
    lstrcatW(root, L"mods");
    wsprintfW(pat, L"%s\\*", root);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        wsprintfW(sub, L"%s\\%s\\dialogs", root, fd.cFileName);
        LoadFolder(sub, fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ------------------------------------------------------------------ 찾기
// 널로 끝나는 문자열의 시작 자리만 잡는다(앞 바이트가 0). 부분 일치로 엉뚱한 데를
// 고치지 않으려는 것이다 — 게임 문구는 자리마다 널 패딩이 붙어 있어 이 조건이 잘 맞는다.
static int FindAll(const char* pat, int len, BYTE** out, int max)
{
    static const char* kSecs[] = { ".data", ".rdata" };
    int si, n = 0;
    for (si = 0; si < 2 && n < max; si++) {
        const Sec* s = FindSec(kSecs[si]);
        BYTE *p, *end;
        if (!s) continue;
        p = s->base;
        end = s->base + s->size - len - 1;
        for (; p < end && n < max; p++) {
            if (*p != (BYTE)pat[0]) continue;
            if (p > s->base && p[-1] != 0) continue;          // 문자열 한복판이다
            if (p[len] != 0) continue;                        // 뒤가 더 있다 = 다른 문구
            {
                int k;
                for (k = 1; k < len; k++) if (p[k] != (BYTE)pat[k]) break;
                if (k < len) continue;
            }
            out[n++] = p;
            p += len;
        }
    }
    return n;
}

// 이 자리에 쓸 수 있는 바이트 수 — 문구 길이 + 뒤에 이어지는 0 개수.
// (마지막 0 자리는 새 문구의 널로 쓴다.)
static int SlotCap(BYTE* p, int len)
{
    int z = 0;
    BYTE* q = p + len;                 // 원문의 널
    while (z < MAX_TEXT && q[z + 1] == 0) z++;
    return len + z;
}

// ------------------------------------------------------------------ 포인터 돌리기
// 코드에서 imm32 가 정확히 q 자리인지 본다. 명령 길이를 세어 확인하므로 우연히 같은
// 4바이트가 코드에 박혀 있어도 잘못 고치지 않는다.
//   68 imm32               push imm32
//   B8+r imm32             mov r32, imm32
//   C7 /0 [SIB][disp] imm32  mov r/m32, imm32
static BOOL ImmHere(const BYTE* s, const BYTE* q)
{
    if (s[0] == 0x68) return s + 1 == q;
    if (s[0] >= 0xB8 && s[0] <= 0xBF) return s + 1 == q;
    if (s[0] == 0xC7) {
        BYTE m = s[1];
        int mod = m >> 6, rm = m & 7;
        const BYTE* p = s + 2;
        if (((m >> 3) & 7) != 0) return FALSE;          // /0 만 imm32 를 갖는다
        if (mod != 3) {
            if (rm == 4) {                              // SIB
                BYTE sib = *p++;
                if (mod == 0 && (sib & 7) == 5) p += 4;
            }
            if (mod == 0 && rm == 5) p += 4;
            else if (mod == 1) p += 1;
            else if (mod == 2) p += 4;
        }
        return p == q;
    }
    return FALSE;
}

// oldVA 를 가리키던 자리를 전부 newVA 로 돌린다. 고친 자리 수를 돌려준다.
static int RewritePointers(DWORD oldVA, DWORD newVA)
{
    static const char* kData[] = { ".data", ".rdata" };
    int si, n = 0;
    // 1) 데이터 표 — 4바이트 정렬 자리만 본다
    for (si = 0; si < 2; si++) {
        const Sec* s = FindSec(kData[si]);
        BYTE *p, *end;
        if (!s) continue;
        p = (BYTE*)(((UINT_PTR)s->base + 3) & ~(UINT_PTR)3);
        end = s->base + s->size - 4;
        for (; p <= end; p += 4) {
            if (*(DWORD*)p != oldVA) continue;
            if (Remember(p, 4) && WriteMem(p, (BYTE*)&newVA, 4)) n++;
        }
    }
    // 2) 코드 — push / mov 의 imm32 자리
    {
        const Sec* s = FindSec(".text");
        if (s) {
            BYTE *p = s->base + 8, *end = s->base + s->size - 4;
            for (; p <= end; p++) {
                int k;
                if (*(DWORD*)p != oldVA) continue;
                for (k = 1; k <= 8; k++) if (ImmHere(p - k, p)) break;
                if (k > 8) { LogW(L"[DialogUtilKR] 코드 0x%X 의 값은 명령을 못 읽어 그냥 뒀다", (DWORD)(UINT_PTR)p); continue; }
                if (Remember(p, 4) && WriteMem(p, (BYTE*)&newVA, 4)) n++;
            }
        }
    }
    return n;
}

// ------------------------------------------------------------------ 적용
static void ApplyEntry(Entry* e)
{
    BYTE* hits[MAX_HITS];
    int nhit = 0, i;

    e->nhit = 0; e->nreloc = 0; e->status[0] = 0;

    if (!e->enabled) { lstrcpyW(e->status, L"꺼 둠"); return; }

    if (e->hasOff) {
        BYTE* m = OffToMem(e->off);
        if (!m) { wsprintfW(e->status, L"주소 0x%X 를 못 옮김", e->off); return; }
        hits[nhit++] = m;
    } else {
        nhit = FindAll(e->find, e->findLen, hits, e->all ? MAX_HITS : 1);
        if (!nhit) { lstrcpyW(e->status, L"원문을 못 찾음"); return; }
    }

    for (i = 0; i < nhit; i++) {
        BYTE* p = hits[i];
        int len = e->hasOff ? lstrlenA((char*)p) : e->findLen;
        int cap = SlotCap(p, len);
        if (e->textLen <= cap) {
            BYTE buf[MAX_TEXT + 1];
            int total = cap + 1;                       // 원문 + 패딩 + 널
            if (total > MAX_TEXT + 1) total = MAX_TEXT + 1;
            ZeroMemory(buf, total);
            CopyMemory(buf, e->text, e->textLen);
            if (Remember(p, total) && WriteMem(p, buf, total)) e->nhit++;
        } else {
            BYTE* np = PoolAlloc(e->textLen + 1);
            int n;
            if (!np) { lstrcpyW(e->status, L"새 글 둘 자리가 모자람"); continue; }
            CopyMemory(np, e->text, e->textLen);
            np[e->textLen] = 0;
            n = RewritePointers((DWORD)(UINT_PTR)p, (DWORD)(UINT_PTR)np);
            if (n) { e->nhit++; e->nreloc++; }
            else   wsprintfW(e->status, L"자리보다 %d바이트 긴데 가리키는 곳을 못 찾음", e->textLen - cap);
        }
    }

    if (e->nhit && e->nreloc)      wsprintfW(e->status, L"적용 %d곳 (재배치 %d)", e->nhit, e->nreloc);
    else if (e->nhit)              wsprintfW(e->status, L"적용 %d곳", e->nhit);
    else if (!e->status[0])        lstrcpyW(e->status, L"적용 못 함");
}

static void DialogCore_Load(void)
{
    wchar_t dir[MAX_PATH];
    int i, ok = 0, ng = 0;

    RevertAll();
    g_poolUsed = 0;              // 되돌렸으니 새 글 자리는 다시 써도 된다
    g_nentry = 0;

    DataDir(dir, MAX_PATH);
    lstrcatW(dir, L"dialogs");
    LoadFolder(dir, L"");
    LoadModFolders();

    for (i = 0; i < g_nentry; i++) {
        ApplyEntry(&g_entries[i]);
        if (g_entries[i].nhit) ok++;
        else if (g_entries[i].enabled) {
            ng++;
            LogW(L"[DialogUtilKR] \"%s\" — %s", g_entries[i].wfind[0] ? g_entries[i].wfind : g_entries[i].note, g_entries[i].status);
        }
    }
    LogW(L"[DialogUtilKR] 문구 %d개 중 %d개 적용, %d개 실패", g_nentry, ok, ng);
}

// ------------------------------------------------------------------ 창
static HWND g_win = NULL, g_list = NULL;

static void OpenDialogsDir(HWND owner)
{
    wchar_t dir[MAX_PATH];
    DataDir(dir, MAX_PATH);
    lstrcatW(dir, L"dialogs");
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) CreateDirectoryW(dir, NULL);
    ShellExecuteW(owner, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
}

static void FillList(void)
{
    int i;
    ListView_DeleteAllItems(g_list);
    for (i = 0; i < g_nentry; i++) {
        Entry* e = &g_entries[i];
        LVITEMW it;
        wchar_t find[192];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.iSubItem = 0;
        if (e->wfind[0]) lstrcpynW(find, e->wfind, 192);
        else             wsprintfW(find, L"(주소 0x%X)", e->off);
        it.pszText = find;
        it.lParam = i;
        ListView_InsertItem(g_list, &it);
        ListView_SetItemText(g_list, i, 1, e->wtext);
        ListView_SetItemText(g_list, i, 2, e->status);
        ListView_SetItemText(g_list, i, 3, e->src[0] ? e->src : L"기본");
        ListView_SetItemText(g_list, i, 4, e->note);
    }
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
        case WM_CREATE: {
            const wchar_t* titles[5] = { L"원문", L"바꾼 말", L"상태", L"출처", L"설명" };
            int widths[5] = { 260, 260, 160, 120, 160 };
            LVCOLUMNW c;
            int i;
            g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                        0, 0, 10, 10, h, (HMENU)ID_LIST, g_hinst, NULL);
            ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            ZeroMemory(&c, sizeof(c));
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            for (i = 0; i < 5; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i]; ListView_InsertColumn(g_list, i, &c); }
            CreateWindowExW(0, L"BUTTON", L"다시 읽기",
                        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
            CreateWindowExW(0, L"BUTTON", L"대사 폴더 열기",
                        WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, h, (HMENU)ID_OPENDIR, g_hinst, NULL);
            FillList();
            return 0;
        }
        case WM_SIZE: {
            int cw = LOWORD(l), ch = HIWORD(l), bh = 30;
            MoveWindow(g_list, 0, 0, cw, ch - bh, TRUE);
            MoveWindow(GetDlgItem(h, ID_RELOAD), 6, ch - bh + 3, 150, 24, TRUE);
            MoveWindow(GetDlgItem(h, ID_OPENDIR), 162, ch - bh + 3, 150, 24, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == ID_RELOAD)      { DialogCore_Load(); FillList(); }
            else if (LOWORD(w) == ID_OPENDIR) OpenDialogsDir(h);
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            g_win = NULL; g_list = NULL;
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void DialogWin_Show(HWND owner)
{
    static BOOL registered = FALSE;
    if (g_win) { SetForegroundWindow(g_win); return; }
    if (!registered) {
        WNDCLASSW wc;
        INITCOMMONCONTROLSEX ic;
        ic.dwSize = sizeof(ic); ic.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&ic);
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = g_hinst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DialogUtilKRWin";
        RegisterClassW(&wc);
        registered = TRUE;
    }
    g_win = CreateWindowExW(0, L"DialogUtilKRWin", L"대사 — DialogUtilKR",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 980, 460,
                owner, NULL, g_hinst, NULL);
    if (g_win) { ShowWindow(g_win, SW_SHOW); UpdateWindow(g_win); }
}

// ================================================================== 메뉴 설치 + 서브클래싱
static HWND    g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC g_origProc = NULL;
static int     g_pass = 0;

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) == ID_DLG_OPEN) { DialogWin_Show(h); return 0; }
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

// "파일 > 모드" 서브메뉴를 찾거나(먼저 뜬 플러그인이 만들어 둔다) 두 바퀴째에 만든다.
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
    OutputDebugStringW(L"[DialogUtilKR] menu monitor started.");
    for (;;) {
        HMENU bar;
        g_pass++;
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd && (bar = GetMenu(g_gameHwnd)) != NULL) {
            HMENU fileMenu = FindFileMenu(bar);
            HMENU target = fileMenu ? fileMenu : bar;
            if (!(MenuHasId(target, ID_DLG_OPEN) || ModMenu_HasId(g_gameHwnd, ID_DLG_OPEN))) {
                if (fileMenu && !FileMenuHasPluginItem(fileMenu))
                    AppendMenuW(fileMenu, MF_SEPARATOR, 0, NULL);
                {
                    HMENU modMenu = FindOrCreateModMenu(fileMenu ? fileMenu : target, g_pass > 1);
                    if (!modMenu) { Sleep(1000); continue; }   // 아직 "모드" 가 없다 — 다음 바퀴에
                    AppendMenuW(modMenu, MF_STRING, ID_DLG_OPEN, L"대사");
                }
                DrawMenuBar(g_gameHwnd);
                OutputDebugStringW(L"[DialogUtilKR] 대사 menu installed.");
            }
            if (g_subHwnd != g_gameHwnd) {
                g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                g_subHwnd = g_gameHwnd;
                OutputDebugStringW(L"[DialogUtilKR] window subclassed.");
            }
        }
        Sleep(1000);
    }
}

void DialogKR_Init(HINSTANCE hinst)
{
    g_hinst = hinst;
    InitPE();
    DialogCore_Load();
    {
        HANDLE t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
}
