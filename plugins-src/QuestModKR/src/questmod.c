#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "questmod.h"

// QuestModKR — 퀘스트 파일 갈아 끼우기. 자세한 배치는 questmod.h 참고.
//
// 왜 파일을 복사하나 — 게임은 퀘스트 이벤트를 게임 폴더의 <직업>.CDS 에서 읽는다.
// 어느 파일을 읽을지는 세이브에 박혀 있어서 바꿀 수 없다. 그래서 모드 전환은
// "그 이름 자리에 다른 파일을 놓는 것" 말고는 길이 없다.
//
// CharacterUtilKR 과의 관계 — 그쪽은 <이름>.CDS.orig 를 원본으로 삼고 게임이 읽는 .CDS 를
// [원본 + quests.json] 으로 매번 다시 만든다. 그래서 우리가 파일만 바꿔 놓으면 옛 원본이
// 도로 덮어써 버린다. 갈아 끼울 때 .orig 와 .stamp 를 지워서 새 파일이 원본이 되게 한다.

#define ID_QMOD_OPEN 0xB800u   // Trade=0xB101/0xB102/0xC0xx, Char=0xB301, Ship=0xB410,
                               // Patch=0xB500, Map=0xB600, Mod=0xB700 과 안 겹치게.
#define MAX_MOD    32
#define MAX_FILE   32

typedef struct {
    wchar_t name[64];               // 폴더 이름 = 모드 이름
    wchar_t base[64];               // mod.txt 의 base= — 이 모드를 깔기 전에 먼저 깔 모드
    wchar_t all[32];                // mod.txt 의 all= — 이 파일 하나를 8직업 이름으로 다 깐다
    wchar_t desc[256];              // mod.txt 의 설명 줄
    wchar_t file[MAX_FILE][32];     // 이 모드가 덮어쓸 .CDS 이름들
    int     nfile;
    int     hasJson;                // 모드 폴더에 quests.json 이 있나
    int     applied;                // 지금 이게 깔려 있나
} QMod;

static HINSTANCE g_hinst = NULL;
static QMod      g_mods[MAX_MOD];
static int       g_nmod = 0;
static HWND      g_wnd = NULL, g_list = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

static void LogW(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfW(buf, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
}

// ------------------------------------------------------------------ 경로

static void JoinPath(wchar_t* out, const wchar_t* a, const wchar_t* b)
{
    lstrcpyW(out, a);
    if (out[0] && out[lstrlenW(out) - 1] != L'\\') lstrcatW(out, L"\\");
    lstrcatW(out, b);
}

// 이 플러그인이 있는 폴더 = CDS95Util
static void PluginDir(wchar_t* out)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, MAX_PATH);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
}

// 게임 실행 파일이 있는 폴더 — 퀘스트 .CDS 가 여기 있다.
static void GameDir(wchar_t* out)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(NULL, out, MAX_PATH);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
}

static void ModsDir(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    PluginDir(dir);
    JoinPath(out, dir, L"mods");
}

static void StatePath(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    PluginDir(dir);
    JoinPath(out, dir, L"questmod.state");
}

static void JsonPath(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    PluginDir(dir);
    JoinPath(out, dir, L"quests.json");
}

// ------------------------------------------------------------------ 파일 견주기

// 크기와 마지막 쓴 시각이 같으면 같은 파일로 본다.
// CopyFile 이 시각까지 옮기므로 복사해 둔 것은 정확히 같은 값이 된다.
static int SameFile(const wchar_t* a, const wchar_t* b)
{
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    if (!GetFileAttributesExW(a, GetFileExInfoStandard, &x)) return 0;
    if (!GetFileAttributesExW(b, GetFileExInfoStandard, &y)) return 0;
    return x.nFileSizeLow == y.nFileSizeLow
        && x.nFileSizeHigh == y.nFileSizeHigh
        && x.ftLastWriteTime.dwLowDateTime == y.ftLastWriteTime.dwLowDateTime
        && x.ftLastWriteTime.dwHighDateTime == y.ftLastWriteTime.dwHighDateTime;
}

static int FileExists(const wchar_t* p)
{
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

// ------------------------------------------------------------------ 세이브
//
// 세이브의 진행 포인터는 "몇 번째 퀘스트"가 아니라 "파일 안 몇 번째 파트"다. 그래서 모드를
// 갈아 끼우면 같은 숫자가 딴 자리를 가리킨다 — 파일 끝을 넘어가면 퀘스트가 더 안 뜨고,
// 남의 퀘스트 몸통 한가운데 떨어지면 받은 적 없는 의뢰의 완료보고가 뜬다.
// 여기서는 읽어서 진단하고, 사용자가 그러라고 할 때만 0(처음)으로 되돌린다.
#define SV_QNAME  0x25C61      // "C:EHT.CDS"
#define SV_QPTR   0x25D61      // 진행 포인터(u16)
#define SV_QDAYS  0x25C5D      // 남은 기한(u16)
#define SV_MIN    0x25E00

static void SavePath(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    GameDir(dir);
    JoinPath(out, dir, L"SAVEDATA.CDS");
}

// 세이브에서 쓰는 퀘스트 파일 이름과 진행 포인터를 읽는다. 성공 1.
static int ReadSaveInfo(wchar_t* file, int* ptr)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD got = 0, sz;
    unsigned char* buf;
    int ok = 0;

    file[0] = 0; *ptr = -1;
    SavePath(path);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz < SV_MIN) { CloseHandle(h); return 0; }
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return 0; }
    if (ReadFile(h, buf, sz, &got, NULL) && got == sz) {
        const char* s = (const char*)(buf + SV_QNAME);
        const char* nm = s;
        int i, k = 0;
        for (i = 0; i < 24 && s[i]; i++) if (s[i] == ':' || s[i] == '\\') nm = s + i + 1;
        while (nm[k] && k < 20 && (unsigned char)nm[k] > 0x20) { file[k] = (wchar_t)nm[k]; k++; }
        file[k] = 0;
        *ptr = buf[SV_QPTR] | (buf[SV_QPTR + 1] << 8);
        ok = file[0] != 0;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(h);
    return ok;
}

// 진행 포인터와 남은 기한을 갈아 끼운다. 고치기 전에 세이브를 한 번 복사해 둔다. 성공 1.
static int WritePointer(int part)
{
    wchar_t path[MAX_PATH], bak[MAX_PATH];
    HANDLE h;
    DWORD got = 0, put = 0, sz;
    unsigned char* buf;
    int ok = 0;

    SavePath(path);
    lstrcpyW(bak, path); lstrcatW(bak, L".questmod.bak");
    CopyFileW(path, bak, FALSE);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz < SV_MIN) { CloseHandle(h); return 0; }
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return 0; }
    if (ReadFile(h, buf, sz, &got, NULL) && got == sz) {
        buf[SV_QPTR]     = (unsigned char)part;
        buf[SV_QPTR + 1] = (unsigned char)(part >> 8);
        buf[SV_QDAYS]     = 0;          // 옛 모드에서 받아 둔 기한은 뜻이 없다
        buf[SV_QDAYS + 1] = 0;
        SetFilePointer(h, 0, NULL, FILE_BEGIN);
        ok = WriteFile(h, buf, sz, &put, NULL) && put == sz;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    CloseHandle(h);
    return ok;
}

// LS12 아카이브의 파트 수. 표를 세기만 하므로 풀지 않는다.
//   0x000 매직 16 + 0x010 사전 256 + 0x110 부터 12바이트짜리 파트 표, 4바이트 0 이면 끝.
static int PartCount(const wchar_t* path)
{
    HANDLE h;
    DWORD got = 0;
    unsigned char hdr[0x110 + 12 * 512];
    int n = 0, i;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(h, hdr, sizeof(hdr), &got, NULL)) got = 0;
    CloseHandle(h);
    if (got < 0x114 || hdr[0] != 'L' || hdr[1] != 's' || hdr[2] != '1' || hdr[3] != '2') return -1;
    for (i = 0x110; i + 12 <= (int)got; i += 12) {
        if (!hdr[i] && !hdr[i+1] && !hdr[i+2] && !hdr[i+3]) break;
        n++;
    }
    return n;
}

// ------------------------------------------------------------------ state

static void StateRead(wchar_t* out, int cch)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    char buf[256];
    DWORD got = 0;
    int i;

    out[0] = 0;
    StatePath(path);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(h);
    buf[got] = 0;
    for (i = 0; buf[i]; i++) if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, cch);
}

static void StateWrite(const wchar_t* name)
{
    wchar_t path[MAX_PATH];
    char buf[256];
    HANDLE h;
    DWORD put = 0;
    int n;

    StatePath(path);
    n = WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, sizeof(buf) - 2, NULL, NULL);
    if (n <= 0) return;
    buf[n - 1] = '\n';                 // 널 자리를 줄바꿈으로
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, (DWORD)n, &put, NULL);
    CloseHandle(h);
}

// ------------------------------------------------------------------ 모드 훑기

// mod.txt 를 읽는다. "base=" / "all=" 줄은 지정이고, 나머지 첫 줄이 설명이다.
//
//   all=quest.CDS                  이 파일 하나를 8직업 이름으로 전부 깐다
//   base=kseokjeong_quest_mod      이 모드를 깔기 전에 먼저 깔 모드
//   교회 대사를 손본 모드. 퀘스트패치 위에 얹는다.
//
// all= 은 8직업 파일이 같은 내용일 때 쓴다(퀘스트패치가 그렇다). 54KB 짜리 8개를 넣을
// 이유가 없어서 하나만 두고 이름만 바꿔 가며 깐다.
//
// 베이스를 적어 두면 quests.json 하나만 든 모드도 배포할 수 있다 — 주소(파트 번호)가 맞는
// 베이스를 먼저 깔아 주기 때문이다. 바닐라는 8직업 파일이 제각각이라 그렇게 못 한다.
static void ReadModTxt(const wchar_t* modDir, wchar_t* base, wchar_t* all, wchar_t* desc)
{
    wchar_t path[MAX_PATH], line[512];
    HANDLE h;
    char buf[2048];
    DWORD got = 0;
    int i, s0;

    base[0] = 0; all[0] = 0; desc[0] = 0;
    JoinPath(path, modDir, L"mod.txt");
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(h);
    buf[got] = 0;
    s0 = (got >= 3 && (unsigned char)buf[0] == 0xEF) ? 3 : 0;      // BOM 건너뛰기
    for (i = s0; ; i++) {
        if (buf[i] && buf[i] != '\r' && buf[i] != '\n') continue;
        if (i > s0) {
            char save = buf[i];
            buf[i] = 0;
            MultiByteToWideChar(CP_UTF8, 0, buf + s0, -1, line, 512);
            buf[i] = save;
            if ((line[0] == L'b' || line[0] == L'B') && line[1] == L'a' && line[2] == L's'
                && line[3] == L'e' && line[4] == L'=')
                lstrcpynW(base, line + 5, 64);
            else if ((line[0] == L'a' || line[0] == L'A') && line[1] == L'l' && line[2] == L'l'
                && line[3] == L'=')
                lstrcpynW(all, line + 4, 32);
            else if (!desc[0])
                lstrcpynW(desc, line, 256);
        }
        while (buf[i] == '\r' || buf[i] == '\n') i++;
        s0 = i;
        if (!buf[i]) break;
        i--;
    }
}

// 이 모드가 지금 그대로 깔려 있나. 파일 하나라도 다르면 아니다.
//
// 게임이 읽는 .CDS 는 CharacterUtilKR 이 [원본 + quests.json] 으로 다시 만들어 놓기 때문에
// 모드 파일과 크기부터 다르다. 그래서 원본(.orig)과 견준다. 아직 .orig 가 없으면(그 플러그인이
// 안 돌았거나 꺼져 있으면) .CDS 와 견준다.
// 8직업 퀘스트 파일 이름. all= 로 하나를 다 깔 때 쓰는 이름표다.
static const wchar_t* kJobFiles[8] = {
    L"ECQ.CDS", L"EDG.CDS", L"EEX.CDS", L"EHT.CDS",
    L"PCQ.CDS", L"PDG.CDS", L"PEX.CDS", L"PHT.CDS",
};

// 게임 폴더의 그 자리가 이 원본 파일과 같은가. .orig 가 있으면 그쪽과 견준다
// (게임이 읽는 .CDS 는 CharacterUtilKR 이 [원본 + quests.json] 으로 다시 만들어 놔서 다르다).
static int SlotHas(const wchar_t* game, const wchar_t* jobName, const wchar_t* src)
{
    wchar_t dst[MAX_PATH];
    JoinPath(dst, game, jobName);
    lstrcatW(dst, L".orig");
    if (FileExists(dst)) return SameFile(src, dst);
    JoinPath(dst, game, jobName);
    return SameFile(src, dst);
}

// 이 모드가 가진 .CDS 가 게임 폴더에 그대로 있나. quests.json 은 보지 않는다 —
// 그건 게임 안에서 고쳐 가며 쓰는 파일이라, 다르다고 해서 다시 깔면 고친 것이 날아간다.
static int FilesOk(const QMod* m)
{
    wchar_t game[MAX_PATH], mods[MAX_PATH], src[MAX_PATH], dst[MAX_PATH], modDir[MAX_PATH];
    int i;

    if (m->nfile <= 0) return 1;                 // json 만 든 모드 — 볼 파일이 없다
    GameDir(game);
    ModsDir(mods);
    JoinPath(modDir, mods, m->name);
    if (m->all[0]) {                             // 하나를 8직업에 다 깐 모드
        JoinPath(src, modDir, m->all);
        for (i = 0; i < 8; i++) if (!SlotHas(game, kJobFiles[i], src)) return 0;
        return 1;
    }
    for (i = 0; i < m->nfile; i++) {
        JoinPath(src, modDir, m->file[i]);
        if (!SlotHas(game, m->file[i], src)) return 0;
    }
    (void)dst;
    return 1;
}

static void Scan(void)
{
    wchar_t mods[MAX_PATH], pat[MAX_PATH], modDir[MAX_PATH], jsonPath[MAX_PATH], cur[64];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    g_nmod = 0;
    ModsDir(mods);
    CreateDirectoryW(mods, NULL);          // 없으면 만들어 둔다(어디에 넣는지 보이라고)
    StateRead(cur, 64);

    JoinPath(pat, mods, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        QMod* m;
        WIN32_FIND_DATAW f2;
        HANDLE h2;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        if (g_nmod >= MAX_MOD) break;

        m = &g_mods[g_nmod];
        ZeroMemory(m, sizeof(*m));
        lstrcpynW(m->name, fd.cFileName, 64);
        JoinPath(modDir, mods, m->name);
        ReadModTxt(modDir, m->base, m->all, m->desc);

        JoinPath(pat, modDir, L"*.CDS");
        h2 = FindFirstFileW(pat, &f2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (m->nfile >= MAX_FILE) break;
                lstrcpynW(m->file[m->nfile++], f2.cFileName, 32);
            } while (FindNextFileW(h2, &f2));
            FindClose(h2);
        }
        JoinPath(jsonPath, modDir, L"quests.json");
        m->hasJson = FileExists(jsonPath);
        m->applied = (lstrcmpiW(cur, m->name) == 0);   // 쓰기로 정해 둔 것이 이건가
        g_nmod++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    LogW(L"[QuestModKR] 모드 %d개 (지금 %s)", g_nmod, cur[0] ? cur : L"-");
}

static QMod* FindMod(const wchar_t* name)
{
    int i;
    for (i = 0; i < g_nmod; i++) if (lstrcmpiW(g_mods[i].name, name) == 0) return &g_mods[i];
    return NULL;
}

// ------------------------------------------------------------------ 적용

// switching 이면 quests.json 도 같이 옮긴다 — 지금 것을 전에 쓰던 모드 폴더로 되가져오고,
// 새 모드 것을 깔아 준다. 게임을 켤 때마다 도는 자동 적용에서는 건드리지 않는다(그때는
// 같은 모드라, 그동안 손으로 고친 quests.json 을 모드 폴더 것으로 덮으면 안 되기 때문).
static int ApplyMod(QMod* m, int switching, wchar_t* err)
{
    wchar_t game[MAX_PATH], mods[MAX_PATH], modDir[MAX_PATH];
    wchar_t src[MAX_PATH], dst[MAX_PATH], prev[64], live[MAX_PATH];
    int i;

    err[0] = 0;
    if (!m) return 0;
    if (m->nfile <= 0 && !m->hasJson) {
        lstrcpyW(err, L"이 모드 폴더가 비어 있습니다. .CDS 나 quests.json 을 넣으세요.");
        return 0;
    }
    // 베이스가 적혀 있으면 그것부터 깐다 — 편집만 든 모드(quests.json 하나)는 주소가 맞는
    // 베이스 위에서만 뜻이 있다. 층은 한 단만 쌓는다(베이스의 베이스는 안 본다).
    if (m->base[0]) {
        QMod* bm = FindMod(m->base);
        if (!bm) { wsprintfW(err, L"베이스 모드 \"%s\" 를 못 찾았습니다.", m->base); return 0; }
        if (bm->nfile > 0 && !FilesOk(bm)) {
            wchar_t berr[256];
            if (!ApplyMod(bm, 0, berr)) { lstrcpynW(err, berr, 256); return 0; }
        }
    }
    GameDir(game);
    ModsDir(mods);
    JoinPath(modDir, mods, m->name);
    JsonPath(live);
    StateRead(prev, 64);

    // 1) 쓰던 quests.json 을 전에 쓰던 모드 폴더에 돌려 놓는다(손으로 고친 것을 잃지 않게).
    if (switching && prev[0] && lstrcmpiW(prev, m->name) != 0 && FileExists(live)) {
        QMod* pm = FindMod(prev);
        if (pm) {
            JoinPath(dst, mods, prev);
            JoinPath(src, dst, L"quests.json");
            CopyFileW(live, src, FALSE);
        }
    }

    // 2) 모드의 .CDS 를 게임 폴더에 깔고, 옛 원본 표시를 지운다.
    //    .orig 가 없어야 CharacterUtilKR 이 새로 깔린 파일을 원본으로 잡는다.
    //    all= 이 있으면 그 파일 하나를 8직업 이름으로 다 깐다.
    {
        int n = m->all[0] ? 8 : m->nfile;
        for (i = 0; i < n; i++) {
            const wchar_t* job = m->all[0] ? kJobFiles[i] : m->file[i];
            JoinPath(src, modDir, m->all[0] ? m->all : m->file[i]);
            JoinPath(dst, game, job);
            if (!CopyFileW(src, dst, FALSE)) {
                wsprintfW(err, L"%s 를 게임 폴더에 넣지 못했습니다.\n게임이 파일을 잡고 있는지 확인하세요.",
                          job);
                return 0;
            }
            JoinPath(dst, game, job); lstrcatW(dst, L".orig");  DeleteFileW(dst);
            JoinPath(dst, game, job); lstrcatW(dst, L".stamp"); DeleteFileW(dst);
        }
    }

    // 3) 새 모드의 quests.json 을 깐다. 없으면 지운다(그 모드는 원본 그대로 쓰겠다는 뜻).
    if (switching) {
        JoinPath(src, modDir, L"quests.json");
        if (FileExists(src)) CopyFileW(src, live, FALSE);
        else                 DeleteFileW(live);
    }

    StateWrite(m->name);
    LogW(L"[QuestModKR] \"%s\" 적용 (%d개 파일%s)", m->name, m->nfile,
         switching ? L", quests.json 같이" : L"");
    return 1;
}

// 게임이 뜰 때. state 에 적힌 모드가 그대로 깔려 있으면 아무 것도 안 한다.
static void ApplyOnStart(void)
{
    wchar_t cur[64], err[256];
    QMod* m;

    StateRead(cur, 64);
    if (!cur[0]) return;
    Scan();
    m = FindMod(cur);
    if (!m) { LogW(L"[QuestModKR] state 의 \"%s\" 폴더가 없습니다.", cur); return; }
    // 파일이 그대로면 아무 것도 안 한다. 베이스가 있으면 그것까지 본다.
    {
        QMod* bm = m->base[0] ? FindMod(m->base) : NULL;
        if (FilesOk(m) && (!bm || FilesOk(bm))) {
            LogW(L"[QuestModKR] \"%s\" 이미 깔려 있음 — 건너뜀", cur);
            return;
        }
    }
    if (!ApplyMod(m, 0, err)) LogW(L"[QuestModKR] 자동 적용 실패: %s", err);
}

// ------------------------------------------------------------------ 창

#define WC_QMOD   L"QuestModKR_Window"
#define ID_LIST   1001
#define ID_APPLY  1002
#define ID_RELOAD 1003
#define ID_FOLDER 1004

static void FillList(void)
{
    int i;
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_nmod; i++) {
        QMod* m = &g_mods[i];
        LVITEMW it;
        wchar_t t[64];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.pszText = m->name; it.lParam = i;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        { LVITEMW s; s.iSubItem = 1; s.pszText = m->applied ? L"● 쓰는 중" : L"";
          SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        if (m->all[0])    wsprintfW(t, L"%s -> 8직업%s", m->all, m->hasJson ? L" + json" : L"");
        else if (m->nfile > 0) wsprintfW(t, L"%d개%s", m->nfile, m->hasJson ? L" + json" : L"");
        else if (m->hasJson) wsprintfW(t, L"quests.json%s%s", m->base[0] ? L" / 베이스 " : L"", m->base);
        else lstrcpyW(t, L"(비었음)");
        { LVITEMW s; s.iSubItem = 2; s.pszText = t;       SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        { LVITEMW s; s.iSubItem = 3; s.pszText = m->desc; SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        if (m->applied) ListView_SetItemState(g_list, i, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
    }
}

static void DoApply(HWND h)
{
    int sel = (int)SendMessageW(g_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    wchar_t err[256], msg[512], prev[64];
    QMod* m;

    if (sel < 0 || sel >= g_nmod) {
        MessageBoxW(h, L"목록에서 쓸 모드를 먼저 고르세요.", L"퀘스트 모드", MB_OK | MB_ICONINFORMATION);
        return;
    }
    m = &g_mods[sel];
    StateRead(prev, 64);
    if (m->applied && lstrcmpiW(prev, m->name) == 0) {
        wsprintfW(msg, L"[%s]\n\n이미 쓰고 있는 모드입니다.", m->name);
        MessageBoxW(h, msg, L"퀘스트 모드", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!ApplyMod(m, 1, err)) {
        MessageBoxW(h, err[0] ? err : L"적용하지 못했습니다.", L"퀘스트 모드", MB_OK | MB_ICONWARNING);
        return;
    }
    Scan();
    FillList();

    // 진행 포인터 손보기 — 세이브의 포인터는 파트 번호라 파일이 바뀌면 딴 자리를 가리킨다.
    {
        wchar_t save[64], game[MAX_PATH], qpath[MAX_PATH], where[256];
        int ptr = -1, parts;
        if (ReadSaveInfo(save, &ptr) && ptr >= 0) {
            GameDir(game);
            JoinPath(qpath, game, save);
            parts = PartCount(qpath);
            if (parts > 0) {
                if (ptr > parts - 1)
                    wsprintfW(where, L"새 파일은 파트가 %d개뿐이라 %d 번은 끝을 넘어갑니다.\n"
                                     L"이 세이브로는 퀘스트가 더 뜨지 않습니다.", parts, ptr);
                else if (ptr == 0)
                    lstrcpyW(where, L"처음(0)을 가리키고 있어 그대로 두어도 됩니다.");
                else
                    wsprintfW(where, L"새 파일에서 %d 번 파트는 다른 퀘스트의 한가운데일 수 있습니다.\n"
                                     L"받은 적 없는 의뢰의 완료보고가 뜨는 식으로 어긋납니다.", ptr);
                wsprintfW(msg,
                    L"세이브: %s · 진행 포인터 %d\n%s\n\n"
                    L"진행을 처음(0)으로 되돌릴까요?\n"
                    L"되돌리면 새 모드의 첫 퀘스트부터 시작합니다. 세이브는 고치기 전에\n"
                    L"SAVEDATA.CDS.questmod.bak 로 복사해 둡니다.",
                    save, ptr, where);
                if (MessageBoxW(h, msg, L"퀘스트 모드 — 진행 포인터", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (WritePointer(0))
                        MessageBoxW(h, L"진행을 처음으로 되돌렸습니다.\n\n"
                                       L"게임 안에서 저장하면 지금 메모리에 있는 옛 값이 다시 쓰여 되돌아갑니다.\n"
                                       L"저장하지 말고 바로 종료하세요.",
                                    L"퀘스트 모드", MB_OK | MB_ICONINFORMATION);
                    else
                        MessageBoxW(h, L"세이브를 고치지 못했습니다.", L"퀘스트 모드", MB_OK | MB_ICONWARNING);
                }
            }
        }
    }

    wsprintfW(msg,
        L"[%s] 을(를) 깔았습니다. (파일 %d개%s)\n\n"
        L"게임은 퀘스트 파일을 켤 때 읽으므로 껐다 켜야 반영됩니다.\n\n"
        L"지금 게임을 종료할까요?",
        m->name, m->all[0] ? 8 : m->nfile, m->hasJson ? L" + quests.json" : L"");
    if (MessageBoxW(h, msg, L"퀘스트 모드", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (g_gameHwnd) PostMessageW(g_gameHwnd, WM_CLOSE, 0, 0);
        ShowWindow(h, SW_HIDE);
    }
}

static LRESULT CALLBACK QModProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        const wchar_t* titles[4] = { L"모드", L"상태", L"파일", L"설명" };
        int widths[4] = { 220, 90, 150, 380 };
        LVCOLUMNW c;
        int i;
        g_list = CreateWindowExW(0, WC_LISTVIEW, L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                    0, 0, 10, 10, h, (HMENU)ID_LIST, g_hinst, NULL);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        for (i = 0; i < 4; i++) { c.pszText = (LPWSTR)titles[i]; c.cx = widths[i];
                                  SendMessageW(g_list, LVM_INSERTCOLUMNW, i, (LPARAM)&c); }
        CreateWindowExW(0, L"BUTTON", L"적용",      WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 0,0,10,10, h, (HMENU)ID_APPLY,  g_hinst, NULL);
        CreateWindowExW(0, L"BUTTON", L"다시 읽기", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
        CreateWindowExW(0, L"BUTTON", L"폴더 열기", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_FOLDER, g_hinst, NULL);
        Scan();
        FillList();
        return 0;
    }
    case WM_SIZE: {
        int cw = LOWORD(l), ch = HIWORD(l), bh = 34;
        MoveWindow(g_list, 0, 0, cw, ch - bh, TRUE);
        MoveWindow(GetDlgItem(h, ID_APPLY),  6,   ch-bh+4, 90, 26, TRUE);
        MoveWindow(GetDlgItem(h, ID_RELOAD), 102, ch-bh+4, 90, 26, TRUE);
        MoveWindow(GetDlgItem(h, ID_FOLDER), 198, ch-bh+4, 90, 26, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_APPLY)       DoApply(h);
        else if (LOWORD(w) == ID_RELOAD) { Scan(); FillList(); }
        else if (LOWORD(w) == ID_FOLDER) {
            wchar_t mods[MAX_PATH];
            ModsDir(mods);
            CreateDirectoryW(mods, NULL);
            ShellExecuteW(h, L"open", mods, NULL, NULL, SW_SHOWNORMAL);
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)l;
        if (nh->idFrom == ID_LIST && nh->code == NM_DBLCLK) DoApply(h);
        return 0;
    }
    case WM_CLOSE:   ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY: g_wnd = NULL; g_list = NULL; return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void ShowQModWindow(void)
{
    static BOOL reg = FALSE;
    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = QModProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_QMOD;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        g_wnd = CreateWindowExW(0, WC_QMOD,
                    L"퀘스트 모드 — CDS95Util\\mods 의 폴더를 골라 [적용] (게임을 껐다 켜야 반영)",
                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 420,
                    NULL, NULL, g_hinst, NULL);
    } else {
        Scan();
        FillList();
    }
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); SetForegroundWindow(g_wnd); }
}

// ------------------------------------------------------------------ 메뉴 붙이기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && LOWORD(w) == ID_QMOD_OPEN) { ShowQModWindow(); return 0; }
    if (m == WM_DESTROY && h == g_subHwnd) {
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)op);
        g_origProc = NULL; g_subHwnd = NULL; g_gameHwnd = NULL;
    }
    return op ? CallWindowProcW(op, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

static BOOL CALLBACK EnumProc(HWND h, LPARAM l)
{
    DWORD pid = 0;
    (void)l;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetMenu(h)) { g_gameHwnd = h; return FALSE; }
    return TRUE;
}

static HMENU FindFileMenu(HMENU bar)
{
    int n = GetMenuItemCount(bar), i;
    WCHAR s[64];
    for (i = 0; i < n; i++)
        if (GetMenuStringW(bar, (UINT)i, s, 64, MF_BYPOSITION) > 0 && s[0]==L'파' && s[1]==L'일')
            return GetSubMenu(bar, i);
    return NULL;
}

static BOOL HasOurItem(HMENU m)
{
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) if (GetMenuItemID(m, (UINT)i) == ID_QMOD_OPEN) return TRUE;
    return FALSE;
}

static DWORD WINAPI MenuThread(LPVOID p)
{
    (void)p;
    for (;;) {
        g_gameHwnd = NULL;
        EnumWindows(EnumProc, 0);
        if (g_gameHwnd) {
            HMENU bar = GetMenu(g_gameHwnd);
            if (bar) {
                HMENU fileMenu = FindFileMenu(bar);
                HMENU target = fileMenu ? fileMenu : bar;
                if (!HasOurItem(target)) {
                    AppendMenuW(target, MF_STRING, ID_QMOD_OPEN, L"퀘스트 모드");
                    DrawMenuBar(g_gameHwnd);
                    LogW(L"[QuestModKR] \"퀘스트 모드\" 메뉴 설치.");
                }
                if (g_subHwnd != g_gameHwnd) {
                    g_origProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)SubProc);
                    g_subHwnd = g_gameHwnd;
                }
            }
        }
        Sleep(1000);
    }
}

void QuestModKR_Init(HINSTANCE hinst)
{
    INITCOMMONCONTROLSEX ic;
    HANDLE t;
    g_hinst = hinst;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&ic);
    LogW(L"[QuestModKR] init.");
    // 퀘스트 파일을 먼저 제자리에 놔야 CharacterUtilKR 이 그것을 원본으로 잡는다.
    // 그래서 스레드로 미루지 않고 여기서 바로 한다(파일 몇 개 복사라 금방이다).
    ApplyOnStart();
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
