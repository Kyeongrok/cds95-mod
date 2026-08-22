#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include "update.h"
#include "modmenu.h"   // common/ — 모드 창 등록부(걷어 간 항목을 여기서 본다)

// UpdateUtilKR — 자세한 배치는 update.h 참고.
//
// 인터넷은 WinINet 으로만 만진다. 게임이 32비트라 요즘 라이브러리를 끌어오기 번거롭고,
// WinINet 은 윈도에 늘 있으며 HTTPS 와 리다이렉트를 알아서 처리한다(릴리즈 자산 주소는
// objects.githubusercontent.com 으로 넘어간다).
//
// zip 은 윈도10 에 들어 있는 tar.exe 로 푼다. 압축 라이브러리를 넣을 이유가 없다.

#define ID_UPD_OPEN 0xB900u   // Trade=0xB101/0xB102/0xC0xx, Char=0xB301, Ship=0xB410,
                              // Patch=0xB500, Map=0xB600, Mod=0xB700, QuestMod=0xB800 과 겹치지 않게.
#define MAX_REL     40
#define API_HOST    L"api.github.com"
#define API_PATH    L"/repos/Kyeongrok/cds95-mod/releases?per_page=40"
#define REL_PAGE    L"https://github.com/Kyeongrok/cds95-mod/releases"
#define UA          L"cds95-mod UpdateUtilKR"

typedef struct {
    wchar_t tag[32];         // v0.4.19
    wchar_t date[16];        // 2026-08-06
    wchar_t url[256];        // 자산(zip) 내려받을 주소
    int     size;            // 자산 크기
    int     cur;             // 지금 깔려 있는 판인가
} Rel;

static HINSTANCE g_hinst = NULL;
static Rel       g_rel[MAX_REL];
static int       g_nrel = 0;
static wchar_t   g_cur[32] = L"";          // update.state 에 적힌 태그
static wchar_t   g_err[256] = L"";
static HWND      g_wnd = NULL, g_list = NULL;
static HWND      g_gameHwnd = NULL, g_subHwnd = NULL;
static WNDPROC   g_origProc = NULL;

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
    UpToDataDir(out);
}

static void StatePath(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    PluginDir(dir);
    JoinPath(out, dir, L"update.state");
}

// 첫 줄만 떠 온다. 없으면 0.
static int ReadFirstLine(const wchar_t* path, wchar_t* out, int cch)
{
    HANDLE h;
    char buf[64];
    DWORD got = 0;
    int i;

    out[0] = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL)) got = 0;
    CloseHandle(h);
    buf[got] = 0;
    for (i = 0; buf[i]; i++) if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == ' ') { buf[i] = 0; break; }
    if (!buf[0]) return 0;
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, cch);
    return out[0] != 0;
}

// 태그끼리 견준다. VERSION 은 "0.4.24", 릴리즈 태그는 "v0.4.24" 라 v 를 떼고 본다.
static int SameTag(const wchar_t* a, const wchar_t* b)
{
    if (*a == L'v' || *a == L'V') a++;
    if (*b == L'v' || *b == L'V') b++;
    return lstrcmpiW(a, b) == 0;
}

// 지금 깔려 있는 판. VERSION 을 먼저 본다 — 릴리즈 zip 이 담아 오므로 손으로 풀어 넣었든
// 이 창으로 받았든 사실대로 적혀 있다. 그것이 없으면(옛 판) 이 창이 남긴 update.state 를 본다.
static void StateRead(void)
{
    wchar_t path[MAX_PATH], dir[MAX_PATH];

    g_cur[0] = 0;
    PluginDir(dir);
    JoinPath(path, dir, L"VERSION");
    if (!ReadFirstLine(path, g_cur, 32)) {
        StatePath(path);
        ReadFirstLine(path, g_cur, 32);
    }
    if (g_cur[0] && g_cur[0] != L'v' && g_cur[0] != L'V') {   // "0.4.24" → "v0.4.24"
        wchar_t t[32];
        lstrcpynW(t, g_cur, 31);
        wsprintfW(g_cur, L"v%s", t);
    }
}

static void WriteLine(const wchar_t* path, const wchar_t* text)
{
    char buf[64];
    HANDLE h;
    DWORD put = 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, sizeof(buf) - 2, NULL, NULL);
    if (n <= 0) return;
    buf[n - 1] = '\n';
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, (DWORD)n, &put, NULL);
    CloseHandle(h);
}

// 깔고 나서 남긴다. VERSION 도 함께 고쳐 둔다 — 옛 판으로 되돌리면 그 zip 에는 VERSION 이
// 없어 옛 파일이 그대로 남고, 그러면 창이 엉뚱한 판을 지금 것이라 우기게 된다.
static void StateWrite(const wchar_t* tag)
{
    wchar_t path[MAX_PATH], dir[MAX_PATH];

    StatePath(path);
    WriteLine(path, tag);
    PluginDir(dir);
    JoinPath(path, dir, L"VERSION");
    WriteLine(path, tag);
    lstrcpynW(g_cur, tag, 32);
}

// ------------------------------------------------------------------ 인터넷

// 주소 하나를 통째로 읽어 온다. 성공하면 힙에 담아 돌려주고(널로 끝냄) 길이를 낸다.
// 파일 이름을 주면 그리로 바로 흘려 쓴다(zip 은 메모리에 들 이유가 없다).
static char* Fetch(const wchar_t* url, const wchar_t* toFile, DWORD* outLen)
{
    HINTERNET hi = NULL, hu = NULL;
    char* buf = NULL;
    DWORD cap = 0, len = 0, got = 0;
    HANDLE f = INVALID_HANDLE_VALUE;
    char chunk[16384];

    if (outLen) *outLen = 0;
    hi = InternetOpenW(UA, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hi) { lstrcpyW(g_err, L"인터넷을 열지 못했습니다."); return NULL; }
    // 캐시를 타면 방금 낸 릴리즈가 안 보인다. 늘 새로 받는다.
    hu = InternetOpenUrlW(hi, url, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                          INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                          INTERNET_FLAG_SECURE | INTERNET_FLAG_KEEP_CONNECTION, 0);
    if (!hu) {
        wsprintfW(g_err, L"주소를 열지 못했습니다(오류 %lu).\n인터넷 연결이나 방화벽을 확인하세요.",
                  GetLastError());
        InternetCloseHandle(hi);
        return NULL;
    }
    {   // HTTP 상태 확인 — 404/403 이면 본문 대신 오류로 다룬다.
        DWORD code = 0, sz = sizeof(code), idx = 0;
        if (HttpQueryInfoW(hu, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &sz, &idx)
            && code >= 400) {
            wsprintfW(g_err, L"서버가 %lu 를 냈습니다.", code);
            InternetCloseHandle(hu); InternetCloseHandle(hi);
            return NULL;
        }
    }
    if (toFile) {
        f = CreateFileW(toFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) {
            lstrcpyW(g_err, L"받을 파일을 만들지 못했습니다.");
            InternetCloseHandle(hu); InternetCloseHandle(hi);
            return NULL;
        }
    }
    for (;;) {
        if (!InternetReadFile(hu, chunk, sizeof(chunk), &got) || got == 0) break;
        if (toFile) {
            DWORD put = 0;
            if (!WriteFile(f, chunk, got, &put, NULL) || put != got) break;
            len += got;
        } else {
            if (len + got + 1 > cap) {
                DWORD nc = cap ? cap * 2 : 65536;
                char* nb;
                while (nc < len + got + 1) nc *= 2;
                nb = (char*)(buf ? HeapReAlloc(GetProcessHeap(), 0, buf, nc)
                                 : HeapAlloc(GetProcessHeap(), 0, nc));
                if (!nb) break;
                buf = nb; cap = nc;
            }
            memcpy(buf + len, chunk, got);
            len += got;
        }
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    InternetCloseHandle(hu);
    InternetCloseHandle(hi);
    if (buf) buf[len] = 0;
    if (outLen) *outLen = len;
    return buf;
}

// ------------------------------------------------------------------ JSON 훑기
//
// 온전한 파서를 두지 않는다. 필요한 것이 태그·날짜·자산 주소뿐이고 GitHub 응답은 순서가
// 고정이라, 키를 앞에서부터 찾아 값만 떠 내면 된다(questjson.c 와 같은 방식).

static const char* FindKey(const char* p, const char* end, const char* key)
{
    int n = lstrlenA(key);
    for (; p + n < end; p++) if (p[0] == '"' && memcmp(p + 1, key, n) == 0) return p + 1 + n;
    return NULL;
}

// "키": "값" 에서 값을 떠 낸다. p 는 키 뒤를 가리킨다.
static const char* GrabStr(const char* p, const char* end, wchar_t* out, int cch)
{
    const char* s;
    char tmp[512];
    int n = 0;
    out[0] = 0;
    while (p < end && *p != '"') { if (*p == ',' || *p == '}') return p; p++; }
    if (p >= end) return p;
    s = ++p;
    while (p < end && *p != '"' && n < (int)sizeof(tmp) - 1) { tmp[n++] = *p++; }
    tmp[n] = 0;
    (void)s;
    MultiByteToWideChar(CP_UTF8, 0, tmp, -1, out, cch);
    return p;
}

static int GrabInt(const char* p, const char* end)
{
    int v = 0;
    while (p < end && (*p < '0' || *p > '9')) { if (*p == '"') return 0; p++; }
    while (p < end && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

static int EndsWithZip(const wchar_t* s)
{
    int n = lstrlenW(s);
    return n > 4 && (s[n-4] == L'.') && (s[n-3] == L'z' || s[n-3] == L'Z')
        && (s[n-2] == L'i' || s[n-2] == L'I') && (s[n-1] == L'p' || s[n-1] == L'P');
}

static void ParseReleases(const char* js, DWORD len)
{
    const char* p = js;
    const char* end = js + len;

    g_nrel = 0;
    for (;;) {
        const char* t = FindKey(p, end, "tag_name\"");
        const char* nextTag;
        Rel* r;
        if (!t || g_nrel >= MAX_REL) break;
        r = &g_rel[g_nrel];
        ZeroMemory(r, sizeof(*r));
        p = GrabStr(t, end, r->tag, 32);
        nextTag = FindKey(p, end, "tag_name\"");        // 이 릴리즈의 끝 = 다음 태그 앞
        if (!nextTag) nextTag = end;

        {   // 날짜는 앞 10자만 쓴다(2026-08-06T07:47:53Z)
            const char* d = FindKey(p, nextTag, "published_at\"");
            if (d) { wchar_t full[40]; GrabStr(d, nextTag, full, 40); full[10] = 0; lstrcpynW(r->date, full, 16); }
        }
        {   // 자산 — .zip 으로 끝나는 첫 주소. 크기는 그 앞에 나오는 "size" 다.
            const char* q = p;
            while (q < nextTag) {
                const char* s = FindKey(q, nextTag, "size\"");
                const char* u = FindKey(q, nextTag, "browser_download_url\"");
                int sz = 0;
                wchar_t url[256];
                if (!u) break;
                if (s && s < u) sz = GrabInt(s, nextTag);
                q = GrabStr(u, nextTag, url, 256);
                if (EndsWithZip(url)) { lstrcpynW(r->url, url, 256); r->size = sz; break; }
            }
        }
        if (r->tag[0] && r->url[0]) {
            r->cur = (g_cur[0] && SameTag(g_cur, r->tag));
            g_nrel++;
        }
        p = nextTag;
    }
}

static int LoadReleases(void)
{
    wchar_t url[256];
    char* js;
    DWORD len = 0;

    g_err[0] = 0;
    StateRead();
    wsprintfW(url, L"https://%s%s", API_HOST, API_PATH);
    js = Fetch(url, NULL, &len);
    if (!js) return 0;
    ParseReleases(js, len);
    HeapFree(GetProcessHeap(), 0, js);
    if (!g_nrel) { lstrcpyW(g_err, L"릴리즈 목록을 읽지 못했습니다(응답 형식이 다릅니다)."); return 0; }
    LogW(L"[UpdateUtilKR] 릴리즈 %d개, 지금 %s", g_nrel, g_cur[0] ? g_cur : L"(모름)");
    return 1;
}

// ------------------------------------------------------------------ 깔기

// zip 을 푼다. 윈도10 의 tar.exe 가 zip 을 안다. 성공 1.
static int Unzip(const wchar_t* zip, const wchar_t* dir)
{
    wchar_t cmd[MAX_PATH * 3], sys[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    GetSystemDirectoryW(sys, MAX_PATH);
    wsprintfW(cmd, L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys, zip, dir);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        lstrcpyW(g_err, L"tar.exe 를 실행하지 못했습니다(윈도10 이상이 필요합니다).");
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) { wsprintfW(g_err, L"압축을 푸는 데 실패했습니다(코드 %lu).", code); return 0; }
    return 1;
}

static int IsPlugin(const wchar_t* name)
{
    int n = lstrlenW(name);
    return n > 7 && lstrcmpiW(name + n - 7, L".plugin") == 0;
}

// 푼 폴더의 파일을 CDS95Util 로 옮긴다. 옮긴 개수를 낸다.
//
//  .plugin  늘 덮어쓴다. 지금 쓰고 있어 못 덮으면 이름을 밀어내고 새것을 놓는다
//           (윈도는 열려 있는 이미지의 이름 바꾸기를 허용한다). 반영은 다음 실행부터.
//  .json    건드리지 않는다 — 사용자가 고쳐 쓰는 파일이다. 새것은 <이름>.new 로 옆에 둔다.
// 하위 폴더까지 그대로 따라 깐다 — zip 이 plugins\<만든이>\ 와 mods\ 를 담고 있어서다.
static int InstallTree(const wchar_t* srcDir, const wchar_t* dstDir, const wchar_t* tag, int* skipped)
{
    wchar_t pat[MAX_PATH], from[MAX_PATH], to[MAX_PATH], bak[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;

    JoinPath(pat, srcDir, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { lstrcpyW(g_err, L"푼 폴더가 비어 있습니다."); return 0; }
    do {
        JoinPath(from, srcDir, fd.cFileName);
        JoinPath(to, dstDir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == L'.') continue;
            CreateDirectoryW(to, NULL);
            n += InstallTree(from, to, tag, skipped);
            continue;
        }
        // VERSION 은 사람이 고쳐 쓰는 파일이 아니라 "지금 무엇이 깔려 있나" 그 자체다. 늘 덮어쓴다.
        if (!IsPlugin(fd.cFileName) && lstrcmpiW(fd.cFileName, L"VERSION") != 0) {
            if (GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES) {
                if (CopyFileW(from, to, FALSE)) n++;
            } else {
                lstrcpyW(bak, to); lstrcatW(bak, L".new");
                CopyFileW(from, bak, FALSE);
                (*skipped)++;
            }
            continue;
        }
        if (CopyFileW(from, to, FALSE)) { n++; continue; }
        // 쓰는 중이라 못 덮었다 — 이름을 밀어내고 새것을 놓는다.
        wsprintfW(bak, L"%s.old-%s", to, tag);
        DeleteFileW(bak);
        if (MoveFileW(to, bak) && CopyFileW(from, to, FALSE)) n++;
        else (*skipped)++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

// 고른 판을 받아 깐다. 성공 1.
static int Install(const Rel* r, int* copied, int* skipped)
{
    wchar_t tmp[MAX_PATH], zip[MAX_PATH], dir[MAX_PATH], inner[MAX_PATH];
    DWORD len = 0;

    g_err[0] = 0;
    *copied = *skipped = 0;
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(dir, L"%scds95upd", tmp);
    wsprintfW(zip, L"%s\\%s.zip", dir, r->tag);
    CreateDirectoryW(dir, NULL);

    if (!Fetch(r->url, zip, &len) && len == 0) return 0;
    if (len < 1024) { lstrcpyW(g_err, L"받은 파일이 너무 작습니다."); return 0; }
    if (!Unzip(zip, dir)) return 0;

    // zip 안에 CDS95Util\ 이 들어 있다. 없으면 푼 폴더를 그대로 쓴다.
    JoinPath(inner, dir, L"CDS95Util");
    if (GetFileAttributesW(inner) == INVALID_FILE_ATTRIBUTES) lstrcpyW(inner, dir);

    {
        wchar_t root[MAX_PATH];
        PluginDir(root);                       // CDS95Util (plugins\<만든이>\ 에 있어도 루트로 온다)
        *skipped = 0;
        *copied = InstallTree(inner, root, r->tag, skipped);
    }
    if (*copied <= 0) { if (!g_err[0]) lstrcpyW(g_err, L"옮긴 파일이 없습니다."); return 0; }
    StateWrite(r->tag);
    LogW(L"[UpdateUtilKR] %s 깔았음 — %d개(%d개 건너뜀)", r->tag, *copied, *skipped);
    return 1;
}

// ------------------------------------------------------------------ 창

#define WC_UPD    L"UpdateUtilKR_Window"
#define ID_LIST   1001
#define ID_APPLY  1002
#define ID_RELOAD 1003
#define ID_PAGE   1004

static void FillList(void)
{
    int i;
    SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_nrel; i++) {
        Rel* r = &g_rel[i];
        LVITEMW it;
        wchar_t t[64];
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i; it.pszText = r->tag; it.lParam = i;
        SendMessageW(g_list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        { LVITEMW s; s.iSubItem = 1; s.pszText = r->date; SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        wsprintfW(t, L"%d KB", (r->size + 512) / 1024);
        { LVITEMW s; s.iSubItem = 2; s.pszText = t; SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
        if (r->cur)      lstrcpyW(t, i == 0 ? L"● 지금 이 판 (최신)" : L"● 지금 이 판");
        else if (i == 0) lstrcpyW(t, g_cur[0] ? L"최신 — 올릴 수 있음" : L"최신");
        else             t[0] = 0;
        { LVITEMW s; s.iSubItem = 3; s.pszText = t; SendMessageW(g_list, LVM_SETITEMTEXTW, i, (LPARAM)&s); }
    }
    // 고른 것이 없으면 맨 위(최신)를 잡아 둔다 — 기본은 늘 최신이다.
    if (g_nrel > 0) {
        int sel = 0, k;
        for (k = 0; k < g_nrel; k++) if (g_rel[k].cur) { sel = k; break; }
        ListView_SetItemState(g_list, sel, LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
    }
}

// 지금 쓰는 판을 제목에 박아 둔다 — 목록만 봐서는 어느 것이 내 것인지 모른다.
static void SetTitle(HWND h)
{
    wchar_t t[160];
    wsprintfW(t, L"업데이트 — 지금 %s (문제 있으면 옛 판으로 되돌릴 수 있다)",
              g_cur[0] ? g_cur : L"쓰는 판 모름");
    SetWindowTextW(h, t);
}

static void DoReload(HWND h)
{
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    if (!LoadReleases()) {
        SendMessageW(g_list, LVM_DELETEALLITEMS, 0, 0);
        MessageBoxW(h, g_err[0] ? g_err : L"목록을 받지 못했습니다.", L"업데이트", MB_OK | MB_ICONWARNING);
    } else FillList();
    SetTitle(h);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
}

static void DoApply(HWND h)
{
    int sel = (int)SendMessageW(g_list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    wchar_t msg[640];
    int copied = 0, skipped = 0;
    Rel* r;

    if (sel < 0 || sel >= g_nrel) {
        MessageBoxW(h, L"목록에서 판을 먼저 고르세요.", L"업데이트", MB_OK | MB_ICONINFORMATION);
        return;
    }
    r = &g_rel[sel];
    wsprintfW(msg, L"[%s] (%s · %d KB) 을(를) 받아서 깝니다.\n\n"
                   L"플러그인 파일만 바뀝니다. cities.json 처럼 손대 쓰는 파일과\n"
                   L"quests.json · mods\\ 는 건드리지 않습니다.\n\n계속할까요?",
              r->tag, r->date, (r->size + 512) / 1024);
    if (MessageBoxW(h, msg, L"업데이트", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    {
        int ok = Install(r, &copied, &skipped);
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_ARROW));
        if (!ok) {
            MessageBoxW(h, g_err[0] ? g_err : L"깔지 못했습니다.", L"업데이트", MB_OK | MB_ICONWARNING);
            return;
        }
    }
    FillList();
    SetTitle(h);
    wsprintfW(msg,
        L"[%s] 을(를) 깔았습니다. (파일 %d개%s)\n\n"
        L"쓰고 있던 플러그인은 <이름>.plugin.old-%s 로 밀어냈습니다.\n"
        L"게임을 껐다 켜야 새 판이 뜹니다.\n\n지금 게임을 종료할까요?",
        r->tag, copied,
        skipped ? L", 건드리지 않은 설정 파일은 <이름>.new 로 뒀습니다" : L"", r->tag);
    if (MessageBoxW(h, msg, L"업데이트", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        if (g_gameHwnd) PostMessageW(g_gameHwnd, WM_CLOSE, 0, 0);
        ShowWindow(h, SW_HIDE);
    }
}

static LRESULT CALLBACK UpdProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        const wchar_t* titles[4] = { L"판", L"올린 날", L"크기", L"상태" };
        int widths[4] = { 120, 130, 90, 170 };
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
        CreateWindowExW(0, L"BUTTON", L"이 판으로", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 0,0,10,10, h, (HMENU)ID_APPLY,  g_hinst, NULL);
        CreateWindowExW(0, L"BUTTON", L"다시 읽기", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_RELOAD, g_hinst, NULL);
        CreateWindowExW(0, L"BUTTON", L"릴리즈 페이지", WS_CHILD|WS_VISIBLE, 0,0,10,10, h, (HMENU)ID_PAGE, g_hinst, NULL);
        return 0;
    }
    case WM_SIZE: {
        int cw = LOWORD(l), ch = HIWORD(l), bh = 34;
        MoveWindow(g_list, 0, 0, cw, ch - bh, TRUE);
        MoveWindow(GetDlgItem(h, ID_APPLY),  6,   ch-bh+4, 100, 26, TRUE);
        MoveWindow(GetDlgItem(h, ID_RELOAD), 112, ch-bh+4, 100, 26, TRUE);
        MoveWindow(GetDlgItem(h, ID_PAGE),   218, ch-bh+4, 120, 26, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_APPLY)       DoApply(h);
        else if (LOWORD(w) == ID_RELOAD) DoReload(h);
        else if (LOWORD(w) == ID_PAGE)   ShellExecuteW(h, L"open", REL_PAGE, NULL, NULL, SW_SHOWNORMAL);
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

static void ShowUpdWindow(void)
{
    static BOOL reg = FALSE;
    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = UpdProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_UPD;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        g_wnd = CreateWindowExW(0, WC_UPD,
                    L"업데이트",
                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 620, 420,
                    NULL, NULL, g_hinst, NULL);
    }
    if (g_wnd) {
        SetTitle(g_wnd);
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
        DoReload(g_wnd);          // 창을 열 때마다 목록을 새로 받는다
    }
}

// ------------------------------------------------------------------ 메뉴 붙이기

static LRESULT CALLBACK SubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC op = g_origProc;
    if (m == WM_COMMAND && LOWORD(w) == ID_UPD_OPEN) { ShowUpdWindow(); return 0; }
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
    // 하위 메뉴까지 내려가며 본다. MenuTidyKR 이 우리 항목을 "모드" 아래로 옮기므로,
    // 파일 메뉴만 훑으면 늘 "없다" 가 나와 1초마다 또 달게 된다.
    int n = GetMenuItemCount(m), i;
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, (UINT)i);
        if (sub) { if (HasOurItem(sub)) return TRUE; continue; }
        if (GetMenuItemID(m, (UINT)i) == ID_UPD_OPEN) return TRUE;
    }
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
                if (!HasOurItem(target) && !ModMenu_HasId(g_gameHwnd, ID_UPD_OPEN)) {
                    AppendMenuW(target, MF_STRING, ID_UPD_OPEN, L"업데이트");
                    DrawMenuBar(g_gameHwnd);
                    LogW(L"[UpdateUtilKR] \"업데이트\" 메뉴 설치.");
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

void UpdateKR_Init(HINSTANCE hinst)
{
    INITCOMMONCONTROLSEX ic;
    HANDLE t;
    g_hinst = hinst;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&ic);
    StateRead();
    LogW(L"[UpdateUtilKR] init (지금 %s).", g_cur[0] ? g_cur : L"모름");
    // 인터넷은 창을 열 때만 만진다 — 게임 뜨는 길에 네트워크를 붙들지 않는다.
    t = CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
