#pragma once
#include <windows.h>

// 주인공 퀘스트(직업 이벤트) 목록 · 진행 상황 · 값 편집.
//
// 어느 이벤트 파일을 쓰는지는 세이브에 문자열로 박혀 있다. 국가 x 직업 8종
// (E/P = 에스파니아/포르투갈, CQ 정복자 · DG 발굴자 · EX 탐험가 · HT 사냥꾼)
// 이고 이지 모드는 STORY0/STORY1 이다.
//
//   SAVEDATA.CDS +0x25C61   "C:EHT.CDS"  — 쓰는 퀘스트 파일(앞의 드라이브명은 버린다)
//   SAVEDATA.CDS +0x25D61   진행 포인터 — 다음에 실행할 이벤트 파트 번호(u16)
//   SAVEDATA.CDS +0x25C5D   남은 기한(일, u16). 의뢰를 받은 상태에서만 뜻이 있다
//   SAVEDATA.CDS +0x53      주인공 명성(u16) — 개방 조건과 견주는 데 쓴다
//
// 포인터는 앞으로만 간다. 그래서 퀘스트 순서가 파일에 적힌 대로 고정이고, 지나간
// 의뢰는 다시 못 받는다. "완료한 퀘스트 목록" 같은 별도 표는 세이브에 없다.
// 세이브 3쌍(에스톡 before/received/completed, 견직물 before/received/completed)으로
// 포인터가 1 -> 2 -> 4 -> 5 -> 6 으로 도는 것을 확인했다. 조건 파트는 조건이 차는
// 순간 자동으로 넘어가므로 포인터가 그 번호에 멈춰 있으면 아직 조건 미달이라는 뜻이다.
//
// ---- 이벤트 파일 구조 (LS12 아카이브, 파트 하나 = 이벤트 하나) ----
//   +0x00 u16  단계 번호. 0 = 개방 조건, 1 = 의뢰, 2 이상 = 완료보고/후속
//   +0x02 u16  슬롯 수 N  (같은 이벤트의 서로 다른 발생 상황)
//   +0x04      N x (u16 조건오프셋, u16 본문오프셋) — 둘 다 +0x04 기준
//   조건/본문은 각각 0xFF 로 끝난다.
//
// 스크립트 오피코드는 obsidian "11.이벤트 스크립트 오피코드 표" 참고. 여기서 읽는 것만:
//   17 00 <국가>  17 08 <도시>  17 10 <건물>  17 19 <지역>   발생 장소 조건
//   1B 16 <연도>                                            연도 조건
//   2B 1C 11 00 1A <u32>                                    명성 조건
//   26 1C 1D 00 1A <u32>                                    기한(일) 부여
//   19 14 <u32> / 1A 14 <u32>                               금화 증/감
//   19 1C 11 00 1A <u32>                                    명성 증가
//   43 12 05 <아이템>                                       아이템 소지 요구
//   43 2C 08 <산지> 15 <교역품> 1A <수량>                   교역품 요구
//   0A <cp949 문자열> 00                                    대사
// 같은 덩이 안은 AND, 덩이(0xFF)가 갈리면 OR 다.
//
// 세이브는 절대 쓰지 않는다 — savedata.h 와 같은 원칙(실행 중 덮어쓰면 깨진다).
// 고치는 것은 퀘스트 이벤트 파일(.CDS)뿐이고, 그것도 처음 저장할 때 <이름>.orig 로
// 원본을 남긴 뒤에 쓴다.

#define QUEST_MAX 64

#define QUEST_LOCKED  0   // 앞 퀘스트가 안 끝남
#define QUEST_WAIT    1   // 차례는 왔는데 개방 조건 미달
#define QUEST_READY   2   // 조건 충족. 해당 장소로 가면 의뢰가 뜬다
#define QUEST_ACTIVE  3   // 수락함 — 진행 중
#define QUEST_DONE    4   // 끝남

// ---- 편집 가능한 값 ----
// 전부 파트 안에 고정폭(u16/u32)으로 박혀 있어 바이트만 갈아끼우면 된다.
// 대사처럼 길이가 변하는 것은 파트 머리의 오프셋 표를 다시 계산해야 해서 여기서 안 다룬다.
#define QF_YEAR   0    // 개방 연도
#define QF_FAME   1    // 개방 명성
#define QF_CITY   2    // 의뢰가 뜨는 도시
#define QF_BLDG   3    // 의뢰가 뜨는 건물
#define QF_DAYS   4    // 기한(일)
#define QF_ADV    5    // 선금 (실패 시 회수액도 같이 따라간다)
#define QF_REW    6    // 성공 보수
#define QF_FGAIN  7    // 명성 보상
#define QF_ITEM   8    // 요구 아이템
#define QF_GORG   9    // 요구 교역품 산지
#define QF_GOODS  10   // 요구 교역품 종류
#define QF_GQTY   11   // 요구 교역품 수량
// 내가 내는 돈 — 뇌물처럼 주인공이 지불하는 금액(1A 14). 실패 시 선금 회수는 QF_ADV 에
// 묶여 있으므로 여기 안 들어온다. 원본에는 넷뿐이다(뉘른베르크 수도원 1000, 튀니스 100).
#define QF_PAY    12
#define QF_N      13

typedef struct {
    int  first, last;        // 이 퀘스트가 차지하는 파트 번호 범위
    int  state;              // QUEST_*
    int  condAnd;            // 1 = 연도·명성이 같은 덩이(AND), 0 = 따로(OR)
    int  addedFrom;          // -1 = 원본 퀘스트, >=0 = 그 번호를 본떠 새로 붙인 것
    int  edited;             // 1 = quests.json 에서 값을 덮어쓴 퀘스트
    int  v[QF_N];            // 현재 값. 그 자리가 없으면 -1
    int  n[QF_N];            // 파일 안에서 그 값이 박혀 있는 자리 수(0 = 편집 불가)
    wchar_t summary[160];    // 소문 대사 한 줄 — "세빌리아 조합에서 일할 사람을 찾고 있었네."
} QuestInfo;

// 플러그인이 뜰 때 한 번. quests.json 을 읽어 게임이 쓸 .CDS 를 다시 만든다.
// 창을 열지 않아도 반영되도록 감시 스레드 초입에서 부른다.
void Quest_Init(HINSTANCE hinst);

// 세이브 -> <이름>.CDS.orig -> quests.json 순으로 읽어 목록을 만든다. 성공 1.
int  Quest_Load(void);

const wchar_t* Quest_FileName(void);   // "EHT.CDS". 실패하면 L""
const wchar_t* Quest_JobName(void);    // "에스파니아 사냥꾼"
int  Quest_Pointer(void);              // 진행 포인터(파트 번호)
int  Quest_DaysLeft(void);             // 남은 기한(일)
int  Quest_MyFame(void);               // 주인공 명성
int  Quest_Year(void);                 // 세이브의 연도
int  Quest_Count(void);
int  Quest_DoneCount(void);
const QuestInfo* Quest_At(int i);

// ---- 편집 ----
// 값을 바꾸면 메모리에 풀어둔 파트 + quests.json 모델에 반영된다.
// Quest_Save 를 불러야 quests.json 과 게임이 읽는 .CDS 에 쓰인다.
void Quest_SetField(int qi, int f, int v);
int  Quest_Dirty(void);
int  Quest_Save(void);              // quests.json 저장 + .CDS 재생성. 성공 1
int  Quest_HasBackup(void);         // <이름>.CDS.orig 가 있나

// qi 를 본뜬 퀘스트를 파일 끝에 새로 붙인다. 새 퀘스트 번호, 실패 -1.
// 끝에만 붙이는 이유는 questjson.h 참고(세이브 진행 포인터가 파트 번호라서).
int  Quest_Add(int qi);

// 이 퀘스트에 걸린 quests.json 항목을 지운다. 원본 퀘스트면 값이 원래대로 돌아가고,
// 추가한 퀘스트면 통째로 사라진다. 성공 1.
int  Quest_Reset(int qi);

// 이 파일에 걸린 항목을 전부 지운다(= 원본 그대로). 성공 1.
int  Quest_ResetAll(void);

// ---- 스크립트 줄 훑어보기 ----
// 파트를 명령 한 줄씩으로 쪼개 보여준다. 주소는 "파트:슬롯:줄번호" 이고, 줄번호는 그
// 슬롯 안에서 조건 덩이부터 0 으로 세어 나간다(quests.json 의 script 키와 같은 번호다).
//
// 쪼갠 것을 도로 합치면 원본과 바이트가 같고(파트 190개 확인), 대사 길이를 바꿔도
// 모든 분기가 같은 줄을 가리키도록 오프셋을 다시 낼 수 있다는 것까지 확인했다.
// 위치를 참조하는 명령은 43 계열 하나뿐이고 형태가 통일돼 있다:
//     43 <조건식> <u16 상대오프셋>      오프셋은 명령 끝 기준 전방
// 뜻을 모르는 바이트는 QL_RAW 로 그대로 들고 있는다 — 몰라도 보존만 하면 되기 때문이다.
#define QLINE_MAX 320

#define QL_HEADER 0    // 슬롯 머리글(줄이 아니라 구분선). a=도시 b=건물 (-1 없음)
#define QL_TEXT   1    // 대사
#define QL_RAW    2    // 뜻 모르는 바이트. text 에 16진수
#define QL_END    3    // 덩이 끝(FF)
#define QL_WHERE  4    // a: 0x00 국가 0x08 도시 0x10 건물 0x19 지역, b: 값
#define QL_YEAR   5    // a: 연도
#define QL_DISC   6    // a: 0 발견함 / 1 아직, b: 발견물
#define QL_CMP    7    // 조건 비교. a=항목 b=값
#define QL_STAT   8    // a: 0 더함 1 뺌 2 정함, b=항목, c=값
#define QL_GOLD   9    // a: 0 받음 1 냄, b=액수
#define QL_ITEM   10   // 분기 — 아이템 소지. a=아이템, c=점프 목표 줄번호
#define QL_GOODS  11   // 분기 — 교역품 소지. a=산지 b=품목 c2=수량, c=목표 줄
#define QL_BRANCH 12   // 그 밖의 43 분기. text 에 설명, c=목표 줄

typedef struct {
    short   part, step, slot;
    short   chunk;       // 0 = 조건 덩이, 1 = 본문 덩이
    short   idx;         // 슬롯 안 줄번호 (quests.json 주소의 셋째 칸)
    short   kind;        // QL_*
    int     a, b, c, c2;       // c = 분기 목표의 줄번호(-1 없음)
    short   jo;                // 점프 오프셋이 놓인 자리(명령 안). -1 = 분기 아님
    unsigned short tgt;        // 분기 목표의 원본 바이트 위치
    unsigned short off, len;   // 파트 안 위치/길이
    wchar_t text[136];
} QuestLine;

// qi 의 스크립트를 읽어 들인다. 줄 수를 돌려준다. 다른 퀘스트를 부르면 갈아탄다.
int  Quest_ReadLines(int qi);
const QuestLine* Quest_LineAt(int i);

// 실패 사유(화면에 그대로 띄운다)
#define QUEST_OK        0
#define QUEST_E_SAVE    1   // SAVEDATA.CDS 를 못 읽음
#define QUEST_E_NAME    2   // 세이브에서 퀘스트 파일명을 못 찾음
#define QUEST_E_FILE    3   // 그 .CDS 를 못 열거나 LS12 가 아님
int  Quest_Status(void);
const wchar_t* Quest_LastError(void);   // 저장/복원 실패 사유. 없으면 L""

const wchar_t* Quest_BuildingName(int b);
