#include "chardb.h"
#include "char_names.h"   // kMaleNames[], kFemaleNames[], kMaleCat[], kFemaleCat[]
#include "char_info.h"    // kMaleInfo[], kFemaleInfo[]

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

static int FindIn(const wchar_t* const* tbl, int n, const wchar_t* key)
{
    wchar_t tmp[96];
    int i;
    for (i = 0; i < n; i++) {
        if (!tbl[i] || !tbl[i][0]) continue;
        NormName(tbl[i], tmp, 96);
        if (lstrcmpW(tmp, key) == 0) return i;
    }
    return -1;
}

int CharDb_FindByName(const wchar_t* name, int* gender, int* code)
{
    wchar_t key[96];
    int i;
    if (!name || !name[0]) return 0;
    NormName(name, key, 96);
    if (!key[0]) return 0;

    i = FindIn(kMaleNames, MALE_N, key);
    if (i >= 0) { if (gender) *gender = CHARDB_MALE; if (code) *code = i; return 1; }

    i = FindIn(kFemaleNames, FEMALE_N, key);
    if (i >= 0) { if (gender) *gender = CHARDB_FEMALE; if (code) *code = i; return 1; }

    return 0;
}
