#pragma once
#include <windows.h>

// 게임 폴더의 SAVEDATA.CDS 를 읽어 인물 레코드를 뽑아낸다. 읽기 전용.
// (게임이 실행 중일 때 세이브를 덮어쓰면 다음 저장에 날아가거나 파일이 깨지므로
//  이 플러그인은 절대 쓰지 않는다. 편집이 필요하면 cds-helper 를 쓸 것.)
//
// 레코드 배치는 cds-helper 의 SaveDataService 와 동일하며, 실제 세이브로 검증했다:
//   표 시작 0x924A, 레코드 0x90 바이트, 최대 461개
//   +0x00 체력  +0x01 지력  +0x02 무력  +0x03 매력  +0x04 운
//   +0x0A 등장여부, +0x0A+k = 특기 k(1~27) 레벨
//   +0x26 명성(u16)  +0x2E 소재 도시  +0x30 건물(4=주점 5=여관)
//   +0x32 이름(20)   +0x45 성(20, cp949)
//   +0x5C 연령(부호 있음)  +0x60 성좌(0~11)  +0x62 고용상태(1~3)
// 플레이어: +0x53 명성(u16), +0x57 현재 도시

#define SAVE_SKILL_MAX   27    // 특기 ID 1..27
#define SAVE_SKILL_LANG0 14    // 14 이상은 언어 특기

typedef struct {
    int            index;                       // 세이브 내 인물 번호
    wchar_t        name[64];
    unsigned char  hp, intel, str, chm, luk;
    unsigned char  skill[SAVE_SKILL_MAX + 1];   // [1..27] = 레벨(0=없음)
    unsigned short fame;
    unsigned char  loc;                         // 도시 ID (255=함대소속)
    unsigned char  bldg;                        // 4=주점 5=여관
    signed char    age;
    unsigned char  hire;                        // 1=대화만 2=고용가능 3=고용중
    unsigned char  zodiac;                      // 성좌 0~11
    int            faceGender;                  // 0=남 1=여, -1=이름 역추적 실패
    int            faceCode;                    // -1=실패
} SaveChar;

typedef struct {
    SaveChar*      chars;
    int            count;
    unsigned short playerFame;
    unsigned char  playerCity;
    unsigned short year;
    unsigned char  month, day;
    int            loaded;
    wchar_t        path[MAX_PATH];
} SaveData;

// 게임 실행 파일과 같은 폴더의 SAVEDATA.CDS 를 읽는다. 성공 1 / 실패 0.
// 이미 로드돼 있으면 기존 내용을 버리고 다시 읽는다(새로고침 겸용).
int  Save_Load(SaveData* s);
void Save_Free(SaveData* s);

const wchar_t* Save_CityName(unsigned char id);      // 255 -> "함대소속"
const wchar_t* Save_BuildingName(unsigned char b);   // 4 -> "주점", 5 -> "여관"
const wchar_t* Save_SkillName(int id);               // 1 -> "항해술"
const wchar_t* Save_SkillShort(int id);              // 1 -> "항"
