#include <windows.h>
#include "discovl.h"

// 규율 숫자를 8bpp 색인 그림으로 굽는다. (discovl.h 의 설명 참고)
//
// 색인은 arrow.c 와 같은 것을 쓴다 — 해상 팔레트의 10 = 흰색, 50 = 짙은 남색.
// 색인만 넘기므로 게임이 그때그때 올린 팔레트로 나온다.

#define IDX_TEXT  10
#define IDX_EDGE  50

#define DISC_RVA  0x001B3954u    // 함대 정보 +0x2C — 규율(FatigueUtilKR/src/fleetmem.h 와 같은 자리)
#define LUCK_RVA  0x001B60D0u    // 주인공 레코드 +0x28 — 운(ce/CDS_95.CT "운")
#define FAITH_RVA 0x001B60D4u    // 주인공 레코드 +0x2C — 신앙심(ce/CDS_95.CT "신앙심")

// 성격 궁합 — CharacterUtilKR/src/maids.c Maid_PlayerPrefs 와 같은 셈(note/MaidMatch-0x477FE0.md).
#define PL_FACE_RVA   0x001B60A8u  // 주인공 +0x00 얼굴코드
#define PL_BORNY_RVA  0x001B6188u  // +0xE0 생년 (+0x04 나이는 주인공에게는 늘 0 이라 안 쓴다)
#define NOW_Y_RVA     0x001A4D20u  // 지금 연·월·일
#define NOW_M_RVA     0x001A4D24u
#define NOW_D_RVA     0x001A4D28u
#define PL_BLOOD_RVA  0x001B60B8u  // +0x10 혈액형 0=A 1=B 2=O 3=AB
#define PL_BORNM_RVA  0x001B618Cu  // +0xE4 생월
#define PL_BORND_RVA  0x001B6190u  // +0xE8 생일
#define ZODIAC_RVA    0x00168578u  // 12성좌 x 8 int
#define BLOOD_RVA     0x001686F8u  // 4혈액형 x 8 int
#define FACE_RVA      0x0011ACA0u  // 얼굴 32줄 x 32바이트, +0x18 성격 · +0x1C 보정
#define PERSONA_N     8
#define ELDER_AGE     36           // 이 나이부터 표시 얼굴이 +16

static unsigned char g_pix[DISCOVL_W * DISCOVL_H];
static int  g_ready = 0;
static int  g_shownVal = -12345;
static int  g_shownLuck = -12345;
static int  g_shownFaith = -12345;
static int  g_shownMatch = -12345;
static int  g_serial = 0;

// 4바이트 값 하나를 그대로. 못 읽으면 0.
static int PeekInt(unsigned rva, int* out)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* base = (unsigned char*)GetModuleHandleW(NULL);
    const int* p;
    if (!base) return 0;
    p = (const int*)(base + rva);
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    // 검사와 읽기 사이에 자리가 사라져도 게임이 꺼지지는 않게 한다.
    __try { *out = *p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return 1;
}

// 능력치 하나. 못 읽거나 말이 안 되면 -1(세이브를 아직 안 불러온 자리다).
static int ReadStat(unsigned rva)
{
    int v;
    if (!PeekInt(rva, &v)) return -1;
    if (v < 0 || v > 1000) return -1;
    return v;
}

// 생월·생일 -> 성좌(0=양자리 … 11=물고기자리). 게임 0x42E620 과 같은 셈.
static int ZodiacOf(int month, int day)
{
    static const short kFrom[12] = { 321, 421, 522, 622, 723, 823, 924,1024,1123,1222,1321,1419 };
    static const short kTo  [12] = { 420, 521, 621, 722, 822, 923,1023,1122,1221,1320,1418,1520 };
    int v, i;
    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    v = month * 100 + day;
    if (v < 321) v += 1200;
    for (i = 0; i < 12; i++)
        if (kFrom[i] <= v && v <= kTo[i]) return i;
    return -1;
}

// 주인공의 성격 선호 8칸(0~2)을 2비트씩 묶어서. 칸 i = (값 >> 2i) & 3. 못 재면 -1.
// 델포이 계시(0x40A5A4)가 이 값으로 성격 낱말을 고르고, 여급 궁합(0x465C61)도 이 값을 쓴다.
static int PersonaPrefs(void)
{
    int year, month, day, ny, nm, nd, blood, face, age, z, i, packed = 0;
    int pref[PERSONA_N];

    if (!PeekInt(PL_BORNM_RVA, &month) || !PeekInt(PL_BORND_RVA, &day)) return -1;
    if (!PeekInt(PL_BLOOD_RVA, &blood) || !PeekInt(PL_FACE_RVA, &face)) return -1;
    if (!PeekInt(PL_BORNY_RVA, &year)) return -1;
    if (!PeekInt(NOW_Y_RVA, &ny) || !PeekInt(NOW_M_RVA, &nm) || !PeekInt(NOW_D_RVA, &nd)) return -1;
    // 만나이 — 게임 0x47CB20 과 같다(생일이 안 지났으면 한 살 뺀다).
    age = ny - year;
    if (month > nm || (month == nm && day > nd)) age--;
    z = ZodiacOf(month, day);
    if (z < 0 || blood < 0 || blood > 3 || face < 0) return -1;
    if (age >= ELDER_AGE) face += 16;     // 화면에 나오는 얼굴로 따진다(0x47CAF0)
    if (face >= 32) return -1;

    for (i = 0; i < PERSONA_N; i++) {
        int zv, bv;
        if (!PeekInt(ZODIAC_RVA + (unsigned)(z * PERSONA_N + i) * 4u, &zv)) return -1;
        if (!PeekInt(BLOOD_RVA + (unsigned)(blood * PERSONA_N + i) * 4u, &bv)) return -1;
        pref[i] = zv + bv;
    }
    {
        int k, adj;
        if (!PeekInt(FACE_RVA + (unsigned)face * 32u + 0x18u, &k)) return -1;
        if (!PeekInt(FACE_RVA + (unsigned)face * 32u + 0x1Cu, &adj)) return -1;
        if (k >= 0 && k < PERSONA_N) pref[k] += adj;
    }
    for (i = 0; i < PERSONA_N; i++) {
        int p = pref[i] < 0 ? 0 : (pref[i] > 2 ? 2 : pref[i]);   // 게임도 0~2 로 자른다(0x49E540)
        packed |= p << (2 * i);
    }
    return packed;
}

// GDI 로 글자를 찍어 색인 그림으로 옮긴다. 글자 둘레 한 점은 테두리 색인으로 채워
// 바다 위에서도 읽히게 한다(화살표와 같은 수법).
static int BuildBitmap(const wchar_t* text)
{
    static unsigned char body[DISCOVL_W * DISCOVL_H];
    BITMAPINFO bi;
    HDC dc;
    HBITMAP bmp, oldBmp;
    HFONT font, oldFont;
    DWORD* bits = NULL;
    RECT r;
    int x, y;

    dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = DISCOVL_W;
    bi.bmiHeader.biHeight = -DISCOVL_H;          // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, NULL, 0);
    if (!bmp || !bits) { DeleteDC(dc); return 0; }
    oldBmp = (HBITMAP)SelectObject(dc, bmp);

    r.left = 0; r.top = 0; r.right = DISCOVL_W; r.bottom = DISCOVL_H;
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    font = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"맑은 고딕");
    oldFont = font ? (HFONT)SelectObject(dc, font) : NULL;
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    // 줄바꿈(\n)마다 한 줄씩 내려 찍는다.
    {
        const wchar_t* line = text;
        int ly = 2;
        while (*line) {
            const wchar_t* e = line;
            while (*e && *e != L'\n') e++;
            TextOutW(dc, 1, ly, line, (int)(e - line));
            ly += DISCOVL_LINE_H;
            line = *e ? e + 1 : e;
        }
    }
    GdiFlush();

    // 밝으면 글자, 아니면 빈 칸. 안티에일리어싱을 껐으니 문턱 하나로 갈린다.
    for (y = 0; y < DISCOVL_H; y++)
        for (x = 0; x < DISCOVL_W; x++) {
            DWORD px = bits[y * DISCOVL_W + x];
            body[y * DISCOVL_W + x] = ((px & 0xFF) > 96) ? IDX_TEXT : 0;
        }

    // 글자에 닿은 빈 칸을 테두리로.
    for (y = 0; y < DISCOVL_H; y++)
        for (x = 0; x < DISCOVL_W; x++) {
            int dx, dy, touch = 0;
            if (body[y * DISCOVL_W + x]) { g_pix[y * DISCOVL_W + x] = IDX_TEXT; continue; }
            for (dy = -1; dy <= 1 && !touch; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= DISCOVL_W || ny >= DISCOVL_H) continue;
                    if (body[ny * DISCOVL_W + nx]) { touch = 1; break; }
                }
            g_pix[y * DISCOVL_W + x] = touch ? IDX_EDGE : 0;
        }

    if (oldFont) SelectObject(dc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return 1;
}

const unsigned char* DiscOvl_Bitmap(void)
{
    int v = ReadStat(DISC_RVA);
    int luck = ReadStat(LUCK_RVA);
    int faith = ReadStat(FAITH_RVA);
    int match = PersonaPrefs();
    wchar_t text[256];
    int n;

    if (v < 0) return NULL;
    if (g_ready && v == g_shownVal && luck == g_shownLuck && faith == g_shownFaith
        && match == g_shownMatch) return g_pix;

    // 운·신앙은 못 읽으면 그 자리만 뺀다 — 규율은 그래도 보인다.
    n = wsprintfW(text, L"규율 %d", v);
    if (luck >= 0)  n += wsprintfW(text + n, L"  운 %d", luck);
    if (faith >= 0) n += wsprintfW(text + n, L"  신앙 %d", faith);
    if (match >= 0) {
        // 델포이 계시와 같은 표(0x40A4C0). 성격마다 [선호0 낱말, -, 선호2 낱말], 선호 1 이면 안 나온다.
        static const wchar_t* kWord[PERSONA_N][2] = {
            { L"소심",   L"거만" },     { L"우유부단", L"독선" },   { L"변덕",   L"집착" },
            { L"겁장이", L"무모" },     { L"냉혹",   L"팔방미인" }, { L"편협",   L"욕심장이" },
            { L"무신경", L"신경질" },   { L"낭비가", L"깍쟁이" }
        };
        static const wchar_t* kName[PERSONA_N] = {
            L"당당", L"강인", L"의지", L"용감", L"친절", L"로맨틱", L"섬세", L"견실"
        };
        int i, first;

        // 둘째 줄 — 성격(계시에 나오는 낱말 그대로).
        first = 1;
        for (i = 0; i < PERSONA_N; i++) {
            int p = (match >> (2 * i)) & 3;
            if (p == 1) continue;
            n += wsprintfW(text + n, first ? L"\n성격 %s" : L"·%s", kWord[i][p == 2]);
            first = 0;
        }
        // 셋째 줄 — 궁합 좋은 여급 성격(선호 2).
        first = 1;
        for (i = 0; i < PERSONA_N; i++) {
            if (((match >> (2 * i)) & 3) != 2) continue;
            n += wsprintfW(text + n, first ? L"\n여급궁합 %s" : L"·%s", kName[i]);
            first = 0;
        }
    }
    if (!BuildBitmap(text)) return NULL;
    g_shownVal = v;
    g_shownLuck = luck;
    g_shownFaith = faith;
    g_shownMatch = match;
    g_ready = 1;
    g_serial++;
    return g_pix;
}

int DiscOvl_Serial(void) { return g_serial; }
