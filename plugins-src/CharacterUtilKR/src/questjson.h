#pragma once
#include <windows.h>
#include "questdb.h"    // QF_N

// CDS95Util\quests.json — 퀘스트 "덮어쓰기 + 추가" 목록.
//
// 원본 이벤트 파일은 <이름>.CDS.orig 로 손대지 않고 남겨 두고, 게임이 읽는 <이름>.CDS 는
// 플러그인이 뜰 때마다 [.orig + 이 목록] 으로 다시 만든다. 그래서 목록에서 항목을 지우면
// 그 퀘스트는 원래대로 돌아간다 — 되돌리기가 공짜다.
//
//   {
//     "EHT.CDS": [
//       { "index": 0, "reward": 20000, "days": 365 },
//       { "clone": 0, "city": 12, "building": 1, "item": 55, "reward": 60000 }
//     ]
//   }
//
//   index  기존 퀘스트 번호(0부터)를 덮어쓴다
//   clone  그 번호 퀘스트를 통째로 본떠 파일 끝에 새 퀘스트로 붙인 뒤 값을 덮어쓴다
//
// 새 퀘스트를 "끝에만" 붙이는 데는 이유가 있다. 세이브의 진행 포인터가 파트 번호라서,
// 중간에 끼워 넣으면 기존 세이브가 엉뚱한 퀘스트를 가리키게 된다(questdb.h 참고).
//
// 대사는 아직 못 고친다. 길이가 변하면 파트 머리의 오프셋 표와 분기 점프를 전부 다시
// 계산해야 해서 별건이다. clone 으로 만든 퀘스트는 본뜬 쪽의 대사를 그대로 쓴다.

// ---- script: 줄 단위 편집 ----
//
//   "script": {
//     "1:1:07": ["대사", "리스본에서 대포를 사다 주게."],
//     "2:0:10": [["만약","아이템",60,"@성공"], ["대사","아직인가. 서둘러 주게."]],
//     "2:0:13": []
//   }
//
// 주소는 "파트:슬롯:줄" 이고 인게임 [대사] 탭에 그대로 찍히는 번호다. 값은 그 줄을
// 대신할 줄들의 목록이라, 한 가지 형태로 바꾸기·끼워넣기·지우기가 다 된다([] = 지우기).
// 줄 하나짜리는 대괄호를 겹치지 않아도 된다.
// "파트:슬롯:$" 로 쓰면 그 슬롯 본문 맨 끝(끝 줄 앞)에 덧붙인다.
//
// **주소는 늘 원본(.orig) 기준이다.** 줄을 넣고 빼도 다른 주소가 밀리지 않는다.
//
// 점프는 오프셋 대신 "@이름" 라벨로 쓴다. ["라벨","@이름"] 줄이 그 자리를 잡고,
// 바이트 오프셋은 저장할 때 계산한다.
#define QJ_OP_NONE   0
#define QJ_OP_TEXT   1    // ["대사","글"]  또는 ["대사","글",플래그]
#define QJ_OP_WHERE  2    // ["도시",n] ["건물",n] ["지역",n] ["국가",n]  arg0=선택자 arg1=값
#define QJ_OP_YEAR   3    // ["연도",1500]
#define QJ_OP_CMP    4    // ["명성조건",2000] / ["조건","악명",5]
#define QJ_OP_GOLD   5    // ["금화+",v] ["금화-",v]   arg0=0더함 1뺌
#define QJ_OP_STAT   6    // ["명성+",v] 같은 것. arg0=0더함 1뺌 2정함, arg1=항목, arg2=값
#define QJ_OP_DAYS   7    // ["기한",183]
#define QJ_OP_IFITEM 8    // ["만약","아이템",id,"@라벨"]
#define QJ_OP_IFGOODS 9   // ["만약","교역품",산지,품목,수량,"@라벨"]
#define QJ_OP_JUMP   10   // ["점프","@라벨"]
#define QJ_OP_LABEL  11   // ["라벨","@이름"]  — 바이트를 만들지 않는다
#define QJ_OP_RAW    12   // ["생바이트","06 4D"]
#define QJ_OP_END    13   // ["끝"]

#define QJ_ARG_MAX   5
#define QJ_STR_MAX   256
#define QJ_LINE_MAX  256
#define QJ_EDIT_MAX  128

typedef struct {
    short op;
    short nargs;
    int   arg[QJ_ARG_MAX];
    char  str[QJ_STR_MAX];     // 대사(UTF-8) / 라벨 이름 / 생바이트 16진수
    // 대사 앞머리의 화자. 초상화를 정하는 자리라 비우면 초상화가 안 뜬다.
    // 글자로 적으면 cp932 로 바꿔 넣고("教会"), 16진수로 적으면 그 바이트 그대로 넣는다.
    char  who[32];
} QJLine;

typedef struct {
    short part, slot, idx;     // 주소. idx = -1 이면 "$"(맨 끝에 덧붙이기)
    short first, count;        // 이 주소를 대신할 줄들 (줄 풀에서의 위치)
} QJEdit;

#define QJ_ENTRY_MAX 64
#define QJ_FILE_MAX  12

typedef struct {
    int clone;            // >=0 이면 그 번호를 본떠 추가. -1 이면 index 를 덮어쓰기
    int index;            // clone < 0 일 때 대상 퀘스트 번호
    int set[QF_N];        // 이 값을 지정했나
    int val[QF_N];
    short editFirst, editCount;   // script 항목들 (편집 풀에서의 위치)
} QJEntry;

const QJEdit* QJson_EditAt(int i);
const QJLine* QJson_LineAt(int i);

typedef struct {
    wchar_t  file[24];               // "EHT.CDS"
    QJEntry  e[QJ_ENTRY_MAX];
    int      n;
} QJFile;

void     QJson_Load(HINSTANCE hinst);              // 없으면 빈 목록으로 둔다
int      QJson_Save(HINSTANCE hinst);              // 성공 1
QJFile*  QJson_File(const wchar_t* name, int create);
const wchar_t* QJson_Path(void);

// 항목 하나를 지우고 뒤를 당긴다.
void     QJson_Remove(QJFile* f, int i);
