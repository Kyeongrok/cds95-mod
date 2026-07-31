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

// 그 자리의 이름표가 주어진 이름과 같은지(가운뎃점/공백/마침표 무시).
// 세이브의 얼굴코드는 남/여 표를 구분하지 않아서, 성별은 이름이 어느 표와 맞는지로 가린다.
int CharDb_NameMatches(int gender, int code, const wchar_t* name);
