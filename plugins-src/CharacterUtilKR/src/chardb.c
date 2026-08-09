#include "chardb.h"
#include "char_names.h"   // kMaleNames[], kFemaleNames[], kMaleCat[], kFemaleCat[]
#include "char_info.h"    // kMaleInfo[], kFemaleInfo[]
#include "patron_rows.h"  // kPatronRows[81]

// 취향 이름. 비트 순서는 patron_rows.h 의 pref 와 같다.
// (EXE 후원자 표 +0x34 도 같은 여덟 가지인데 비트 순서가 다르다 —
//  거긴 지리·역사·보물·종교·교역품·미신·생물·민족 순으로, 81행 전부 대조해 확인했다.)
static const wchar_t* kPatronPref[CHARDB_PREF_N] = {
    L"지리", L"역사", L"종교", L"민족", L"생물", L"미신", L"교역품", L"보물"
};

#define MALE_N   ((int)(sizeof(kMaleNames)   / sizeof(kMaleNames[0])))
#define FEMALE_N ((int)(sizeof(kFemaleNames) / sizeof(kFemaleNames[0])))

int CharDb_Count(int gender) { return gender == CHARDB_FEMALE ? FEMALE_N : MALE_N; }

const wchar_t* CharDb_Name(int gender, int code)
{
    if (code < 0) return L"";
    if (gender == CHARDB_FEMALE) return code < FEMALE_N ? kFemaleNames[code] : L"";
    return code < MALE_N ? kMaleNames[code] : L"";
}

const wchar_t* CharDb_Info(int gender, int code)
{
    if (code < 0) return L"";
    if (gender == CHARDB_FEMALE)
        return code < (int)(sizeof(kFemaleInfo)/sizeof(kFemaleInfo[0])) ? kFemaleInfo[code] : L"";
    return code < (int)(sizeof(kMaleInfo)/sizeof(kMaleInfo[0])) ? kMaleInfo[code] : L"";
}

unsigned char CharDb_Cat(int gender, int code)
{
    if (code < 0) return 0;
    if (gender == CHARDB_FEMALE)
        return code < (int)sizeof(kFemaleCat) ? kFemaleCat[code] : 0;
    return code < (int)sizeof(kMaleCat) ? kMaleCat[code] : 0;
}

// ---- 후원자(스폰서) ----

static const PatronRow* Patron(int row)
{
    return (row >= 0 && row < PATRON_ROW_N) ? &kPatronRows[row] : NULL;
}

int CharDb_PatronCount(void)  { return PATRON_ROW_N; }
int CharDb_PatronFace(int row)   { const PatronRow* p = Patron(row); return p ? p->face : -1; }
int CharDb_PatronGender(int row) { const PatronRow* p = Patron(row); return p ? p->gender : 0; }
unsigned char CharDb_PatronPrefAt(int row) { const PatronRow* p = Patron(row); return p ? p->pref : 0; }
const wchar_t* CharDb_PatronName(int row)  { const PatronRow* p = Patron(row); return p ? p->name : L""; }
const wchar_t* CharDb_PatronCity(int row)  { const PatronRow* p = Patron(row); return p ? p->city : L""; }
int CharDb_PatronAppear(int row) { const PatronRow* p = Patron(row); return p ? p->appear : 0; }

int CharDb_PatronWealthAt(int row)
{
    const PatronRow* p = Patron(row);
    return p ? p->wealth : -1;
}

const wchar_t* CharDb_PrefName(int bit)
{
    return (bit >= 0 && bit < CHARDB_PREF_N) ? kPatronPref[bit] : L"";
}

int CharDb_FormatPatronRow(int row, wchar_t* out, int cap)
{
    const PatronRow* p = Patron(row);
    wchar_t prefs[96], years[48];
    int b, n = 0;

    if (!p || cap < 192) return 0;

    prefs[0] = 0;
    for (b = 0; b < CHARDB_PREF_N; b++) {
        if (!(p->pref >> b & 1)) continue;
        if (n++) lstrcatW(prefs, L",");
        lstrcatW(prefs, kPatronPref[b]);
    }
    if (!prefs[0]) lstrcpyW(prefs, L"없음");

    // 은퇴연도는 16명만 있다. 없으면 끝까지 남는 후원자라 아예 적지 않는다.
    // 등장연도는 셀 아래쪽 select box 가 맡는다(고칠 수 있어야 해서) — 여기선 은퇴만 적는다.
    if (p->retire) wsprintfW(years, L"\n은퇴 %d", p->retire);
    else years[0] = 0;

    wsprintfW(out, L"[스폰서] %s·%s\n%s\n자금 %d\n취향 %s%s",
              p->city, p->job, p->nation, p->wealth, prefs, years);
    return 1;
}

// 표기 흔들림(가운뎃점/공백/마침표)을 걷어낸 비교용 키를 만든다.
// 세이브는 "성"과 "이름"을 따로 들고 있어 우리가 가운뎃점으로 이어 붙이는데,
// char_names.h 쪽은 공백으로 이어 붙어 있어서 그냥 비교하면 전부 어긋난다.
static void NormName(const wchar_t* s, wchar_t* out, int cap)
{
    int n = 0;
    if (cap <= 0) return;
    while (*s && n < cap - 1) {
        if (*s != L'·' && *s != L'・' && *s != L' ' && *s != L'.')
            out[n++] = *s;
        s++;
    }
    out[n] = 0;
}

int CharDb_NameMatches(int gender, int code, const wchar_t* name)
{
    const wchar_t* entry = CharDb_Name(gender, code);
    wchar_t a[96], b[96];
    if (!entry[0] || !name || !name[0]) return 0;
    NormName(entry, a, 96);
    NormName(name, b, 96);
    return lstrcmpW(a, b) == 0;
}
