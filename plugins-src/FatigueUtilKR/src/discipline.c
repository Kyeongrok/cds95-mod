#include <windows.h>
#include <string.h>
#include "discipline.h"
#include "fleetmem.h"
#include "gameday.h"
#include "ovlflag.h"    // common/ — 화면 오버레이 켬/끔(그리는 쪽은 WindArrowKR)

// DisciplineKR — 규율을 보는 창.
//
// 규율(통합수정판 용어. 게임 원문은 "규칙")은 함대 정보의 둘째 칸 0x5B3954 다.
// 하루가 갈 때마다 깎이고 바닥이 나면 반란이 난다. 게임 화면 어디에도 이 숫자가 안 나와서
// 만든 창이다. 셈은 옵시디안 Project/cds95/분석/항해/70.분석-규율(대원 불만과 하루 셈).md
// 에 적힌 그대로다 —
//
//   뭍의 하루 (0x475470, [0x5B61B4] != 0 일 때)
//       규율 += 운용술 − (여행비를 내고도 소지금이 남으면 4, 아니면 10)
//   바다의 하루 (0x475810 부터)
//       규율 += 항해술 − (지금 칸이 근해·원양이면 6, 그 밖이면 3) − 고위도 벌점(0~3)
//
// 고위도 벌점은 0x475587~0x4755DC 다 — |위도 − 10000| 이 7222 · 7777 · 8333 을 넘을
// 때마다 1 씩 붙는다(위도 20000 이 180도이므로 대략 남·북위 65 · 70 · 75도).
// 70번 노트가 "[esp+0x14] 는 못 짚었다" 고 남긴 자리가 이것이다.
//
// ★ 지형이 규율을 더 깎지는 않는다. 산 · 사막에서 빨리 주는 것처럼 보이는 것은 한 칸에
//   드는 눈금이 커서(산 7 · 숲 5 · 사막 4 · 육지 2 · 바다와 강 1, 48눈금이 하루)
//   하루가 그만큼 빨리 오기 때문이다. 그래서 이 창은 지금 칸의 눈금도 함께 보여 준다.
//
// 값을 고치지는 않는다 — 보려고 만든 창이다(피로도 창은 같은 DLL 의 fatigue.c).

#define ID_TITLE   2001
#define ID_VALUE   2002
#define ID_DATE    2003
#define ID_PLACE   2004
#define ID_SKILL   2005
#define ID_RATE    2006
#define ID_LOGLBL  2007
#define ID_LIST    2008
#define ID_OVL     2009
#define ID_NOTE    2010
#define ID_CLEAR   2011
#define ID_CLOSE   2012

#define DISC_MAX   100

#define WC_DISC    L"DisciplineUtilKR_Window"
#define CLIENT_W   500
#define CLIENT_H   506

#define BAR_X      16
#define BAR_Y      66
#define BAR_W      (CLIENT_W - 32)
#define BAR_H      20

#define LOG_N      64          // 링버퍼. 넘치면 오래된 것부터 밀린다

typedef struct {
    int year, month, day, tick;
    int val, delta;
    int terr;                  // 칸 종류 0~6, 모르면 -1
    int land;                  // 뭍(말)이면 1, 바다면 0, 모르면 -1
} DiscLog;

static HINSTANCE g_hinst = NULL;
static HWND      g_wnd = NULL;
static HWND      g_gameHwnd = NULL;    // 오버레이 켬/끔은 이 창의 프로퍼티에 걸린다
static int       g_ovlRestored = 0;
static HFONT     g_font = NULL, g_big = NULL, g_mono = NULL;
static int       g_lastShown = -2;     // 막대에 그려 둔 규율 값

static CRITICAL_SECTION g_cs;
static int       g_csReady = 0;
static DiscLog   g_log[LOG_N];
static int       g_logHead = 0;        // 다음에 쓸 자리
static int       g_logCount = 0;       // 링에 든 줄 수
static int       g_logSeq = 0;         // 창이 "새 줄이 생겼나" 를 보는 번호
static int       g_shownSeq = -1;
static int       g_watchVal = -2;      // 감시 스레드가 마지막으로 본 규율

static void LogW(const wchar_t* s) { OutputDebugStringW(s); }

// ------------------------------------------------------------------ 게임 자리 읽기

static unsigned char* Base(void) { return (unsigned char*)GetModuleHandleW(NULL); }

// 읽기 전용 자리(.rdata 의 표들)도 봐야 해서 fleetmem.h 의 것과 따로 둔다 —
// 저쪽은 쓰기가 되는 자리만 통과시킨다.
static int Readable(const void* p, SIZE_T n)
{
    const unsigned char* q = (const unsigned char*)p;
    const unsigned char* end;
    if (!p) return 0;
    end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

// 검사와 읽기 사이에 자리가 사라질 수 있다 — 게임은 도시에 들고 날 때 큰 자리를 놓았다
// 다시 잡는다. 그때 마침 읽으면 게임이 통째로 꺼지므로, 읽기는 모두 이 문을 지난다.
static int SafeReadInt(const void* p, int* out)
{
    if (!Readable(p, 4)) return 0;
    __try { *out = *(const int*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

static int SafeReadU16(const void* p, unsigned* out)
{
    if (!Readable(p, 2)) return 0;
    __try { *out = *(const unsigned short*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

static int SafeReadByte(const void* p, unsigned char* out)
{
    if (!Readable(p, 1)) return 0;
    __try { *out = *(const unsigned char*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

static int SafeReadPtr(const void* p, void** out)
{
    if (!Readable(p, sizeof(void*))) return 0;
    __try { *out = *(void* const*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

static int SafeReadBytes(const void* p, void* dst, int n)
{
    if (!Readable(p, (SIZE_T)n)) return 0;
    __try { CopyMemory(dst, p, (SIZE_T)n); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

static int DisciplineGet(void) { return FleetGet(FLEET_DISCIPLINE_RVA); }

// 연 · 월 · 일 · 눈금. 말이 안 되면 0(세이브를 아직 안 불러온 것으로 본다).
static int CalGet(int* y, int* m, int* d, int* tick)
{
    const unsigned char* c;
    unsigned char* b = Base();
    if (!b) return 0;
    c = b + GD_CAL_RVA;
    if (!SafeReadInt(c + GD_CAL_YEAR,  y)    ||
        !SafeReadInt(c + GD_CAL_MONTH, m)    ||
        !SafeReadInt(c + GD_CAL_DAY,   d)    ||
        !SafeReadInt(c + GD_CAL_TICK,  tick)) return 0;
    if (*y < 1400 || *y > 1700) return 0;
    if (*m < 1 || *m > 12 || *d < 1 || *d > 31) return 0;
    if (*tick < 0 || *tick >= GD_TICKS_PER_DAY) return 0;
    return 1;
}

// 뭍(말)이면 1, 바다면 0, 모르면 -1.
static int OnLand(void)
{
    unsigned char* b = Base();
    int v;
    if (!b || !SafeReadInt(b + GD_HORSE_RVA, &v)) return -1;
    return v ? 1 : 0;
}

static int MoneyMirror(void)
{
    unsigned char* b = Base();
    int v;
    if (!b || !SafeReadInt(b + GD_MONEY_RVA, &v)) return -1;
    return v;
}

// 지도(항해 · 육상) 화면이면 1. 도시 안이나 시설 화면이면 0.
static int OnMapScreen(void)
{
    unsigned char* b = Base();
    unsigned char v;
    if (!b || !SafeReadByte(b + GD_CAL_RVA, &v)) return 0;
    return (v & GD_SCREEN_MAP_MASK) ? 1 : 0;
}

// 지금 함대가 서 있는 칸의 종류(0~6). 못 읽으면 -1.
// 게임 함수 0x426740 과 같은 셈을 여기서 한다 — 배열을 읽기만 하므로 어느 스레드에서
// 불러도 게임과 부딪히지 않는다.
static int TerrainNow(void)
{
    unsigned char* b = Base();
    const unsigned short* w;
    const unsigned char* lut;
    unsigned char kind;
    int lon, lat, cx, cy, t;
    unsigned v;

    if (!b) return -1;
    if (!OnMapScreen()) return -1;        // 도시 안에서는 그 배열을 묻지 않는다
    if (!SafeReadInt(b + GD_LON_RVA, &lon) || !SafeReadInt(b + GD_LAT_RVA, &lat)) return -1;
    if (lon < 0 || lon > 40000 || lat < 0 || lat > 20000) return -1;
    cx = lon / 16; cy = lat / 16;
    if (cx < 0 || cx >= GD_WORLD_STRIDE || cy < 0 || cy >= GD_WORLD_ROWS) return -1;

    if (!SafeReadPtr(b + GD_WORLD_OBJ_RVA + GD_WORLD_PTR_OFF, (void**)&w)) return -1;
    if (!w) return -1;
    w += (SIZE_T)cy * GD_WORLD_STRIDE + cx;
    if (!SafeReadU16(w, &v)) return -1;
    v &= 0x3FFFu;

    lut = b + GD_CELL_LUT_RVA;
    if (!SafeReadByte(lut + v, &kind)) return -1;
    t = kind;
    return (t >= 0 && t < GD_TERR_N) ? t : -1;
}

// 그 칸을 지나는 데 드는 눈금(0x53C330 + 종류*8). 모르면 -1.
static int TerrainTicks(int t)
{
    unsigned char* b = Base();
    const int* p;
    int v;
    if (t < 0 || t >= GD_TERR_N || !b) return -1;
    p = (const int*)(b + GD_TERR_TICK_RVA + (unsigned)t * 8);
    if (!SafeReadInt(p, &v)) return -1;
    if (v < 1 || v > 99) return -1;
    return v;
}

// 게임의 지형 이름표(0x56F810, cp949 8바이트 고정폭). 앞뒤 공백을 떼고 돌려준다.
static const wchar_t* TerrainName(int t)
{
    static wchar_t cache[GD_TERR_N][16];
    static int done[GD_TERR_N];
    unsigned char* b = Base();
    const char* p;
    char buf[9];
    int s, e;

    if (t < 0 || t >= GD_TERR_N) return L"?";
    if (done[t]) return cache[t];
    if (!b) return L"?";
    p = (const char*)(b + GD_TERR_NAME_RVA + (unsigned)t * 8);
    if (!SafeReadBytes(p, buf, 8)) return L"?";
    buf[8] = 0;
    for (s = 0; buf[s] == ' '; s++) { }
    for (e = (int)strlen(buf); e > s && buf[e - 1] == ' '; e--) { }
    buf[e] = 0;
    if (!MultiByteToWideChar(949, 0, buf + s, -1, cache[t], 16)) return L"?";
    done[t] = 1;
    return cache[t];
}

// 고위도 벌점 0~3 (0x475587~0x4755DC). 못 읽으면 -1.
static int LatPenalty(void)
{
    unsigned char* b = Base();
    int lat, dist, n = 0;
    if (!b || !SafeReadInt(b + GD_LAT_RVA, &lat)) return -1;
    if (lat < 0 || lat > 20000) return -1;
    dist = lat >= GD_LAT_EQUATOR ? lat - GD_LAT_EQUATOR : GD_LAT_EQUATOR - lat;
    if (dist >= GD_LAT_STEP1) n++;
    if (dist >= GD_LAT_STEP2) n++;
    if (dist >= GD_LAT_STEP3) n++;
    return n;
}

// 주인공(제독)의 기능값. 사람 레코드는 +0x40 부터 기능이 4바이트씩 놓인다 —
// 0x47CCA0 이 `[ebx + 기능번호*4 + 0x40]` 으로 견주는 그 자리이고, 그 함수의 this
// (0x5B60A0)가 곧 견줌의 첫 후보라 제독 레코드도 같은 꼴이다.
//
// ★ 게임 함수 0x47CCA0 을 부르면 **함대에 탄 사람 전부 중 제일 높은 값**을 얻을 수 있고
//   실제로 하루 셈에 쓰이는 것도 그 값이다. 처음에는 그렇게 했는데 규율 창을 여는 순간
//   게임이 꺼졌다 — 그 함수는 사람 목록을 훑으며 0x47CC60 으로 레코드를 얻어 오므로
//   게임이 그 목록을 손보고 있는 사이에 부르면 발밑이 무너진다. 창 하나 보자고 게임을
//   떨어뜨릴 수는 없으니 **게임 코드는 부르지 않고 제독 레코드만 읽는다.**
//   그래서 부하가 나보다 그 기능을 잘하면 화면 값이 실제보다 낮게 나온다 — 창에도
//   "주인공 기준" 이라고 적어 둔다.
static int SkillOf(int skill)
{
    unsigned char* b = Base();
    const int* p;
    int v;

    if (!b || skill < 0 || skill > 12) return -1;
    p = (const int*)(b + GD_ADMIRAL_RVA + 0x40 + (unsigned)skill * 4);
    if (!SafeReadInt(p, &v)) return -1;
    if (v < 0 || v > 20) return -1;
    return v;
}

// ------------------------------------------------------------------ 오버레이 켬/끔

// 데이터는 CDS95Util 한 자리에 모은다 — 플러그인이 plugins\<만든이>\ 에 있어도 설정은
// 루트에서 찾는다(DiscoveryEditKR 의 UpToDataDir 과 같은 규칙).
static void UpToDataDir(wchar_t* dir)
{
    wchar_t tmp[MAX_PATH];
    int n, i, cut2 = -1, cut1 = -1;
    lstrcpynW(tmp, dir, MAX_PATH);
    n = lstrlenW(tmp);
    if (n && tmp[n - 1] == L'\\') tmp[--n] = 0;
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

static void CfgPath(wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(g_hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"discipline.json");
}

// 적혀 있으면 그 값, 파일이 없으면 0(꺼짐).
static int CfgRead(void)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    char buf[256];
    DWORD got = 0;
    int on = 0;

    CfgPath(path, MAX_PATH);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) && got) {
        buf[got] = 0;
        // 값 하나뿐이라 온전한 파서를 두지 않는다 — "Overlay" 뒤에 true 가 있나만 본다.
        { const char* p = strstr(buf, "\"Overlay\"");
          if (p && strstr(p, "true")) on = 1; }
    }
    CloseHandle(h);
    return on;
}

static void CfgWrite(int on)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    const char* body;
    DWORD wr = 0;

    CfgPath(path, MAX_PATH);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    body = on ? "{\r\n  \"Overlay\": true\r\n}\r\n"
              : "{\r\n  \"Overlay\": false\r\n}\r\n";
    WriteFile(h, body, (DWORD)lstrlenA(body), &wr, NULL);
    CloseHandle(h);
}

// 게임 창을 찾은 쪽(fatigue.c 의 메뉴 스레드)이 한 번 불러 준다 — 지난 판에 켜 뒀으면
// 이때 다시 켠다. 창을 열지 않아도 켜져 있어야 하므로 여기서 한다.
void Discipline_RestoreOverlay(HINSTANCE hinst, HWND gameHwnd)
{
    if (!gameHwnd) return;
    g_hinst = hinst;
    g_gameHwnd = gameHwnd;
    if (g_ovlRestored) return;
    g_ovlRestored = 1;
    if (CfgRead()) {
        OvlDisc_Set(gameHwnd, 1);
        LogW(L"[FatigueUtilKR] 규율 오버레이를 지난 판 설정대로 켰습니다.");
    }
}

// ------------------------------------------------------------------ 변한 자리 기록

static void LogPush(int val, int delta)
{
    DiscLog e;
    int y, m, d, t;
    ZeroMemory(&e, sizeof(e));
    if (!CalGet(&y, &m, &d, &t)) { y = -1; m = -1; d = -1; t = -1; }
    e.year = y; e.month = m; e.day = d; e.tick = t;
    e.val = val; e.delta = delta;
    e.terr = TerrainNow();
    e.land = OnLand();

    EnterCriticalSection(&g_cs);
    g_log[g_logHead] = e;
    g_logHead = (g_logHead + 1) % LOG_N;
    if (g_logCount < LOG_N) g_logCount++;
    g_logSeq++;
    LeaveCriticalSection(&g_cs);
}

static void LogClear(void)
{
    EnterCriticalSection(&g_cs);
    g_logHead = 0; g_logCount = 0; g_logSeq++;
    LeaveCriticalSection(&g_cs);
}

// 새것이 0. 그런 줄이 없으면 0 을 돌려준다.
static int LogGet(int idx, DiscLog* out)
{
    int ok = 0;
    EnterCriticalSection(&g_cs);
    if (idx >= 0 && idx < g_logCount) {
        int pos = (g_logHead - 1 - idx + LOG_N * 2) % LOG_N;
        *out = g_log[pos];
        ok = 1;
    }
    LeaveCriticalSection(&g_cs);
    return ok;
}

// 창을 안 열어 둬도 쌓이게 따로 돈다. 게임 코드는 안 부르고 값만 읽는다.
static DWORD WINAPI WatchThread(LPVOID p)
{
    (void)p;
    for (;;) {
        int v = DisciplineGet();
        if (v < 0) {
            g_watchVal = -2;                 // 세이브를 안 불러온 자리로 돌아갔다
        } else if (v != g_watchVal) {
            int delta = (g_watchVal < 0) ? 0 : v - g_watchVal;
            g_watchVal = v;
            LogPush(v, delta);
        }
        Sleep(500);
    }
}

// ------------------------------------------------------------------ 창

// 부호를 앞에 붙인 수. ★ wsprintfW 에 "%+d" 를 쓰면 안 된다 — 이 함수는 CRT 의
// printf 가 아니라 user32 의 것이라 '+' 플래그를 모른다. 모르는 플래그를 만나면 인자를
// 하나씩 밀려 읽어, 뒤따르는 %s 가 숫자를 문자열 주소로 알고 따라가다 게임이 통째로
// 꺼진다(도시에서 규율 창을 열면 다운되던 것이 이것이었다).
static void SignedStr(int v, wchar_t* out)
{
    if (v > 0) wsprintfW(out, L"+%d", v);
    else       wsprintfW(out, L"%d", v);      // 음수의 '-' 는 %d 가 알아서 붙인다
}

static void SetRow(HWND h, int id, const wchar_t* label, const wchar_t* body)
{
    wchar_t t[400];
    wsprintfW(t, L"%s   %s", label, body);
    SetDlgItemTextW(h, id, t);
}

static void RefreshInfo(HWND h)
{
    wchar_t t[300], num[64];
    int y, m, d, tick;
    int terr, ticks, land, sail, handle, pen, money;
    int cur = DisciplineGet();

    // 규율
    if (cur < 0) lstrcpyW(num, L"아직 못 읽음");
    else         wsprintfW(num, L"%d / %d", cur, DISC_MAX);
    SetDlgItemTextW(h, ID_VALUE, num);
    if (cur != g_lastShown) {
        RECT bar;
        bar.left = BAR_X; bar.top = BAR_Y; bar.right = BAR_X + BAR_W; bar.bottom = BAR_Y + BAR_H;
        InvalidateRect(h, &bar, TRUE);
        g_lastShown = cur;
    }

    // 날짜
    if (CalGet(&y, &m, &d, &tick)) {
        wsprintfW(t, L"%d년 %d월 %d일 · 눈금 %d/%d", y, m, d, tick, GD_TICKS_PER_DAY);
        SetRow(h, ID_DATE, L"날짜", t);
    } else {
        SetRow(h, ID_DATE, L"날짜", L"아직 못 읽음 — 세이브를 불러오면 나옵니다");
    }

    // 자리 — 뭍/바다 · 칸 종류 · 그 칸에 드는 눈금
    land = OnLand();
    terr = TerrainNow();
    ticks = TerrainTicks(terr);
    if (!OnMapScreen()) {
        SetRow(h, ID_PLACE, L"자리", L"도시 안 — 지형은 항해 · 육상 화면에서 나옵니다");
    } else if (terr < 0) {
        SetRow(h, ID_PLACE, L"자리", L"아직 못 읽음");
    } else if (ticks > 0) {
        wsprintfW(t, L"%s · %s · 한 칸 %d눈금 (%d칸이면 하루)",
                  land > 0 ? L"뭍(말)" : L"바다", TerrainName(terr), ticks,
                  (GD_TICKS_PER_DAY + ticks - 1) / ticks);
        SetRow(h, ID_PLACE, L"자리", t);
    } else {
        wsprintfW(t, L"%s · %s", land > 0 ? L"뭍(말)" : L"바다", TerrainName(terr));
        SetRow(h, ID_PLACE, L"자리", t);
    }

    // 기능 — 함대에 탄 사람 전부에서 제일 높은 값이 쓰인다
    sail = SkillOf(GD_SKILL_SAIL);
    handle = SkillOf(GD_SKILL_HANDLE);
    if (sail < 0 && handle < 0) {
        SetRow(h, ID_SKILL, L"기능", L"아직 못 읽음");
    } else {
        wsprintfW(t, L"항해술 %d · 운용술 %d   (주인공 기준 — %s)",
                  sail < 0 ? 0 : sail, handle < 0 ? 0 : handle,
                  land > 0 ? L"뭍이라 운용술이 먹습니다" : L"바다라 항해술이 먹습니다");
        SetRow(h, ID_SKILL, L"기능", t);
    }

    // 하루 셈
    pen = LatPenalty();
    money = MoneyMirror();
    if (!OnMapScreen()) {
        // 마을에서 하루를 나면 되레 오른다 — 던 피로의 세 배다(0x4A2B0C).
        SetRow(h, ID_RATE, L"하루", L"마을에서 하루 나면 +3 (덜어낸 피로의 세 배)");
    } else if (land < 0 || terr < 0 || (sail < 0 && handle < 0)) {
        SetRow(h, ID_RATE, L"하루", L"아직 못 셉니다");
    } else if (land > 0) {
        int hv = handle < 0 ? 0 : handle;
        int base = (money > 0) ? 4 : 10;
        wchar_t sign[16];
        SignedStr(hv - base, sign);
        wsprintfW(t, L"%s = 운용술 %d − %d(%s)", sign, hv, base,
                  base == 4 ? L"여행비를 내고도 돈이 남을 때" : L"소지금 0");
        SetRow(h, ID_RATE, L"하루", t);
    } else {
        int sv = sail < 0 ? 0 : sail;
        int base = (terr <= 1) ? 6 : 3;
        int pv = pen < 0 ? 0 : pen;
        wchar_t sign[16];
        SignedStr(sv - base - pv, sign);
        wsprintfW(t, L"%s = 항해술 %d − %d(%s) − %d(고위도)", sign, sv, base,
                  base == 6 ? L"근해·원양" : L"뭍에 붙은 칸", pv);
        SetRow(h, ID_RATE, L"하루", t);
    }
}

static void RefreshList(HWND h)
{
    HWND lb = GetDlgItem(h, ID_LIST);
    int i, n, seq;
    if (!lb) return;

    EnterCriticalSection(&g_cs);
    seq = g_logSeq; n = g_logCount;
    LeaveCriticalSection(&g_cs);
    if (seq == g_shownSeq) return;
    g_shownSeq = seq;

    SendMessageW(lb, WM_SETREDRAW, FALSE, 0);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) {
        DiscLog e;
        wchar_t line[200], when[48], amount[16];
        if (!LogGet(i, &e)) break;
        if (e.year > 0) wsprintfW(when, L"%d.%02d.%02d %2d눈금", e.year, e.month, e.day, e.tick);
        else            lstrcpyW(when, L"(날짜 모름)   ");
        if (e.delta) SignedStr(e.delta, amount);
        else         lstrcpyW(amount, L"처음");
        wsprintfW(line, L"%s   규율 %3d  %-4s  %s%s",
                  when, e.val, amount,
                  e.land > 0 ? L"뭍·" : (e.land == 0 ? L"바다·" : L""),
                  e.terr >= 0 ? TerrainName(e.terr) : L"");
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageW(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, NULL, TRUE);
}

// 막대. 창이 직접 그린다 — 컨트롤을 쓸 만큼 복잡하지 않다.
static void PaintBar(HDC dc)
{
    RECT r;
    HBRUSH back, fill;
    int cur = DisciplineGet();
    int w;

    r.left = BAR_X; r.top = BAR_Y; r.right = BAR_X + BAR_W; r.bottom = BAR_Y + BAR_H;
    back = CreateSolidBrush(RGB(0xEC, 0xEC, 0xEC));
    FillRect(dc, &r, back);
    DeleteObject(back);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (cur < 0) return;
    if (cur > DISC_MAX) cur = DISC_MAX;
    w = (BAR_W - 2) * cur / DISC_MAX;
    if (w <= 0) return;

    r.left = BAR_X + 1; r.top = BAR_Y + 1;
    r.right = r.left + w; r.bottom = BAR_Y + BAR_H - 1;
    fill = CreateSolidBrush(RGB(0x4A, 0x6E, 0xA8));
    FillRect(dc, &r, fill);
    DeleteObject(fill);
}

static HWND MakeCtl(HWND h, const wchar_t* text, DWORD style,
                    int x, int y, int cw, int ch, int id, HFONT f)
{
    HWND c = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
                             x, y, cw, ch, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
    if (c && f) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static HWND MakeBtn(HWND h, const wchar_t* text, int x, int y, int cw, int ch, int id)
{
    HWND c = CreateWindowExW(0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                x, y, cw, ch, h, (HMENU)(UINT_PTR)id, g_hinst, NULL);
    if (c && g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK DiscProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        HWND lb, chk;
        // 창마다 제 글꼴을 만들어 쓰고 창과 함께 지운다(공유하면 먼저 닫힌 창이 지워 버린다).
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        g_big  = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"맑은 고딕");
        // 기록은 자리가 맞아야 읽히므로 고정폭으로 뽑는다.
        g_mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, DEFAULT_QUALITY, FIXED_PITCH, L"굴림체");

        MakeCtl(h, L"지금 규율", 0,          16, 18, 120, 22, ID_TITLE, g_font);
        MakeCtl(h, L"", SS_RIGHT,            16, 14, CLIENT_W - 32, 44, ID_VALUE, g_big);
        MakeCtl(h, L"", 0,                   16,  96, CLIENT_W - 32, 20, ID_DATE,  g_font);
        MakeCtl(h, L"", 0,                   16, 118, CLIENT_W - 32, 20, ID_PLACE, g_font);
        MakeCtl(h, L"", 0,                   16, 140, CLIENT_W - 32, 20, ID_SKILL, g_font);
        MakeCtl(h, L"", 0,                   16, 162, CLIENT_W - 32, 20, ID_RATE,  g_font);
        MakeCtl(h, L"규율이 바뀐 자리 (새것이 위. 창을 닫아 둬도 쌓입니다)", 0,
                                             16, 192, CLIENT_W - 32, 20, ID_LOGLBL, g_font);

        lb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOSEL | LBS_NOINTEGRALHEIGHT,
                    16, 214, CLIENT_W - 32, 170, h, (HMENU)(UINT_PTR)ID_LIST, g_hinst, NULL);
        if (lb && g_mono) SendMessageW(lb, WM_SETFONT, (WPARAM)g_mono, TRUE);

        chk = CreateWindowExW(0, L"BUTTON", L"화면 왼쪽 위에 규율 보이기",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    16, 392, 240, 22, h, (HMENU)(UINT_PTR)ID_OVL, g_hinst, NULL);
        if (chk) {
            if (g_font) SendMessageW(chk, WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(chk, BM_SETCHECK,
                         OvlDisc_Get(g_gameHwnd) ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        MakeCtl(h, L"산 · 사막에서 빨리 주는 것은 한 칸에 드는 눈금이 커서 하루가 빨리 오기 때문입니다.\n"
                   L"오버레이는 항해 · 육상 화면에 뜹니다(그리는 것은 WindArrowKR 입니다).", 0,
                                             16, 420, CLIENT_W - 32, 38, ID_NOTE, g_font);
        MakeBtn(h, L"기록 지우기", 16, CLIENT_H - 42, 108, 28, ID_CLEAR);
        MakeBtn(h, L"닫기", CLIENT_W - 92, CLIENT_H - 42, 76, 28, ID_CLOSE);

        g_lastShown = -2;
        g_shownSeq = -1;
        LogW(L"[FatigueUtilKR] 규율 창 — 위젯 놓기 끝.");
        RefreshInfo(h);
        LogW(L"[FatigueUtilKR] 규율 창 — 첫 표시 끝.");
        RefreshList(h);
        SetTimer(h, 1, 500, NULL);      // 게임이 값을 바꿔도 표시가 따라가게
        LogW(L"[FatigueUtilKR] 규율 창 준비 끝.");
        return 0;
    }
    case WM_TIMER:
        RefreshInfo(h);
        RefreshList(h);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintBar(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_COMMAND:
        if (LOWORD(w) == ID_CLOSE) ShowWindow(h, SW_HIDE);
        if (LOWORD(w) == ID_CLEAR) { LogClear(); RefreshList(h); }
        if (LOWORD(w) == ID_OVL) {
            // 체크는 게임 창 프로퍼티 한 칸이다 — 그리는 쪽(WindArrowKR)이 그 칸만 본다.
            int on = SendMessageW(GetDlgItem(h, ID_OVL), BM_GETCHECK, 0, 0) == BST_CHECKED;
            OvlDisc_Set(g_gameHwnd, on);
            CfgWrite(on);                       // 다음 판에도 그대로 켜지게
            LogW(on ? L"[FatigueUtilKR] 규율 오버레이 켬." : L"[FatigueUtilKR] 규율 오버레이 끔.");
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        if (g_big)  { DeleteObject(g_big);  g_big  = NULL; }
        if (g_mono) { DeleteObject(g_mono); g_mono = NULL; }
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

void Discipline_Init(HINSTANCE hinst)
{
    HANDLE t;
    g_hinst = hinst;
    if (!g_csReady) { InitializeCriticalSection(&g_cs); g_csReady = 1; }
    t = CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);   // 규율이 바뀌는 자리를 적어 둔다
    if (t) CloseHandle(t);
    LogW(L"[FatigueUtilKR] 규율 감시 시작.");
}

void Discipline_Show(HINSTANCE hinst, HWND gameHwnd)
{
    static BOOL reg = FALSE;
    RECT r, orc;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, ww, wh;

    g_hinst = hinst;
    LogW(L"[FatigueUtilKR] 규율 창 열기.");
    if (gameHwnd) g_gameHwnd = gameHwnd;
    if (!g_csReady) { InitializeCriticalSection(&g_cs); g_csReady = 1; }
    if (!g_wnd) {
        if (!reg) {
            WNDCLASSW wc;
            ZeroMemory(&wc, sizeof(wc));
            wc.lpfnWndProc = DiscProc;
            wc.hInstance = g_hinst;
            wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            wc.lpszClassName = WC_DISC;
            RegisterClassW(&wc);
            reg = TRUE;
        }
        r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
        AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
        ww = r.right - r.left; wh = r.bottom - r.top;
        // 게임 창 한가운데. 게임이 전체화면이라 소유자로 걸어야 위에 뜬다.
        if (gameHwnd && GetWindowRect(gameHwnd, &orc)) {
            x = orc.left + ((orc.right - orc.left) - ww) / 2;
            y = orc.top  + ((orc.bottom - orc.top) - wh) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
        }
        g_wnd = CreateWindowExW(0, WC_DISC, L"규율 — 날짜가 갈 때 얼마나 깎이나",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                    x, y, ww, wh, gameHwnd, NULL, g_hinst, NULL);
        LogW(L"[FatigueUtilKR] 규율 창 열림.");
    } else {
        g_lastShown = -2;
        g_shownSeq = -1;
        SendMessageW(GetDlgItem(g_wnd, ID_OVL), BM_SETCHECK,
                     OvlDisc_Get(g_gameHwnd) ? BST_CHECKED : BST_UNCHECKED, 0);
        RefreshInfo(g_wnd);
        RefreshList(g_wnd);
        InvalidateRect(g_wnd, NULL, TRUE);
    }
    if (g_wnd) {
        ShowWindow(g_wnd, SW_SHOW);
        SetForegroundWindow(g_wnd);
    }
}
