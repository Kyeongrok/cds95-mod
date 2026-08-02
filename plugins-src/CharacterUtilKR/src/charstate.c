#include "charstate.h"
#include "maids.h"
#include "patrons.h"

// PatchUtilKR 의 patches.state 와 같은 방식이다. 그쪽은 patch.c 안에 얽혀 있어 나누지 않고
// 여기 따로 뒀다(다루는 값이 파일오프셋이 아니라 표의 행 번호라 형식도 다르다).

static int g_haveBase = 0;
static int g_baseMaidYear[MAID_COUNT];
static unsigned g_baseMaidLang[MAID_COUNT];
static int g_basePatronYear[PATRON_COUNT];

// 되살리기 전의 값 = EXE 원본. 이걸 기준으로 "고친 것"만 골라 적는다.
static void CaptureBase(void)
{
    int i;
    if (g_haveBase) return;
    for (i = 0; i < MAID_COUNT; i++) {
        const MaidInfo* m = Maid_At(i);
        g_baseMaidYear[i] = m ? Maid_Year(m) : 0;
        g_baseMaidLang[i] = m ? m->lang : 0;
    }
    for (i = 0; i < PATRON_COUNT; i++) g_basePatronYear[i] = Patron_Year(i);
    g_haveBase = 1;
}

static void StatePath(HINSTANCE hinst, wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(hinst, out, cch);        // ...\CDS95Util\CharacterUtilKR.plugin
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    lstrcatW(out, L"character.state");
}

static char* ReadWholeFile(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0;
    char* buf;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 1024u * 1024) { CloseHandle(h); return NULL; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, buf, sz, &got, NULL)) { HeapFree(GetProcessHeap(), 0, buf); CloseHandle(h); return NULL; }
    buf[got] = 0;
    CloseHandle(h);
    return buf;
}

void CharState_Save(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    HANDLE f;
    DWORD wr;
    char line[128];
    int i, n;
    // /utf-8 로 컴파일하므로 좁은 문자열 리터럴이 그대로 UTF-8 이다.
    static const char hdr[] =
        "# CharacterUtilKR — 인물 창에서 고친 값. 게임을 다시 켜면 이대로 다시 쓴다.\r\n"
        "# 이 파일을 지우면 다음 실행부터 원본 값으로 돌아간다.\r\n"
        "# 항해사(생년·특기·승무원)는 세이브를 불러와야 생기는 값이라 여기 담지 않는다.\r\n";

    if (!g_haveBase) return;                    // 기준값을 모르면 무엇이 바뀐 건지 알 수 없다
    StatePath(hinst, path, MAX_PATH);
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { OutputDebugStringW(L"[CharacterUtilKR] character.state 쓰기 실패"); return; }
    WriteFile(f, hdr, (DWORD)(sizeof(hdr) - 1), &wr, NULL);

    for (i = 0; i < MAID_COUNT; i++) {
        const MaidInfo* m = Maid_At(i);
        if (!m) continue;
        if (Maid_Year(m) != g_baseMaidYear[i]) {
            n = wsprintfA(line, "maid.year\t%d\t%d\r\n", i, Maid_Year(m));
            WriteFile(f, line, (DWORD)n, &wr, NULL);
        }
        if (m->lang != g_baseMaidLang[i]) {
            n = wsprintfA(line, "maid.lang\t%d\t%u\r\n", i, m->lang);
            WriteFile(f, line, (DWORD)n, &wr, NULL);
        }
    }
    for (i = 0; i < PATRON_COUNT; i++) {
        int y = Patron_Year(i);
        if (y && y != g_basePatronYear[i]) {
            n = wsprintfA(line, "patron.year\t%d\t%d\r\n", i, y);
            WriteFile(f, line, (DWORD)n, &wr, NULL);
        }
    }
    CloseHandle(f);
}

// 언어는 비트마스크라 토글 API 밖에 없다. 지금 값과 목표 값의 다른 비트만 뒤집는다.
static void SetLang(int row, unsigned want)
{
    const MaidInfo* m = Maid_At(row);
    unsigned diff;
    int b;
    if (!m) return;
    diff = m->lang ^ want;
    for (b = 0; b < MAID_LANG_N; b++)
        if (diff & (1u << b)) Maid_ToggleLang(row, b);
}

void CharState_Apply(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    char* buf;
    char* p;
    int nMaid = 0, nPatron = 0;

    CaptureBase();                              // 되살리기 전 값이 원본이다
    StatePath(hinst, path, MAX_PATH);
    buf = ReadWholeFile(path);
    if (!buf) return;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    while (*p) {
        char* line = p;
        char* f2;
        char* f3;
        int row = 0, val = 0, k;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        { char* e = line + lstrlenA(line); while (e > line && (e[-1] == '\r' || e[-1] == ' ')) *--e = 0; }
        if (!line[0] || line[0] == '#') continue;

        f2 = line;  while (*f2 && *f2 != '\t') f2++;   if (!*f2) continue;  *f2++ = 0;
        f3 = f2;    while (*f3 && *f3 != '\t') f3++;   if (!*f3) continue;  *f3++ = 0;
        for (k = 0; f2[k] >= '0' && f2[k] <= '9'; k++) row = row * 10 + (f2[k] - '0');
        if (!k) continue;
        for (k = 0; f3[k] >= '0' && f3[k] <= '9'; k++) val = val * 10 + (f3[k] - '0');
        if (!k) continue;

        if (lstrcmpA(line, "maid.year") == 0) {
            if (row >= 0 && row < MAID_COUNT && val >= MAID_YEAR_MIN && val <= MAID_YEAR_MAX)
                if (Maid_SetYear(row, val)) nMaid++;
        } else if (lstrcmpA(line, "maid.lang") == 0) {
            if (row >= 0 && row < MAID_COUNT && val < (1 << MAID_LANG_N)) { SetLang(row, (unsigned)val); nMaid++; }
        } else if (lstrcmpA(line, "patron.year") == 0) {
            if (row >= 0 && row < PATRON_COUNT && Patron_SetYear(row, val)) nPatron++;
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (nMaid || nPatron) {
        wchar_t msg[128];
        wsprintfW(msg, L"[CharacterUtilKR] 지난번 값 복원: 여급 %d건, 스폰서 %d건", nMaid, nPatron);
        OutputDebugStringW(msg);
    }
}
