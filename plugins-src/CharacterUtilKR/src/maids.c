#include "maids.h"
#include "faces.h"
#include "savedata.h"   // Save_CityName / Save_SkillName / Save_SkillShort / SAVE_SKILL_LANG0

#define REC_SZ      40
#define OFF_NAME    0x00
#define OFF_FACE    0x04
#define OFF_AGE     0x08
#define OFF_BLOOD   0x10
#define OFF_BLDG    0x1C
#define OFF_LANG    0x20
#define OFF_CITY    0x24

#define BLDG_TAVERN 4       // 여급은 127행 전원 주점 소속이라 표 확인용으로 쓴다
#define LANG_BITS   MAID_LANG_N
#define CITY_MAX    226     // kCities 크기
#define BASE_YEAR   1495

#define MAID_NAME_CAP  ((int)(sizeof(((MaidInfo*)0)->name) / sizeof(wchar_t)))
#define LANG_BUF       192
#define INFO_MIN_CAP   256

static MaidInfo             g_maids[MAID_COUNT];
static int                  g_count = 0;
static const unsigned char* g_base = NULL;
static SIZE_T               g_imgSize = 0;
static unsigned char*       g_tbl = NULL;   // 로드된 이미지 안의 표 시작(쓰기용)

int Maid_Count(void) { return g_count; }

int Maid_Year(const MaidInfo* m) { return BASE_YEAR - m->ageAt1495; }

const MaidInfo* Maid_At(int row)
{
    static const MaidInfo empty;
    if (row < 0 || row >= g_count) return &empty;
    return &g_maids[row];
}

// 로드된 EXE 의 시작 주소와 이미지 크기. 이름 포인터가 게임 안을 가리키는지 볼 때 쓴다.
static int ModuleRange(void)
{
    const IMAGE_DOS_HEADER*  dos = (const IMAGE_DOS_HEADER*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS32* nt;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (const IMAGE_NT_HEADERS32*)((const unsigned char*)dos + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    g_base    = (const unsigned char*)dos;
    g_imgSize = nt->OptionalHeader.SizeOfImage;
    return g_imgSize > 0;
}

static int Readable(const void* p, SIZE_T n)
{
    const unsigned char* q   = (const unsigned char*)p;
    const unsigned char* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static int InModule(unsigned addr)
{
    return addr >= (unsigned)(UINT_PTR)g_base
        && addr <  (unsigned)(UINT_PTR)g_base + (unsigned)g_imgSize;
}

// 이 40바이트가 여급 레코드로 말이 되는지. 127행 전부 통과해야 표를 믿는다.
static int RowOk(const unsigned char* r, int faceMax)
{
    int      face  = *(const int*)(r + OFF_FACE);
    int      blood = *(const int*)(r + OFF_BLOOD);
    int      city  = *(const int*)(r + OFF_CITY);
    unsigned lang  = *(const unsigned*)(r + OFF_LANG);

    if (*(const int*)(r + OFF_BLDG) != BLDG_TAVERN) return 0;
    if (!InModule(*(const unsigned*)(r + OFF_NAME))) return 0;
    if (face < 0 || face >= faceMax) return 0;
    if (blood < 0 || blood > 3) return 0;
    if (city < 0 || city >= CITY_MAX) return 0;
    if (lang == 0 || lang >= (1u << LANG_BITS)) return 0;
    return 1;
}

static void ReadName(unsigned ptr, wchar_t* out, int cap)
{
    const char* s   = (const char*)(UINT_PTR)ptr;
    const char* lim = (const char*)(g_base + g_imgSize);
    char buf[32];
    int  n = 0, w;

    out[0] = 0;
    while (n < (int)sizeof(buf) - 1 && s + n < lim && s[n]) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (n == 0) return;

    // 949 = cp949. savedata.c 의 이름 읽기와 같은 전제(한글 통합수정판)다.
    w = MultiByteToWideChar(949, 0, buf, n, out, cap - 1);
    if (w <= 0) { out[0] = 0; return; }
    if (w > cap - 1) w = cap - 1;
    out[w] = 0;
}

int Maid_Load(void)
{
    const unsigned char* tbl;
    int i, faceMax;

    if (g_count > 0) return 1;

    faceMax = Face_Count(FACE_FEMALE);
    if (faceMax <= 0) return 0;              // FEMALE.CDS 를 아직 못 열었다. 다음에 다시.
    if (!ModuleRange()) return 0;
    if (MAID_RVA + (MAID_COUNT + 1) * REC_SZ > (unsigned)g_imgSize) return 0;

    tbl = g_base + MAID_RVA;
    if (!Readable(tbl, (MAID_COUNT + 1) * REC_SZ)) return 0;

    for (i = 0; i < MAID_COUNT; i++)
        if (!RowOk(tbl + i * REC_SZ, faceMax)) return 0;
    // 128행째까지 조건을 만족하면 표가 밀려 잡힌 것이다. 길이가 딱 127이어야 한다.
    if (RowOk(tbl + MAID_COUNT * REC_SZ, faceMax)) return 0;

    for (i = 0; i < MAID_COUNT; i++) {
        const unsigned char* r = tbl + i * REC_SZ;
        MaidInfo* m = &g_maids[i];
        ReadName(*(const unsigned*)(r + OFF_NAME), m->name, MAID_NAME_CAP);
        m->face      = *(const int*)(r + OFF_FACE);
        m->ageAt1495 = *(const int*)(r + OFF_AGE);
        m->blood     = *(const int*)(r + OFF_BLOOD);
        m->lang      = *(const unsigned*)(r + OFF_LANG);
        m->city      = *(const int*)(r + OFF_CITY);
    }
    g_tbl   = (unsigned char*)tbl;
    g_count = MAID_COUNT;
    OutputDebugStringW(L"[CharacterUtilKR] 여급 표 127행 로드.");
    return 1;
}

// 로드된 이미지의 한 필드를 고쳐 쓴다. 표가 .rdata 라 읽기 전용이므로 잠깐만 연다.
// 파일이 아니라 메모리를 고치는 것이라 게임을 끄면 원래대로 돌아간다.
static int WriteField(int row, int fieldOff, unsigned value)
{
    unsigned char* p;
    DWORD old = 0;

    if (row < 0 || row >= g_count || !g_tbl) return 0;
    p = g_tbl + row * REC_SZ + fieldOff;
    if (!VirtualProtect(p, sizeof(unsigned), PAGE_READWRITE, &old)) return 0;
    *(unsigned*)p = value;
    VirtualProtect(p, sizeof(unsigned), old, &old);
    return 1;
}

int Maid_SetYear(int row, int year)
{
    int value = BASE_YEAR - year;
    if (year < MAID_YEAR_MIN || year > MAID_YEAR_MAX) return 0;
    if (!WriteField(row, OFF_AGE, (unsigned)value)) return 0;
    g_maids[row].ageAt1495 = value;
    return 1;
}

// 언어 비트 하나를 뒤집는다. 전부 꺼서 0 이 되는 것도 그대로 허용한다
// (원본에는 0 인 여급이 없지만, 되돌리려면 게임을 껐다 켜면 그만이다).
int Maid_ToggleLang(int row, int bit)
{
    unsigned lang;
    if (row < 0 || row >= g_count) return 0;
    if (bit < 0 || bit >= LANG_BITS) return 0;
    lang = g_maids[row].lang ^ (1u << bit);
    if (!WriteField(row, OFF_LANG, lang)) return 0;
    g_maids[row].lang = lang;
    return 1;
}

int Maid_SetCity(int row, int city)
{
    if (city < 0 || city >= Save_CityCount()) return 0;
    if (!WriteField(row, OFF_CITY, (unsigned)city)) return 0;
    g_maids[row].city = city;
    return 1;
}

const wchar_t* Maid_LangName(int bit)
{
    if (bit < 0 || bit >= LANG_BITS) return L"";
    return Save_SkillName(SAVE_SKILL_LANG0 + bit);
}

const wchar_t* Maid_CityName(int city)
{
    if (city < 0 || city >= Save_CityCount()) return L"?";
    return Save_CityName((unsigned char)city);
}

int Maid_CityCount(void) { return Save_CityCount(); }

static void Append(wchar_t* dst, int cap, const wchar_t* s)
{
    int n = lstrlenW(dst), i = 0;
    while (s[i] && n < cap - 1) dst[n++] = s[i++];
    dst[n] = 0;
}

// 언어 비트마스크를 사람이 읽는 목록으로.
// 실제 분포는 1~3개가 123명, 4개 2명, 5개 1명, 그리고 14개를 다 아는 여급이 딱 하나
// (세빌리아의 마르가리타) 있다. 5개까지는 전체 이름을 쓰고, 그 하나만 한 글자 표기로 줄인다.
// 구분자에 공백을 넣는 건 멋이 아니다 — 정보 패널은 DT_WORDBREAK 로 그리는데 쉼표만으로는
// 줄바꿈 자리가 없어서 폭(248px)을 넘긴 목록이 잘려나간다.
#define LANG_FULL_MAX 5

static void FormatLangs(unsigned lang, wchar_t* out, int cap)
{
    int b, cnt = 0;
    for (b = 0; b < LANG_BITS; b++) if (lang >> b & 1) cnt++;

    out[0] = 0;
    if (cnt == 0) { Append(out, cap, L"없음"); return; }

    if (cnt > LANG_FULL_MAX) {
        wchar_t head[16];
        wsprintfW(head, L"%d종", cnt);   // 뒤이어 붙는 한 글자 표기마다 앞에 공백이 들어간다
        Append(out, cap, head);
    }
    for (b = 0; b < LANG_BITS; b++) {
        if (!(lang >> b & 1)) continue;
        if (cnt > LANG_FULL_MAX) {
            if (out[0]) Append(out, cap, L" ");
            Append(out, cap, Save_SkillShort(SAVE_SKILL_LANG0 + b));
        } else {
            if (out[0]) Append(out, cap, L", ");
            Append(out, cap, Save_SkillName(SAVE_SKILL_LANG0 + b));
        }
    }
}

const wchar_t* Maid_BloodName(int blood)
{
    static const wchar_t* kBlood[4] = { L"A", L"B", L"O", L"AB" };
    return kBlood[blood & 3];
}

void Maid_FormatInfo(const MaidInfo* m, wchar_t* out, int cap)
{
    wchar_t langs[LANG_BUF];

    if (cap < INFO_MIN_CAP) { if (cap > 0) out[0] = 0; return; }
    FormatLangs(m->lang, langs, LANG_BUF);
#if CHARKR_EDIT_CITY
    wsprintfW(out, L"언어 %s", langs);
#else
    wsprintfW(out, L"도시 %s\n언어 %s", Save_CityName((unsigned char)m->city), langs);
#endif
}
