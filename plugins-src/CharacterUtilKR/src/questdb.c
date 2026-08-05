#include "questdb.h"
#include "questjson.h"
#include "ls12.h"
#include <string.h>

#define SV_QNAME   0x25C61   // "C:EHT.CDS"
#define SV_QPTR    0x25D61   // 진행 포인터(u16)
#define SV_QDAYS   0x25C5D   // 남은 기한(일, u16)
#define SV_FAME    0x53      // 주인공 명성(u16)
#define SV_YEAR    0x15
#define SV_MIN     0x25E00   // 이 자리들을 읽으려면 최소 이만큼은 있어야 한다

#define PART_MAX   512       // ls12.h 의 파트 표 크기와 같다
#define PART_CAP   16384     // 파트 하나 최대치. 실제로는 2.2KB 가 제일 크다
#define REF_MAX    12        // 한 값이 파일 안에 박혀 있는 자리 수 상한

// 값 하나가 파일 안에서 차지하는 자리. 같은 값이 여러 군데 있으면 다 같이 고친다.
typedef struct { short part; unsigned short off; unsigned char w; } QRef;

static Ls12File     g_ls;
static unsigned char* g_part[PART_MAX];   // 풀어놓은 파트들. 편집은 여기에 한다
static unsigned     g_plen[PART_MAX];
static int          g_np = 0;

static QuestInfo    g_q[QUEST_MAX];
static QRef         g_ref[QUEST_MAX][QF_N][REF_MAX];
static int          g_qent[QUEST_MAX];    // 이 퀘스트에 걸린 quests.json 항목 번호. -1 = 없음
static int          g_n = 0;
static int          g_dirty = 0;
static HINSTANCE    g_hinst = NULL;
static int          g_jsonLoaded = 0;
static QJFile*      g_jf = NULL;          // 지금 퀘스트 파일의 json 항목들
static QuestLine    g_line[QLINE_MAX];    // 마지막으로 훑어본 퀘스트의 대사
static int          g_nline = 0;
static int          g_lineQ = -1;         // 그 퀘스트 번호. -1 = 다시 읽어야 함
static int          g_ptr = 0, g_days = 0, g_fame = 0, g_year = 0;
static int          g_status = QUEST_E_SAVE;
static wchar_t      g_file[32] = L"";
static wchar_t      g_job[48]  = L"";
static wchar_t      g_err[160] = L"";

// 건물 코드. 조건에 쓰인 건물별로 그 자리의 첫 대사를 모아 붙인 이름이다
// ("여기는 교역품 매매를…" -> 1 = 교역소). 12~15 는 대사가 겹쳐 확신이 덜하다.
static const wchar_t* kBldg[16] = {
    L"항구",   L"교역소", L"왕궁",   L"교회",
    L"주점",   L"여관",   L"조선소", L"시장",
    L"도서관", L"조합",   L"성문",   L"내 저택",
    L"총독 저택", L"명사의 저택", L"상관", L"학자 저택",
};

const wchar_t* Quest_BuildingName(int b) { return (b >= 0 && b < 16) ? kBldg[b] : L""; }
const wchar_t* Quest_FileName(void) { return g_file; }
const wchar_t* Quest_JobName(void)  { return g_job; }
const wchar_t* Quest_LastError(void){ return g_err; }
int Quest_Pointer(void)  { return g_ptr; }
int Quest_DaysLeft(void) { return g_days; }
int Quest_MyFame(void)   { return g_fame; }
int Quest_Year(void)     { return g_year; }
int Quest_Count(void)    { return g_n; }
int Quest_Status(void)   { return g_status; }
int Quest_Dirty(void)    { return g_dirty; }

int Quest_DoneCount(void)
{
    int i, n = 0;
    for (i = 0; i < g_n; i++) if (g_q[i].state == QUEST_DONE) n++;
    return n;
}

const QuestInfo* Quest_At(int i) { return (i >= 0 && i < g_n) ? &g_q[i] : NULL; }

// ---- 파일 읽기/쓰기 ----

static void GameDir(wchar_t* out)
{
    wchar_t exe[MAX_PATH]; wchar_t *p, *last;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    last = exe;
    for (p = exe; *p; p++) if (*p == L'\\' || *p == L'/') last = p;
    *last = 0;
    lstrcpyW(out, exe);
}

static void QuestPath(wchar_t* out, const wchar_t* suffix)
{
    wchar_t dir[MAX_PATH];
    GameDir(dir);
    wsprintfW(out, L"%s\\%s%s", dir, g_file, suffix);
}

static unsigned char* ReadWhole(const wchar_t* path, DWORD* szOut, DWORD minSize)
{
    HANDLE h; DWORD sz, got = 0; unsigned char* buf;
    // 게임이 세이브를 열어둔 채일 수 있으므로 공유를 넉넉히 준다.
    h = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz < minSize) { CloseHandle(h); return NULL; }
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, buf, sz, &got, NULL) || got != sz) {
        HeapFree(GetProcessHeap(), 0, buf); CloseHandle(h); return NULL;
    }
    CloseHandle(h);
    *szOut = sz;
    return buf;
}

// ---- 스크립트 훑기 ----

static unsigned Rd16(const unsigned char* d, unsigned i)
{ return (unsigned)d[i] | ((unsigned)d[i+1] << 8); }
static unsigned Rd32(const unsigned char* d, unsigned i)
{ return (unsigned)d[i] | ((unsigned)d[i+1] << 8) | ((unsigned)d[i+2] << 16) | ((unsigned)d[i+3] << 24); }

// 대사 한 줄을 유니코드로. 앞머리의 "화자：" 는 버린다.
//
// 본문은 cp949 인데 원본에서 손대지 않은 자리(화자 이름, 구분자, 자리표)만 cp932 로 남아
// 있다. 그대로 cp949 로 풀면 "긎깑긤갌"(ギルド：) 같은 한글 잡음이 되므로 셋을 걸러낸다.
//   0x81 0x46 "："       화자 끝 — 여기까지 통째로 버린다
//   0x81 0x5E "／"       선택지 구분자 — '/' 로
//   0x81 0x93 0x82 xx    "％ｓ" 같은 자리표. 게임이 실행할 때 주인공 이름·조사로 바꿔
//                        넣는 자리다. ％ｓ(이름)만 "제독"으로 채우고 나머지는 지운다.
static void TextAt(const unsigned char* d, unsigned n, unsigned i, wchar_t* out, int cap)
{
    char raw[512]; char cook[512];
    unsigned j = i, k;
    int m = 0, w;

    out[0] = 0;
    while (j < n && d[j] != 0 && j - i < sizeof(raw) - 1) raw[j - i] = (char)d[j], j++;
    k = j - i;
    raw[k] = 0;
    if (k == 0) return;

    {
        unsigned s = 0, lim = k < 40 ? k : 40;
        // 화자 머리는 앞쪽에서만 찾는다(본문에도 같은 바이트짝이 나올 수 있다).
        for (j = 0; j + 1 < lim; j++)
            if ((unsigned char)raw[j] == 0x81 && (unsigned char)raw[j+1] == 0x46) { s = j + 2; break; }
        for (j = s; j < k; j++) {
            unsigned char c0 = (unsigned char)raw[j];
            unsigned char c1 = (j + 1 < k) ? (unsigned char)raw[j+1] : 0;
            if (c0 == 0x81 && c1 == 0x5E) { cook[m++] = '/'; j++; continue; }
            if (c0 == 0x81 && c1 == 0x93 && j + 3 < k && (unsigned char)raw[j+2] == 0x82) {
                if ((unsigned char)raw[j+3] == 0x93) {          // ％ｓ = 주인공 이름
                    cook[m++] = (char)0xC1; cook[m++] = (char)0xA6;   // 제
                    cook[m++] = (char)0xB5; cook[m++] = (char)0xB6;   // 독
                }
                j += 3; continue;
            }
            cook[m++] = raw[j];
        }
        cook[m] = 0;
    }
    if (m == 0) return;

    w = MultiByteToWideChar(949, 0, cook, m, out, cap - 1);
    if (w <= 0) { out[0] = 0; return; }
    if (w > cap - 1) w = cap - 1;
    out[w] = 0;
    while (w > 0 && (out[w-1] == L' ' || out[w-1] == 0x3000)) out[--w] = 0;
}

// 덩이 하나를 훑으면 나오는 것들. off 는 값이 박힌 자리(파트 시작 기준), w 는 폭.
#define EV_MAX 96
typedef struct { unsigned char kind, w; unsigned short off; int val; } QEv;
#define EK_YEAR 1
#define EK_FAME 2
#define EK_CITY 3
#define EK_BLDG 4
#define EK_DAYS 5
#define EK_GIN  6      // 19 14 = 금화 받음
#define EK_GOUT 7      // 1A 14 = 금화 잃음
#define EK_FGAIN 8
#define EK_ITEM 9
#define EK_GORG 10
#define EK_GOODS 11
#define EK_GQTY 12

typedef struct {
    QEv ev[EV_MAX];
    int n;
    int hasYear, hasFame;
    wchar_t first[160];
    int gotFirst;
} QChunk;

static void Push(QChunk* c, int kind, unsigned off, int w, int val)
{
    if (c->n >= EV_MAX) return;
    c->ev[c->n].kind = (unsigned char)kind;
    c->ev[c->n].w    = (unsigned char)w;
    c->ev[c->n].off  = (unsigned short)off;
    c->ev[c->n].val  = val;
    c->n++;
}

// start 부터 0xFF(또는 끝)까지 걸으며 아는 오피코드만 줍는다.
// 모르는 바이트는 1씩 건너뛴다 — 표를 다 알지 못해도 여기서 쓰는 값들은
// 앞뒤 바이트가 특징적이라 오인이 잘 나지 않는다.
static void ScanChunk(const unsigned char* d, unsigned n, unsigned start, QChunk* c)
{
    unsigned i = start;
    memset(c, 0, sizeof(*c));

    while (i < n) {
        unsigned char b = d[i];
        if (b == 0xFF) break;

        if (b == 0x0A) {
            wchar_t t[160];
            TextAt(d, n, i + 1, t, 160);
            if (!c->gotFirst && t[0]) { lstrcpynW(c->first, t, 160); c->gotFirst = 1; }
            i++;
            while (i < n && d[i] != 0) i++;      // 문자열은 0x00 으로 끝난다
            if (i < n) i++;
            continue;
        }
        if (b == 0x17 && i + 3 < n) {
            unsigned v = Rd16(d, i + 2);
            switch (d[i+1]) {
            case 0x08: Push(c, EK_CITY, i + 2, 2, (int)v); break;
            case 0x10: Push(c, EK_BLDG, i + 2, 2, (int)v); break;
            case 0x00: case 0x19: break;         // 국가/지역 — 소문 슬롯. 편집 대상 아님
            default:   i++; continue;
            }
            i += 4; continue;
        }
        if (b == 0x1B && i + 3 < n && d[i+1] == 0x16) {
            Push(c, EK_YEAR, i + 2, 2, (int)Rd16(d, i + 2));
            c->hasYear = 1;
            i += 4; continue;
        }
        if (b == 0x2B && i + 8 < n && d[i+1] == 0x1C && Rd16(d, i+2) == 0x11 && d[i+4] == 0x1A) {
            Push(c, EK_FAME, i + 5, 4, (int)Rd32(d, i + 5));
            c->hasFame = 1;
            i += 9; continue;
        }
        if (b == 0x26 && i + 8 < n && d[i+1] == 0x1C && Rd16(d, i+2) == 0x1D && d[i+4] == 0x1A) {
            Push(c, EK_DAYS, i + 5, 4, (int)Rd32(d, i + 5));
            i += 9; continue;
        }
        if ((b == 0x19 || b == 0x1A) && i + 8 < n && d[i+1] == 0x1C && d[i+4] == 0x1A) {
            if (Rd16(d, i+2) == 0x11 && b == 0x19)               // 0x11 = 명성
                Push(c, EK_FGAIN, i + 5, 4, (int)Rd32(d, i + 5));
            i += 9; continue;
        }
        if ((b == 0x19 || b == 0x1A) && i + 5 < n && d[i+1] == 0x14) {
            Push(c, b == 0x19 ? EK_GIN : EK_GOUT, i + 2, 4, (int)Rd32(d, i + 2));
            i += 6; continue;
        }
        if (b == 0x43 && i + 4 < n && d[i+1] == 0x12 && d[i+2] == 0x05) {
            Push(c, EK_ITEM, i + 3, 2, (int)Rd16(d, i + 3));
            i += 5; continue;
        }
        if (b == 0x43 && i + 12 < n && d[i+1] == 0x2C && d[i+2] == 0x08 &&
            d[i+5] == 0x15 && d[i+8] == 0x1A) {
            Push(c, EK_GORG,  i + 3, 2, (int)Rd16(d, i + 3));
            Push(c, EK_GOODS, i + 6, 2, (int)Rd16(d, i + 6));
            Push(c, EK_GQTY,  i + 9, 4, (int)Rd32(d, i + 9));
            i += 13; continue;
        }
        i++;
    }
}

// ---- 값 모으기 ----
// 같은 값이 여러 군데 박혀 있을 수 있어(기한 두 번, 선금 = 실패 시 회수액 …) 후보를 다
// 모아 둔 뒤, 대표값을 고르고 그 값과 같은 자리만 편집 대상으로 남긴다.
// 값이 다른 자리까지 싸잡아 고치면(예: 거절 악명 5 와 실패 악명 80) 뜻이 달라진다.
#define CAND_MAX 24
typedef struct { QRef r; int val; } Cand;
static Cand g_cand[QF_N][CAND_MAX];
static int  g_ncand[QF_N];
static Cand g_gout[CAND_MAX];      // 1A 14 (금화 잃음) — 선금 회수액을 가려내는 데 쓴다
static int  g_ngout;

static void AddCand(int f, int part, unsigned off, int w, int val)
{
    if (f < 0 || f >= QF_N || g_ncand[f] >= CAND_MAX) return;
    g_cand[f][g_ncand[f]].r.part = (short)part;
    g_cand[f][g_ncand[f]].r.off  = (unsigned short)off;
    g_cand[f][g_ncand[f]].r.w    = (unsigned char)w;
    g_cand[f][g_ncand[f]].val    = val;
    g_ncand[f]++;
}

static const QEv* FindEv(const QChunk* c, int kind)
{
    int i;
    for (i = 0; i < c->n; i++) if (c->ev[i].kind == kind) return &c->ev[i];
    return NULL;
}

// 파트 하나를 훑어 후보를 채운다. isGate 면 개방 조건 파트다.
static void ReadPart(int part, QuestInfo* q, int isGate)
{
    const unsigned char* d = g_part[part];
    unsigned n = g_plen[part];
    int slots, s, step;

    if (!d || n < 8) return;
    step  = (int)Rd16(d, 0);
    slots = (int)Rd16(d, 2);
    if (slots <= 0 || slots > 16 || (unsigned)(4 + slots * 4) > n) return;

    for (s = 0; s < slots; s++) {
        QChunk cond, body;
        unsigned co = 4 + Rd16(d, 4 + s * 4);
        unsigned bo = 4 + Rd16(d, 6 + s * 4);
        const QEv* e;
        int i;
        if (co >= n || bo >= n) continue;

        ScanChunk(d, n, co, &cond);
        ScanChunk(d, n, bo, &body);

        if (isGate) {
            for (i = 0; i < cond.n; i++) {
                if (cond.ev[i].kind == EK_YEAR) AddCand(QF_YEAR, part, cond.ev[i].off, 2, cond.ev[i].val);
                if (cond.ev[i].kind == EK_FAME) AddCand(QF_FAME, part, cond.ev[i].off, 4, cond.ev[i].val);
            }
            if (cond.hasYear && cond.hasFame) q->condAnd = 1;
            continue;
        }

        // 소문 슬롯(도시 지정 없음)의 첫 대사가 그 퀘스트를 가장 잘 요약한다.
        e = FindEv(&cond, EK_CITY);
        if (!e && body.gotFirst && !q->summary[0]) lstrcpynW(q->summary, body.first, 160);
        if (e) {
            const QEv* b2 = FindEv(&cond, EK_BLDG);
            AddCand(QF_CITY, part, e->off, 2, e->val);
            if (b2) AddCand(QF_BLDG, part, b2->off, 2, b2->val);
        }

        for (i = 0; i < body.n; i++) {
            const QEv* v = &body.ev[i];
            switch (v->kind) {
            case EK_DAYS:  AddCand(QF_DAYS,  part, v->off, 4, v->val); break;
            case EK_FGAIN: AddCand(QF_FGAIN, part, v->off, 4, v->val); break;
            case EK_ITEM:  AddCand(QF_ITEM,  part, v->off, 2, v->val); break;
            case EK_GORG:  AddCand(QF_GORG,  part, v->off, 2, v->val); break;
            case EK_GOODS: AddCand(QF_GOODS, part, v->off, 2, v->val); break;
            case EK_GQTY:  AddCand(QF_GQTY,  part, v->off, 4, v->val); break;
            // 의뢰 파트의 금화 지급이 선금, 완료 파트의 것이 보수다.
            case EK_GIN:   AddCand(step <= 1 ? QF_ADV : QF_REW, part, v->off, 4, v->val); break;
            case EK_GOUT:
                if (g_ngout < CAND_MAX) {
                    g_gout[g_ngout].r.part = (short)part;
                    g_gout[g_ngout].r.off  = (unsigned short)v->off;
                    g_gout[g_ngout].r.w    = 4;
                    g_gout[g_ngout].val    = v->val;
                    g_ngout++;
                }
                break;
            }
        }
    }
}

// 후보 중 대표값을 골라 g_q / g_ref 에 확정한다.
// 금액·기한·명성은 큰 쪽이 그 퀘스트의 "본값"이고(작은 것은 뇌물·부분지급 같은 곁가지),
// 도시·건물·품목은 처음 나온 것이 발생 장소다.
static void Finalize(int qi)
{
    static const int kMax[QF_N] = { 1,1,0,0,1,1,1,1,0,0,0,1,1 };
    QuestInfo* q = &g_q[qi];
    int f, i, adv = 0;

    // 금화 지출(1A 14)은 두 가지가 섞여 있다 — 실패했을 때 선금을 도로 뱉는 것과,
    // 뇌물처럼 주인공이 실제로 내는 것. 액수가 선금과 같으면 앞의 것이고, 아니면 뒤의 것이다.
    // (0 짜리는 "친밀도가 내려갔다" 줄에 붙어 있는 것이라 둘 다 아니다.)
    for (i = 0; i < g_ncand[QF_ADV]; i++) if (g_cand[QF_ADV][i].val > adv) adv = g_cand[QF_ADV][i].val;
    for (i = 0; i < g_ngout; i++)
        if (g_gout[i].val > 0 && g_gout[i].val != adv)
            AddCand(QF_PAY, g_gout[i].r.part, g_gout[i].r.off, 4, g_gout[i].val);

    for (f = 0; f < QF_N; f++) {
        int best = -1;
        if (g_ncand[f] == 0) { q->v[f] = -1; q->n[f] = 0; continue; }
        best = g_cand[f][0].val;
        if (kMax[f]) for (i = 1; i < g_ncand[f]; i++) if (g_cand[f][i].val > best) best = g_cand[f][i].val;
        q->v[f] = best;
        q->n[f] = 0;
        for (i = 0; i < g_ncand[f] && q->n[f] < REF_MAX; i++)
            if (g_cand[f][i].val == best) g_ref[qi][f][q->n[f]++] = g_cand[f][i].r;
    }

    // 실패하면 선금을 그대로 회수한다. 선금만 고치고 회수액을 그대로 두면 앞뒤가 안 맞아서
    // 액수가 같은 1A 14 자리도 선금에 묶어 같이 고친다.
    if (q->v[QF_ADV] > 0)
        for (i = 0; i < g_ngout && q->n[QF_ADV] < REF_MAX; i++)
            if (g_gout[i].val == q->v[QF_ADV]) g_ref[qi][QF_ADV][q->n[QF_ADV]++] = g_gout[i].r;
}

// ---- 편집 ----

static void Poke(int part, unsigned off, int w, int v)
{
    unsigned char* d;
    if (part < 0 || part >= g_np) return;
    d = g_part[part];
    if (!d || off + (unsigned)w > g_plen[part]) return;
    d[off]   = (unsigned char)v;
    d[off+1] = (unsigned char)(v >> 8);
    if (w == 4) { d[off+2] = (unsigned char)(v >> 16); d[off+3] = (unsigned char)(v >> 24); }
}

// 값을 파트 사본에만 써넣는다(json 기록 없이). 로드할 때 json 을 되먹이는 데 쓴다.
static void ApplyValue(int qi, int f, int v)
{
    QuestInfo* q = &g_q[qi];
    int i;
    if (q->n[f] == 0 || v < 0) return;
    for (i = 0; i < q->n[f]; i++)
        Poke(g_ref[qi][f][i].part, g_ref[qi][f][i].off, g_ref[qi][f][i].w, v);
    q->v[f] = v;
}

// 이 퀘스트의 json 항목. 없으면 만든다(원본 퀘스트를 처음 고칠 때).
static QJEntry* EntryOf(int qi, int create)
{
    if (!g_jf) return NULL;
    if (g_qent[qi] >= 0 && g_qent[qi] < g_jf->n) return &g_jf->e[g_qent[qi]];
    if (!create || g_jf->n >= QJ_ENTRY_MAX) return NULL;
    memset(&g_jf->e[g_jf->n], 0, sizeof(g_jf->e[0]));
    g_jf->e[g_jf->n].clone = -1;
    g_jf->e[g_jf->n].index = qi;
    g_qent[qi] = g_jf->n;
    return &g_jf->e[g_jf->n++];
}

void Quest_SetField(int qi, int f, int v)
{
    QJEntry* e;
    if (qi < 0 || qi >= g_n || f < 0 || f >= QF_N) return;
    if (g_q[qi].n[f] == 0 || v < 0 || v == g_q[qi].v[f]) return;
    ApplyValue(qi, f, v);
    g_lineQ = -1;                   // 발생 장소가 바뀌면 대사 머리글도 다시 읽어야 한다
    e = EntryOf(qi, 1);
    if (e) { e->set[f] = 1; e->val[f] = v; g_q[qi].edited = 1; }
    g_dirty = 1;
}

int Quest_Add(int qi)
{
    QJFile* f = g_jf;
    if (qi < 0 || qi >= g_n || !f || f->n >= QJ_ENTRY_MAX) return -1;
    memset(&f->e[f->n], 0, sizeof(f->e[0]));
    f->e[f->n].clone = qi;
    f->e[f->n].index = -1;
    f->n++;
    g_dirty = 1;
    if (!Quest_Load()) return -1;
    return g_n - 1;          // 새로 붙인 것이 늘 마지막이다
}

int Quest_Reset(int qi)
{
    if (qi < 0 || qi >= g_n || !g_jf) return 0;
    if (g_qent[qi] < 0) return 0;
    QJson_Remove(g_jf, g_qent[qi]);
    g_dirty = 1;
    return Quest_Load();
}

// ---- 파일명 -> 직업 이름 ----

static void JobName(const wchar_t* f, wchar_t* out)
{
    const wchar_t* nation = L"";
    const wchar_t* job    = L"";
    if (f[0] == L'S') { lstrcpyW(out, L"이지 모드"); return; }
    if (f[0] == L'E') nation = L"에스파니아";
    else if (f[0] == L'P') nation = L"포르투갈";
    if      (f[1] == L'C' && f[2] == L'Q') job = L"정복자";
    else if (f[1] == L'D' && f[2] == L'G') job = L"발굴자";
    else if (f[1] == L'E' && f[2] == L'X') job = L"탐험가";
    else if (f[1] == L'H' && f[2] == L'T') job = L"사냥꾼";
    if (!nation[0] && !job[0]) { lstrcpyW(out, L"?"); return; }
    wsprintfW(out, L"%s %s", nation, job);
}

// ---- 로드 ----

static void FreeParts(void)
{
    int i;
    for (i = 0; i < g_np; i++)
        if (g_part[i]) { HeapFree(GetProcessHeap(), 0, g_part[i]); g_part[i] = NULL; }
    g_np = 0;
}

// 파트 목록에서 단계 0 이 나오는 자리마다 새 퀘스트를 연다.
static void BuildGroups(void)
{
    int i;
    g_n = 0;
    for (i = 0; i < g_np && g_n < QUEST_MAX; i++) {
        if (g_plen[i] < 8) continue;
        if (Rd16(g_part[i], 0) == 0) {
            QuestInfo* q = &g_q[g_n];
            int f;
            memset(q, 0, sizeof(*q));
            q->first = q->last = i;
            q->addedFrom = -1;
            for (f = 0; f < QF_N; f++) q->v[f] = -1;
            g_qent[g_n] = -1;
            g_n++;
        } else if (g_n > 0) {
            g_q[g_n - 1].last = i;
        }
    }
}

// qi 의 파트들을 그대로 복사해 파일 끝에 붙인다. 성공 1.
// 파트 안의 오프셋은 전부 파트 기준이라 통째로 옮겨도 손볼 것이 없다.
static int ClonePartsOf(int qi)
{
    int i, cnt = g_q[qi].last - g_q[qi].first + 1;
    if (qi < 0 || qi >= g_n || g_np + cnt > PART_MAX) return 0;
    for (i = 0; i < cnt; i++) {
        int src = g_q[qi].first + i, dst = g_np + i;
        g_part[dst] = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, g_plen[src]);
        if (!g_part[dst]) { g_plen[dst] = 0; return 0; }
        memcpy(g_part[dst], g_part[src], g_plen[src]);
        g_plen[dst] = g_plen[src];
    }
    g_np += cnt;
    return 1;
}

static void ApplyScript(int qi, const QJEntry* en, int srcFirst);   // 아래에 있다

int Quest_Load(void)
{
    wchar_t path[MAX_PATH], orig[MAX_PATH];
    unsigned char* sv; DWORD sz = 0;
    char apath[MAX_PATH];
    unsigned char tmp[PART_CAP];
    int i, gi, base, ci;

    FreeParts();
    g_n = 0;
    g_jf = NULL;
    g_lineQ = -1;
    g_ptr = g_days = g_fame = g_year = 0;
    g_file[0] = 0; g_job[0] = 0;
    g_status = QUEST_E_SAVE;

    {
        wchar_t dir[MAX_PATH];
        GameDir(dir);
        wsprintfW(path, L"%s\\SAVEDATA.CDS", dir);
    }
    sv = ReadWhole(path, &sz, SV_MIN);
    if (!sv) return 0;

    g_ptr  = (int)Rd16(sv, SV_QPTR);
    g_days = (int)Rd16(sv, SV_QDAYS);
    g_fame = (int)Rd16(sv, SV_FAME);
    g_year = (int)Rd16(sv, SV_YEAR);

    // "C:EHT.CDS" 처럼 드라이브명이 붙어 있다. 마지막 ':' 뒤만 쓴다.
    {
        const char* s = (const char*)(sv + SV_QNAME);
        const char* nm = s;
        int k = 0;
        for (i = 0; i < 24 && s[i]; i++) if (s[i] == ':' || s[i] == '\\') nm = s + i + 1;
        while (nm[k] && k < 20 && (unsigned char)nm[k] > 0x20) { g_file[k] = (wchar_t)nm[k]; k++; }
        g_file[k] = 0;
    }
    HeapFree(GetProcessHeap(), 0, sv);

    if (!g_file[0]) { g_status = QUEST_E_NAME; return 0; }
    JobName(g_file, g_job);

    // 게임이 읽는 .CDS 가 아니라 원본 사본(.orig)에서 시작한다. 없으면 지금 것으로 만든다.
    // 늘 원본에서 다시 만들기 때문에 quests.json 에서 항목을 지우면 그대로 원래대로 돌아간다.
    QuestPath(path, L"");
    QuestPath(orig, L".orig");
    if (GetFileAttributesW(orig) == INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(path, orig, TRUE)) { g_status = QUEST_E_FILE; return 0; }
    }
    if (!WideCharToMultiByte(CP_ACP, 0, orig, -1, apath, MAX_PATH, NULL, NULL)) {
        g_status = QUEST_E_FILE; return 0;
    }
    if (!Ls12_Open(&g_ls, apath)) { g_status = QUEST_E_FILE; return 0; }

    // 파트를 전부 풀어 들고 있는다. 편집도 저장도 이 사본을 쓴다(제일 큰 파일이 30KB 남짓).
    g_np = g_ls.count > PART_MAX ? PART_MAX : g_ls.count;
    for (i = 0; i < g_np; i++) {
        int len = Ls12_DecodePart(&g_ls, i, tmp, PART_CAP);
        g_plen[i] = len > 0 ? (unsigned)len : 0;
        g_part[i] = NULL;
        if (len > 0) {
            g_part[i] = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)len);
            if (g_part[i]) memcpy(g_part[i], tmp, (SIZE_T)len);
            else g_plen[i] = 0;
        }
    }
    Ls12_Close(&g_ls);
    g_status = QUEST_OK;

    // 1) 원본 파트만으로 퀘스트 경계를 잡는다.
    BuildGroups();
    base = g_n;

    // 2) quests.json 의 clone 항목대로 파트를 복사해 끝에 붙이고 경계를 다시 잡는다.
    //    끝에만 붙으므로 원본 퀘스트의 파트 번호가 그대로다 = 기존 세이브가 안 어긋난다.
    // Quest_Init 을 아직 안 거쳤으면(창이 먼저 열린 경우) 여기서 목록을 읽는다.
    // 안 읽은 채로 저장하면 남의 항목까지 지워 버린다.
    if (!g_jsonLoaded && g_hinst) { QJson_Load(g_hinst); g_jsonLoaded = 1; }
    g_jf = QJson_File(g_file, 1);
    if (g_jf) {
        int added = 0;
        for (ci = 0; ci < g_jf->n; ci++) {
            int src = g_jf->e[ci].clone;
            if (src < 0) continue;
            if (src >= base) continue;          // 추가한 것을 또 본뜨는 것은 막는다
            if (ClonePartsOf(src)) added++;
        }
        if (added) BuildGroups();
    }

    // 3) json 항목을 퀘스트에 이어 붙인다. clone 은 붙인 순서대로 base 뒤에 하나씩 대응한다.
    if (g_jf) {
        int nextAdded = base;
        for (ci = 0; ci < g_jf->n; ci++) {
            QJEntry* e = &g_jf->e[ci];
            int target;
            if (e->clone >= 0) {
                if (e->clone >= base || nextAdded >= g_n) continue;
                target = nextAdded++;
                g_q[target].addedFrom = e->clone;
            } else {
                if (e->index < 0 || e->index >= base) continue;
                target = e->index;
                g_q[target].edited = 1;
            }
            g_qent[target] = ci;
        }
    }

    // 4) script 편집을 먼저 반영한다 — 줄이 늘고 줄면 값의 위치도 달라지므로
    //    값 자리를 찾아 두기(Finalize) 전에 해야 한다.
    //    주소에 쓰는 파트 번호는 늘 본뜬 원본 쪽 번호다.
    for (gi = 0; gi < g_n; gi++) {
        if (g_qent[gi] < 0 || !g_jf) continue;
        {
            const QJEntry* e = &g_jf->e[g_qent[gi]];
            int src = (g_q[gi].addedFrom >= 0) ? g_q[gi].addedFrom : gi;
            if (e->editCount > 0) ApplyScript(gi, e, g_q[src].first);
        }
    }

    // 5) 값 자리를 찾고 상태를 정한 뒤 값 덮어쓰기를 얹는다.
    for (gi = 0; gi < g_n; gi++) {
        QuestInfo* q = &g_q[gi];
        memset(g_ncand, 0, sizeof(g_ncand));
        g_ngout = 0;
        ReadPart(q->first, q, 1);
        for (i = q->first + 1; i <= q->last; i++) ReadPart(i, q, 0);
        Finalize(gi);

        if      (g_ptr >  q->last)  q->state = QUEST_DONE;
        else if (g_ptr <  q->first) q->state = QUEST_LOCKED;
        else if (g_ptr == q->first) q->state = QUEST_WAIT;      // 조건 파트에 멈춰 있다
        else if (g_ptr == q->first + 1) q->state = QUEST_READY; // 의뢰 파트 대기
        else                        q->state = QUEST_ACTIVE;

        if (g_qent[gi] >= 0 && g_jf) {
            const QJEntry* e = &g_jf->e[g_qent[gi]];
            for (i = 0; i < QF_N; i++) if (e->set[i]) ApplyValue(gi, i, e->val[i]);
        }
    }
    return 1;
}

// ---- 대사 훑어보기 ----

const QuestLine* Quest_LineAt(int i)
{
    return (i >= 0 && i < g_nline) ? &g_line[i] : NULL;
}

static QuestLine* PushLine(int part, int step, int slot)
{
    QuestLine* l;
    if (g_nline >= QLINE_MAX) return NULL;
    l = &g_line[g_nline++];
    memset(l, 0, sizeof(*l));
    l->part = (short)part; l->step = (short)step; l->slot = (short)slot;
    l->a = l->b = l->c = -1;
    l->jo = -1;
    return l;
}

// 명령 하나의 생김새. 43 계열은 전부 "43 <조건식> <u16 상대오프셋>" 한 틀이고
// 조건식 길이만 다르다(190개 파트로 전수 확인). jo = 오프셋이 놓인 자리, -1 이면 분기 아님.
typedef struct { unsigned char n, sig[3]; unsigned char len; signed char jo; short kind; const wchar_t* name; } QForm;
static const QForm kForm[] = {
    { 2, {0x17,0x00},      4, -1, QL_WHERE  , L"" },
    { 2, {0x17,0x08},      4, -1, QL_WHERE  , L"" },
    { 2, {0x17,0x10},      4, -1, QL_WHERE  , L"" },
    { 2, {0x17,0x19},      4, -1, QL_WHERE  , L"" },
    { 2, {0x1B,0x16},      4, -1, QL_YEAR   , L"" },
    { 2, {0x1B,0x0B},      4, -1, QL_DISC   , L"" },
    { 2, {0x5E,0x0B},      4, -1, QL_DISC   , L"" },
    { 2, {0x2B,0x1C},      9, -1, QL_CMP    , L"" },
    { 2, {0x19,0x1C},      9, -1, QL_STAT   , L"" },
    { 2, {0x1A,0x1C},      9, -1, QL_STAT   , L"" },
    { 2, {0x26,0x1C},      9, -1, QL_STAT   , L"" },
    { 2, {0x22,0x1C},      9, -1, QL_STAT   , L"" },
    { 2, {0x19,0x14},      6, -1, QL_GOLD   , L"" },
    { 2, {0x1A,0x14},      6, -1, QL_GOLD   , L"" },
    { 3, {0x43,0x12,0x05}, 7,  5, QL_ITEM   , L"" },
    { 3, {0x43,0x2C,0x08},15, 13, QL_GOODS  , L"" },
    { 3, {0x43,0x2D,0x1C},12, 10, QL_BRANCH , L"능력치 비교" },
    { 3, {0x43,0x2E,0x1C},12, 10, QL_BRANCH , L"능력치 비교2" },
    { 3, {0x43,0x2B,0x1C},12, 10, QL_BRANCH , L"능력치 비교3" },
    { 3, {0x43,0x3A,0x0B}, 7,  5, QL_BRANCH , L"발견물" },
    { 3, {0x43,0x0F,0x0E}, 7,  5, QL_BRANCH , L"0F0E" },
    { 3, {0x43,0x00,0x15}, 6,  4, QL_BRANCH , L"0015" },
    { 2, {0x43,0x11},      6,  4, QL_BRANCH , L"선택지" },
    { 2, {0x43,0x45},      4,  2, QL_BRANCH , L"무조건" },
    { 2, {0x43,0x47},      4,  2, QL_BRANCH , L"무조건2" },
    { 2, {0x43,0x4B},      4,  2, QL_BRANCH , L"무조건3" },
};
#define FORM_N ((int)(sizeof(kForm)/sizeof(kForm[0])))

static const QForm* FormAt(const unsigned char* d, unsigned n, unsigned i)
{
    int k, j;
    for (k = 0; k < FORM_N; k++) {
        if (i + kForm[k].len > n) continue;
        for (j = 0; j < kForm[k].n; j++) if (d[i+j] != kForm[k].sig[j]) break;
        if (j == kForm[k].n) return &kForm[k];
    }
    return NULL;
}

// 덩이 하나를 줄로 쪼갠다. 줄 하나가 시작하는 위치를 pos[] 에 남긴다(분기 목표 찾기용).
static int SplitChunk(const unsigned char* d, unsigned n, unsigned start,
                      int part, int step, int slot, int chunk, int* idx,
                      unsigned short* pos, int poscap)
{
    unsigned i = start;
    int np = 0;
    while (i < n) {
        const QForm* f;
        QuestLine* l;
        if (np < poscap) pos[np] = (unsigned short)i;
        np++;
        l = PushLine(part, step, slot);
        if (!l) return np;
        l->chunk = (short)chunk;
        l->idx   = (short)(*idx); (*idx)++;
        l->off   = (unsigned short)i;

        if (d[i] == 0xFF) { l->kind = QL_END; l->len = 1; i++; break; }
        // 대사는 "<플래그> 0A <cp949> 00" 한 덩어리다. 플래그(대개 00, 더러 0B·20)를 따로
        // 세면 목록이 두 배로 길어지기만 해서 한 줄로 합친다. 점프는 138건이 플래그를,
        // 2건이 0A 를 겨냥하는데 둘 다 이 줄로 떨어지므로 표시에는 지장이 없다.
        f = (d[i] == 0x0A) ? NULL : FormAt(d, n, i);
        if (d[i] == 0x0A || (!f && i + 1 < n && d[i+1] == 0x0A)) {
            unsigned s0 = (d[i] == 0x0A) ? i + 1 : i + 2;
            unsigned e  = s0;
            while (e < n && d[e] != 0) e++;
            l->kind = QL_TEXT;
            l->a    = (d[i] == 0x0A) ? -1 : d[i];      // 플래그. -1 = 없음
            l->off  = (unsigned short)s0;
            l->len  = (unsigned short)(e - s0);
            TextAt(d, n, s0, l->text, 136);
            i = (e < n) ? e + 1 : n;
            continue;
        }
        if (!f) {
            l->kind = QL_RAW; l->len = 1;
            wsprintfW(l->text, L"%02X", d[i]);
            i++;
            continue;
        }
        l->kind = f->kind;
        l->len  = f->len;
        switch (f->kind) {
        case QL_WHERE: l->a = d[i+1]; l->b = (int)Rd16(d, i+2); break;
        case QL_YEAR:  l->a = (int)Rd16(d, i+2); break;
        case QL_DISC:  l->a = (d[i] == 0x5E); l->b = (int)Rd16(d, i+2); break;
        case QL_CMP:   l->a = (int)Rd16(d, i+2); l->b = (int)Rd32(d, i+5); break;
        case QL_STAT:  l->a = (d[i]==0x19) ? 0 : (d[i]==0x1A ? 1 : 2);
                       l->b = (int)Rd16(d, i+2); l->c2 = (int)Rd32(d, i+5); break;
        case QL_GOLD:  l->a = (d[i]==0x19) ? 0 : 1; l->b = (int)Rd32(d, i+2); break;
        case QL_ITEM:  l->a = (int)Rd16(d, i+3); break;
        case QL_GOODS: l->a = (int)Rd16(d, i+3); l->b = (int)Rd16(d, i+6);
                       l->c2 = (int)Rd32(d, i+9); break;
        default:       lstrcpynW(l->text, f->name, 136); break;
        }
        if (f->jo >= 0) {
            l->jo  = f->jo;
            l->tgt = (unsigned short)(i + f->len + Rd16(d, i + f->jo));
            l->c   = (int)l->tgt;          // ResolveJumps 가 줄번호로 바꿔 준다
        }
        i += f->len;
    }
    return np;
}

// 분기의 목표를 '바이트 위치'에서 '줄번호'로 바꿔 준다.
static void ResolveJumps(int from, int to, const unsigned short* pos, int npos, int base)
{
    int k, j;
    for (k = from; k < to; k++) {
        QuestLine* l = &g_line[k];
        if (l->c < 0) continue;
        // 배열 자리가 아니라 그 줄의 주소(슬롯 안 줄번호)로 바꿔 놓는다 — 화면에 그대로 쓴다.
        for (j = 0; j < npos; j++)
            if (pos[j] == (unsigned short)l->c) { l->c = g_line[base + j].idx; break; }
        if (j == npos) l->c = -1;      // 이 덩이 밖으로 나가는 점프는 표시하지 않는다
    }
}

int Quest_ReadLines(int qi)
{
    const QuestInfo* q;
    int p;

    if (qi == g_lineQ) return g_nline;
    g_nline = 0; g_lineQ = qi;
    q = Quest_At(qi);
    if (!q) return 0;

    for (p = q->first; p <= q->last; p++) {
        const unsigned char* d = g_part[p];
        unsigned n = g_plen[p];
        int step, slots, s;
        if (!d || n < 8) continue;
        step  = (int)Rd16(d, 0);
        slots = (int)Rd16(d, 2);
        if (slots <= 0 || slots > 16 || (unsigned)(4 + slots * 4) > n) continue;

        for (s = 0; s < slots; s++) {
            unsigned co = 4 + Rd16(d, 4 + s * 4);
            unsigned bo = 4 + Rd16(d, 6 + s * 4);
            QChunk cond;
            const QEv *ec, *eb;
            QuestLine* h;
            unsigned short pos[QLINE_MAX];
            int idx = 0, base, np;
            if (co >= n || bo >= n) continue;

            // 머리글 — 이 슬롯이 어디서 뜨는가
            ScanChunk(d, n, co, &cond);
            ec = FindEv(&cond, EK_CITY);
            eb = FindEv(&cond, EK_BLDG);
            h = PushLine(p, step, s);
            if (!h) return g_nline;
            h->kind = QL_HEADER;
            h->a = ec ? ec->val : -1;
            h->b = eb ? eb->val : -1;
            h->idx = -1;

            // 조건 덩이 -> 본문 덩이 순으로 줄번호를 이어 붙인다.
            // 분기 목표는 같은 덩이 안에서만 찾는다(덩이를 넘는 점프는 원본에 없다).
            base = g_nline;
            np = SplitChunk(d, n, co, p, step, s, 0, &idx, pos, QLINE_MAX);
            ResolveJumps(base, g_nline, pos, np, base);

            base = g_nline;
            np = SplitChunk(d, n, bo, p, step, s, 1, &idx, pos, QLINE_MAX);
            ResolveJumps(base, g_nline, pos, np, base);
        }
    }
    return g_nline;
}

// ---- 스크립트 편집 (quests.json 의 script) ----
//
// 파트를 줄로 쪼갠 뒤 편집 목록대로 갈아 끼우고 다시 조립한다. 길이가 변하므로
// 파트 머리의 슬롯표와 43 계열 분기의 상대 오프셋을 전부 다시 낸다.
// 주소(파트:슬롯:줄)는 늘 원본 기준이라, 줄을 넣고 빼도 다른 주소가 안 밀린다.

#define EM_LINE_MAX  400
#define EM_BYTE_MAX  272
#define EM_LABEL_MAX 32

typedef struct {
    unsigned char b[EM_BYTE_MAX];
    int  len;
    int  jo;          // 이 줄 안에서 점프 오프셋이 놓인 자리. -1 = 분기 아님
    int  tgtOld;      // 원본 줄의 목표(옛 바이트 위치). -1 = 라벨을 씀
    int  tgtLabel;    // 라벨 번호. -1 = 없음
    int  labelHere;   // 이 줄이 라벨이면 라벨 번호. -1 = 아님
    int  oldPos;      // 원본에서의 위치(-1 = 새로 넣은 줄)
    int  newPos;
} EmLine;

static EmLine g_em[EM_LINE_MAX];
static int    g_nem;
static char   g_label[EM_LABEL_MAX][32];
static int    g_nlabel;

static int LabelId(const char* name)
{
    int i;
    for (i = 0; i < g_nlabel; i++) if (lstrcmpA(g_label[i], name) == 0) return i;
    if (g_nlabel >= EM_LABEL_MAX) return -1;
    lstrcpynA(g_label[g_nlabel], name, 32);
    return g_nlabel++;
}

static EmLine* EmPush(void)
{
    EmLine* e;
    if (g_nem >= EM_LINE_MAX) return NULL;
    e = &g_em[g_nem++];
    memset(e, 0, sizeof(*e));
    e->jo = e->tgtOld = e->tgtLabel = e->labelHere = e->oldPos = -1;
    return e;
}

static void PutU16(unsigned char* p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void PutU32(unsigned char* p, int v)
{ p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }

// json 줄 하나를 바이트로. 성공 1.
static int EmitSpec(const QJLine* s)
{
    EmLine* e = EmPush();
    if (!e) return 0;
    switch (s->op) {
    case QJ_OP_LABEL:
        e->len = 0;
        e->labelHere = LabelId(s->str);
        return e->labelHere >= 0;
    case QJ_OP_TEXT: {
        // UTF-8 -> cp949. 게임 글꼴이 cp949 라 여기서 맞춰 둔다.
        wchar_t w[QJ_STR_MAX];
        int n;
        e->b[0] = (unsigned char)s->arg[0];
        e->b[1] = 0x0A;
        n = MultiByteToWideChar(CP_UTF8, 0, s->str, -1, w, QJ_STR_MAX);
        if (n <= 0) return 0;
        n = WideCharToMultiByte(949, 0, w, -1, (char*)e->b + 2, EM_BYTE_MAX - 3, NULL, NULL);
        if (n <= 0) return 0;
        e->len = 2 + n;          // n 에 널이 포함돼 있다 = 문자열 끝
        return 1; }
    case QJ_OP_WHERE:
        e->b[0] = 0x17; e->b[1] = (unsigned char)s->arg[0]; PutU16(e->b + 2, s->arg[1]);
        e->len = 4; return 1;
    case QJ_OP_YEAR:
        e->b[0] = 0x1B; e->b[1] = 0x16; PutU16(e->b + 2, s->arg[0]);
        e->len = 4; return 1;
    case QJ_OP_CMP:
        e->b[0] = 0x2B; e->b[1] = 0x1C; PutU16(e->b + 2, s->arg[0]);
        e->b[4] = 0x1A; PutU32(e->b + 5, s->arg[1]);
        e->len = 9; return 1;
    case QJ_OP_STAT:
        e->b[0] = (unsigned char)(s->arg[0] == 0 ? 0x19 : (s->arg[0] == 1 ? 0x1A : 0x26));
        e->b[1] = 0x1C; PutU16(e->b + 2, s->arg[1]);
        e->b[4] = 0x1A; PutU32(e->b + 5, s->arg[2]);
        e->len = 9; return 1;
    case QJ_OP_DAYS:
        e->b[0] = 0x26; e->b[1] = 0x1C; PutU16(e->b + 2, 0x1D);
        e->b[4] = 0x1A; PutU32(e->b + 5, s->arg[0]);
        e->len = 9; return 1;
    case QJ_OP_GOLD:
        e->b[0] = (unsigned char)(s->arg[0] ? 0x1A : 0x19); e->b[1] = 0x14;
        PutU32(e->b + 2, s->arg[1]);
        e->len = 6; return 1;
    case QJ_OP_IFITEM:
        e->b[0] = 0x43; e->b[1] = 0x12; e->b[2] = 0x05; PutU16(e->b + 3, s->arg[0]);
        e->len = 7; e->jo = 5; e->tgtLabel = LabelId(s->str);
        return e->tgtLabel >= 0;
    case QJ_OP_IFGOODS:
        e->b[0] = 0x43; e->b[1] = 0x2C; e->b[2] = 0x08; PutU16(e->b + 3, s->arg[0]);
        e->b[5] = 0x15; PutU16(e->b + 6, s->arg[1]);
        e->b[8] = 0x1A; PutU32(e->b + 9, s->arg[2]);
        e->len = 15; e->jo = 13; e->tgtLabel = LabelId(s->str);
        return e->tgtLabel >= 0;
    case QJ_OP_JUMP:
        e->b[0] = 0x43; e->b[1] = 0x45;
        e->len = 4; e->jo = 2; e->tgtLabel = LabelId(s->str);
        return e->tgtLabel >= 0;
    case QJ_OP_RAW: {
        const char* p = s->str;
        int n = 0;
        while (*p && n < EM_BYTE_MAX) {
            int hi, lo;
            while (*p == ' ') p++;
            if (!*p) break;
            hi = (*p >= '0' && *p <= '9') ? *p - '0' : ((*p | 32) >= 'a' && (*p | 32) <= 'f') ? (*p | 32) - 'a' + 10 : -1;
            p++;
            lo = (*p >= '0' && *p <= '9') ? *p - '0' : ((*p | 32) >= 'a' && (*p | 32) <= 'f') ? (*p | 32) - 'a' + 10 : -1;
            if (hi < 0 || lo < 0) return 0;
            e->b[n++] = (unsigned char)((hi << 4) | lo);
            p++;
        }
        e->len = n; return n > 0; }
    case QJ_OP_END:
        e->b[0] = 0xFF; e->len = 1; return 1;
    }
    return 0;
}

// 원본 줄 하나를 그대로 담는다.
static int EmitOrig(const unsigned char* d, const QuestLine* l)
{
    EmLine* e = EmPush();
    unsigned start = (l->kind == QL_TEXT && l->a >= 0) ? (unsigned)l->off - 2
                   : (l->kind == QL_TEXT)              ? (unsigned)l->off - 1
                   : (unsigned)l->off;
    unsigned len = (l->kind == QL_TEXT) ? (unsigned)(l->off + l->len + 1 - start) : l->len;
    if (!e || len == 0 || len > EM_BYTE_MAX) return 0;
    memcpy(e->b, d + start, len);
    e->len = (int)len;
    e->oldPos = (int)start;
    if (l->jo >= 0) { e->jo = l->jo; e->tgtOld = l->tgt; }
    return 1;
}

// 이 주소를 대신할 편집 항목을 찾는다.
static const QJEdit* FindEdit(const QJEntry* en, int part, int slot, int idx)
{
    int k;
    for (k = 0; k < en->editCount; k++) {
        const QJEdit* ed = QJson_EditAt(en->editFirst + k);
        if (ed && ed->part == part && ed->slot == slot && ed->idx == idx) return ed;
    }
    return NULL;
}

// 파트 하나를 편집 목록대로 다시 만든다. 성공 1.
// partNo 는 원본 번호(주소에 쓰인 것)이고 dst 는 실제로 고칠 파트 번호다
// (본떠 추가한 퀘스트는 파트 번호가 다르지만 주소는 본뜬 쪽 번호로 쓴다).
static int RebuildPart(int partNo, int dst, const QJEntry* en)
{
    unsigned char out[32768];
    const unsigned char* d = g_part[dst];
    unsigned n = g_plen[dst];
    int slots, s, k, pass, pos, hdr, touched = 0;
    int cs[16], bs[16];
    unsigned short chunkOff[16][2];

    if (!d || n < 8) return 0;
    slots = (int)Rd16(d, 2);
    if (slots <= 0 || slots > 16 || (unsigned)(4 + slots * 4) > n) return 0;

    // 이 파트에 걸린 편집이 하나라도 있나
    for (k = 0; k < en->editCount; k++) {
        const QJEdit* ed = QJson_EditAt(en->editFirst + k);
        if (ed && ed->part == partNo) { touched = 1; break; }
    }
    if (!touched) return 1;

    g_nem = 0; g_nlabel = 0;
    for (s = 0; s < 16; s++) { cs[s] = bs[s] = -1; }

    // 원본 줄을 다시 읽어 온다(Quest_ReadLines 는 퀘스트 단위라 여기서는 파트만 쓴다).
    // 조건 덩이 -> 본문 덩이 순으로 g_line 에 이미 담겨 있다.
    // 덩이 시작을 기억하며 줄을 하나씩 옮긴다.
    hdr = 4 + slots * 4;
    pos = hdr;
    for (pass = 0; pass < 2; pass++) {            // 0 = 조건 덩이들, 1 = 본문 덩이들
        for (s = 0; s < slots; s++) {
            int i, start = g_nem;
            for (i = 0; i < g_nline; i++) {
                const QuestLine* l = &g_line[i];
                const QJEdit* ed;
                if (l->part != dst || l->slot != s || l->chunk != pass) continue;
                if (l->kind == QL_HEADER) continue;
                ed = FindEdit(en, partNo, s, l->idx);
                if (ed) {
                    int j;
                    for (j = 0; j < ed->count; j++)
                        if (!EmitSpec(QJson_LineAt(ed->first + j))) return 0;
                    // 지운 줄이라도 그 자리를 겨냥하던 점프가 있으므로, 원본 위치는
                    // "이 자리에 새로 놓인 첫 줄"로 잇는다(아래 MapOld 에서 처리).
                    if (ed->count == 0 || g_em[g_nem-1].oldPos < 0) {
                        // 자리표시만 남긴다: 길이 0 짜리 줄
                        EmLine* ph = EmPush();
                        if (!ph) return 0;
                        ph->len = 0;
                        ph->oldPos = (l->kind == QL_TEXT)
                                   ? (l->a >= 0 ? l->off - 2 : l->off - 1) : l->off;
                    }
                } else {
                    if (!EmitOrig(d, l)) return 0;
                }
            }
            // 이 슬롯 끝에 덧붙이기 ($)
            if (pass == 1) {
                const QJEdit* ap = FindEdit(en, partNo, s, -1);
                if (ap) {
                    int j, ins = g_nem;
                    // 끝(FF) 줄 앞에 끼운다
                    for (j = 0; j < ap->count; j++)
                        if (!EmitSpec(QJson_LineAt(ap->first + j))) return 0;
                    if (g_nem > ins && ins > start && g_em[ins-1].len == 1 && g_em[ins-1].b[0] == 0xFF) {
                        EmLine tmp = g_em[ins-1];
                        for (j = ins - 1; j < g_nem - 1; j++) g_em[j] = g_em[j+1];
                        g_em[g_nem-1] = tmp;
                    }
                }
            }
            if (pass == 0) cs[s] = start; else bs[s] = start;
            // 덩이 경계를 기억해 두려고 시작 줄 번호를 담았다. 위치는 아래에서 잰다.
        }
    }

    // 자리 배치 — 덩이는 담은 순서(조건들 -> 본문들) 그대로 이어 붙인다.
    {
        int i, nb = 0, bnd[34];
        for (pass = 0; pass < 2; pass++) for (s = 0; s < slots; s++)
            bnd[nb++] = (pass == 0) ? cs[s] : bs[s];
        bnd[nb] = g_nem;
        for (i = 0; i < nb; i++) {
            int from = bnd[i], to = bnd[i + 1], j;
            int sl = i % slots, ps = i / slots;
            chunkOff[sl][ps] = (unsigned short)(pos - 4);
            for (j = from; j < to; j++) { g_em[j].newPos = pos; pos += g_em[j].len; }
        }
    }
    if ((unsigned)pos > sizeof(out)) return 0;

    // 점프 다시 계산
    for (k = 0; k < g_nem; k++) {
        EmLine* e = &g_em[k];
        int tgt = -1, j;
        if (e->jo < 0) continue;
        if (e->tgtLabel >= 0) {
            for (j = 0; j < g_nem; j++) if (g_em[j].labelHere == e->tgtLabel) { tgt = g_em[j].newPos; break; }
        } else if (e->tgtOld >= 0) {
            for (j = 0; j < g_nem; j++) if (g_em[j].oldPos == e->tgtOld) { tgt = g_em[j].newPos; break; }
            // 대사 줄 안쪽(0A)을 겨냥하던 드문 경우
            if (tgt < 0)
                for (j = 0; j < g_nem; j++)
                    if (g_em[j].oldPos >= 0 && g_em[j].oldPos + 1 == e->tgtOld) { tgt = g_em[j].newPos + 1; break; }
        }
        if (tgt < 0) return 0;                       // 목표를 못 찾으면 통째로 포기(원본 유지)
        { int off = tgt - (e->newPos + e->len);
          if (off < 0 || off > 0xFFFF) return 0;
          PutU16(e->b + e->jo, off); }
    }

    // 조립
    memset(out, 0, hdr);
    PutU16(out, (int)Rd16(d, 0));                    // 단계 번호
    PutU16(out + 2, slots);
    for (s = 0; s < slots; s++) {
        PutU16(out + 4 + s * 4, chunkOff[s][0]);
        PutU16(out + 6 + s * 4, chunkOff[s][1]);
    }
    for (k = 0; k < g_nem; k++)
        if (g_em[k].len) memcpy(out + g_em[k].newPos, g_em[k].b, g_em[k].len);

    // 갈아 끼우기
    {
        unsigned char* nb = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)pos);
        if (!nb) return 0;
        memcpy(nb, out, (SIZE_T)pos);
        HeapFree(GetProcessHeap(), 0, g_part[dst]);
        g_part[dst] = nb;
        g_plen[dst] = (unsigned)pos;
    }
    return 1;
}

// 퀘스트 qi 에 걸린 script 편집을 적용한다.
static void ApplyScript(int qi, const QJEntry* en, int srcFirst)
{
    int p;
    if (!en || en->editCount <= 0) return;
    g_lineQ = -1;
    Quest_ReadLines(qi);                       // 원본 줄을 g_line 에 담는다
    for (p = g_q[qi].first; p <= g_q[qi].last; p++) {
        int partNo = srcFirst + (p - g_q[qi].first);   // 주소에 쓰인 원본 파트 번호
        if (!RebuildPart(partNo, p, en))
            OutputDebugStringW(L"[CharacterUtilKR] script 적용 실패 — 그 파트는 원본 그대로 둡니다.");
    }
    g_lineQ = -1;
}

// ---- 저장 ----

int Quest_HasBackup(void)
{
    wchar_t p[MAX_PATH];
    if (!g_file[0]) return 0;
    QuestPath(p, L".orig");
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static int WriteWhole(const wchar_t* path, const unsigned char* d, unsigned n)
{
    HANDLE h; DWORD put = 0;
    h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(h, d, n, &put, NULL) || put != n) { CloseHandle(h); return 0; }
    CloseHandle(h);
    return 1;
}

// 지금 메모리에 있는 파트들로 게임이 읽을 .CDS 를 다시 만든다.
// 임시 파일에 쓴 뒤 옮긴다 — 쓰다 말고 죽어도 반쪽짜리 파일이 남지 않게.
static int WriteCds(void)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    unsigned cap, made;
    unsigned char* out;
    int ok;

    if (g_status != QUEST_OK || g_np <= 0) { lstrcpyW(g_err, L"읽어둔 퀘스트 파일이 없습니다."); return 0; }
    QuestPath(path, L"");
    QuestPath(tmp, L".new");

    cap = Ls12_BuildCap(g_plen, g_np);
    out = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, cap);
    if (!out) { lstrcpyW(g_err, L"메모리가 모자랍니다."); return 0; }
    made = Ls12_Build(g_part, g_plen, g_np, out, cap);
    if (!made) {
        HeapFree(GetProcessHeap(), 0, out);
        lstrcpyW(g_err, L"파일을 다시 묶는 데 실패했습니다.");
        return 0;
    }
    ok = WriteWhole(tmp, out, made);
    HeapFree(GetProcessHeap(), 0, out);
    if (!ok) { wsprintfW(g_err, L"%s.new 에 쓰지 못했습니다.", g_file); return 0; }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp);
        wsprintfW(g_err, L"%s 를 바꿔 넣지 못했습니다(게임이 파일을 잡고 있을 수 있습니다).", g_file);
        return 0;
    }
    return 1;
}

int Quest_ResetAll(void)
{
    if (!g_jf) return 0;
    g_jf->n = 0;
    g_dirty = 1;
    return Quest_Load();
}

// 게임이 읽을 .CDS 를 지금 목록대로 맞춘다. 성공 1.
static int Sync(void)
{
    if (g_status != QUEST_OK) { lstrcpyW(g_err, L"읽어둔 퀘스트 파일이 없습니다."); return 0; }
    if (g_jf && g_jf->n > 0) return WriteCds();
    // 목록이 비었으면 원본 그대로여야 한다. 지난번에 고쳐 놓은 것이 남아 있을 수 있어
    // .orig 를 도로 덮는다(이미 원본이면 같은 내용을 다시 쓸 뿐이다).
    {
        wchar_t path[MAX_PATH], orig[MAX_PATH];
        QuestPath(path, L""); QuestPath(orig, L".orig");
        if (CopyFileW(orig, path, FALSE)) return 1;
        wsprintfW(g_err, L"%s 를 원본으로 되돌리지 못했습니다(게임이 파일을 잡고 있을 수 있습니다).", g_file);
        return 0;
    }
}

int Quest_Save(void)
{
    g_err[0] = 0;
    if (!QJson_Save(g_hinst)) { lstrcpyW(g_err, L"quests.json 을 쓰지 못했습니다."); return 0; }
    if (!Sync()) return 0;
    g_dirty = 0;
    return 1;
}

void Quest_Init(HINSTANCE hinst)
{
    g_hinst = hinst;
    QJson_Load(hinst);
    g_jsonLoaded = 1;
    if (Quest_Load()) {
        OutputDebugStringW(Sync() ? L"[CharacterUtilKR] 퀘스트 파일 반영 완료."
                                  : L"[CharacterUtilKR] 퀘스트 파일 쓰기 실패.");
        // quests.json 이 아직 없으면 설명이 든 빈 파일을 깔아 둔다.
        // 파일이 있어야 형식을 보고 고칠 수 있다(Quest_Load 가 이 퀘스트 파일 칸을 만들어 둔다).
        if (GetFileAttributesW(QJson_Path()) == INVALID_FILE_ATTRIBUTES)
            OutputDebugStringW(QJson_Save(hinst) ? L"[CharacterUtilKR] quests.json 을 새로 만들었습니다."
                                                 : L"[CharacterUtilKR] quests.json 을 만들지 못했습니다.");
    }
    g_dirty = 0;
}
