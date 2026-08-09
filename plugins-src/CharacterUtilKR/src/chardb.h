#pragma once
#include <windows.h>

// char_names.h / char_info.h (얼굴코드 -> 인물명/카테고리/상세) 접근자.
// 표 자체는 chardb.c 한 곳에서만 include 한다. 도감 탭(character.c)과
// 세이브 파서(savedata.c)가 둘 다 쓰는데, 헤더를 양쪽에서 include 하면
// 수십 KB짜리 문자열 표가 TU 마다 중복 생성되기 때문이다.

#define CHARDB_MALE   0
#define CHARDB_FEMALE 1

int CharDb_Count(int gender);                          // 표 크기(512 / 256)
const wchar_t* CharDb_Name(int gender, int code);      // 없으면 L""
const wchar_t* CharDb_Info(int gender, int code);      // 없으면 L""
unsigned char  CharDb_Cat(int gender, int code);       // 0=기타 1=인물 2=여급 3=스폰서

// ---- 후원자(스폰서) ----
// patron_rows.h 의 표(81행). 얼굴이 아니라 "행"이 단위다 — 81명이 얼굴 72개를 나눠 쓰기
// 때문에(후계자가 선대 초상화를 물려받는다) 얼굴로 세면 9명이 빠진다.
// 표 자체는 chardb.c 한 곳에서만 include 한다(구조체를 내보내면 표가 TU 마다 복제된다).

#define CHARDB_PREF_N 8

int CharDb_PatronCount(void);                    // 81
int CharDb_PatronFace(int row);
int CharDb_PatronGender(int row);                // 0=남 1=여
int CharDb_PatronWealthAt(int row);              // 자금. 잘못된 행이면 -1
unsigned char CharDb_PatronPrefAt(int row);      // 취향 비트마스크
const wchar_t* CharDb_PatronName(int row);
const wchar_t* CharDb_PatronCity(int row);       // 후원자가 있는 도시
int CharDb_PatronAppear(int row);                // 등장연도(관련). EXE 표에서 구운 값
const wchar_t* CharDb_PrefName(int bit);         // 0 -> "지리" … 7 -> "보물"

// 스폰서 상세를 여러 줄 문자열로. out 은 192 wchar 이상. 잘못된 행이면 0.
int CharDb_FormatPatronRow(int row, wchar_t* out, int cap);

// 그 자리의 이름표가 주어진 이름과 같은지(가운뎃점/공백/마침표 무시).
// 세이브의 얼굴코드는 남/여 표를 구분하지 않아서, 성별은 이름이 어느 표와 맞는지로 가린다.
int CharDb_NameMatches(int gender, int code, const wchar_t* name);
