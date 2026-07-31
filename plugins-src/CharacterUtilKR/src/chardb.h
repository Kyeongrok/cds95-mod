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

// 이름으로 얼굴코드를 역추적한다(가운뎃점/공백/마침표 무시).
// 세이브 파일의 인물 이름에는 얼굴코드가 없어서 이름으로 되찾는 수밖에 없다.
// 찾으면 1 을 돌려주고 gender/code 를 채운다. 못 찾으면 0.
int CharDb_FindByName(const wchar_t* name, int* gender, int* code);
