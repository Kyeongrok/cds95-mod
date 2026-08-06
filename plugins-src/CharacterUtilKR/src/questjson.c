#include "questjson.h"
#include <string.h>

// quests.json 을 읽고 쓰는 작은 스캐너. 값이 전부 숫자라 온전한 JSON 파서를 두지 않고
// 키 분기 + 값 건너뛰기만 한다(WorldMapKR 의 cities.json 스캐너와 같은 방식).

static QJFile g_f[QJ_FILE_MAX];
static int    g_nf = 0;
static wchar_t g_path[MAX_PATH] = L"";
static QJEdit g_edit[QJ_EDIT_MAX];
static int    g_nedit = 0;
static QJLine g_eline[QJ_LINE_MAX];
static int    g_neline = 0;

const QJEdit* QJson_EditAt(int i) { return (i >= 0 && i < g_nedit) ? &g_edit[i] : NULL; }
const QJLine* QJson_LineAt(int i) { return (i >= 0 && i < g_neline) ? &g_eline[i] : NULL; }

// 줄 이름표. 첫 낱말로 명령을 가린다.
static const struct { const char* name; short op; int a0; } kOp[] = {
    { "대사",     QJ_OP_TEXT,   0 },
    { "국가",     QJ_OP_WHERE,  0x00 },
    { "도시",     QJ_OP_WHERE,  0x08 },
    { "건물",     QJ_OP_WHERE,  0x10 },
    { "지역",     QJ_OP_WHERE,  0x19 },
    { "연도",     QJ_OP_YEAR,   0 },
    { "명성조건", QJ_OP_CMP,    17 },
    { "조건",     QJ_OP_CMP,    -1 },
    { "금화+",    QJ_OP_GOLD,   0 },
    { "금화-",    QJ_OP_GOLD,   1 },
    { "기한",     QJ_OP_DAYS,   0 },
    { "만약",     QJ_OP_IFITEM, 0 },      // 둘째 낱말로 아이템/교역품을 가른다
    { "점프",     QJ_OP_JUMP,   0 },
    { "라벨",     QJ_OP_LABEL,  0 },
    { "생바이트", QJ_OP_RAW,    0 },
    { "끝",       QJ_OP_END,    0 },
};
#define OP_N ((int)(sizeof(kOp)/sizeof(kOp[0])))

// 능력치 이름 -> 항목 번호. "명성+" 처럼 붙여 쓰는 형태도 받는다.
static const struct { const char* name; int id; } kStat[] = {
    { "악명", 4 }, { "무력", 6 }, { "명성", 17 }, { "운", 18 },
    { "지력", 21 }, { "매력", 22 }, { "기한", 29 },
};
#define STAT_N ((int)(sizeof(kStat)/sizeof(kStat[0])))

// "명성+" / "악명-" / "명성=" 를 가른다. 맞으면 항목 번호, 아니면 -1. mode 로 +/-/= 를 낸다.
static int StatWord(const char* s, int* mode)
{
    int i, n = lstrlenA(s);
    if (n < 2) return -1;
    if      (s[n-1] == '+') *mode = 0;
    else if (s[n-1] == '-') *mode = 1;
    else if (s[n-1] == '=') *mode = 2;
    else return -1;
    for (i = 0; i < STAT_N; i++) {
        int m = lstrlenA(kStat[i].name);
        if (m == n - 1 && memcmp(s, kStat[i].name, m) == 0) return kStat[i].id;
    }
    return -1;
}
static int StatId(const char* s)
{
    int i;
    for (i = 0; i < STAT_N; i++) if (lstrcmpA(s, kStat[i].name) == 0) return kStat[i].id;
    return -1;
}

// JSON 키 <-> QF_*. 순서가 QF_* 번호와 같아야 한다.
static const char* kKey[QF_N] = {
    "year", "fame", "city", "building", "days",
    "advance", "reward", "fameGain", "item", "goodsFrom", "goods", "quantity",
    "pay",
};

const wchar_t* QJson_Path(void) { return g_path; }

QJFile* QJson_File(const wchar_t* name, int create)
{
    int i;
    for (i = 0; i < g_nf; i++) if (lstrcmpiW(g_f[i].file, name) == 0) return &g_f[i];
    if (!create || g_nf >= QJ_FILE_MAX) return NULL;
    memset(&g_f[g_nf], 0, sizeof(g_f[g_nf]));
    lstrcpynW(g_f[g_nf].file, name, 24);
    return &g_f[g_nf++];
}

void QJson_Remove(QJFile* f, int i)
{
    if (!f || i < 0 || i >= f->n) return;
    for (; i + 1 < f->n; i++) f->e[i] = f->e[i + 1];
    f->n--;
}

// ---- 읽기 ----

static void SkipWS(const char** pp)
{ while (**pp == ' ' || **pp == '\t' || **pp == '\r' || **pp == '\n') (*pp)++; }

static void SkipString(const char** pp)
{
    if (**pp != '"') return;
    (*pp)++;
    while (**pp && **pp != '"') { if (**pp == '\\' && (*pp)[1]) (*pp)++; (*pp)++; }
    if (**pp == '"') (*pp)++;
}

static void SkipValue(const char** pp)
{
    SkipWS(pp);
    if (**pp == '"') { SkipString(pp); return; }
    if (**pp == '{' || **pp == '[') {
        char open = **pp, close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (**pp) {
            if (**pp == '"') { SkipString(pp); continue; }
            if (**pp == open) depth++;
            else if (**pp == close) { depth--; (*pp)++; if (!depth) return; continue; }
            (*pp)++;
        }
        return;
    }
    while (**pp && **pp != ',' && **pp != '}' && **pp != ']') (*pp)++;
}

static void ReadString(const char** pp, char* out, int cap)
{
    int n = 0;
    out[0] = 0;
    if (**pp != '"') return;
    (*pp)++;
    while (**pp && **pp != '"') {
        char c = **pp;
        if (c == '\\' && (*pp)[1]) { (*pp)++; c = **pp; }
        if (n < cap - 1) out[n++] = c;
        (*pp)++;
    }
    if (**pp == '"') (*pp)++;
    out[n] = 0;
}

static int ReadInt(const char** pp, int* out)
{
    const char* p = *pp;
    int sign = 1, v = 0, digits = 0;
    SkipWS(&p);
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; digits++; }
    while (*p == '.' || (*p >= '0' && *p <= '9')) p++;   // 소수점은 버린다
    *pp = p;
    if (!digits) return 0;
    *out = sign * v;
    return 1;
}

// ["대사","글"] 같은 줄 하나. 문자열과 숫자를 순서대로 주워 담고 첫 낱말로 뜻을 정한다.
static int ParseLine(const char** pp)
{
    char s[4][80];
    int ns = 0, nums[QJ_ARG_MAX], nn = 0, i, mode;
    QJLine* l;

    if (**pp != '[') { SkipValue(pp); return 0; }
    (*pp)++;
    for (;;) {
        SkipWS(pp);
        if (**pp == ']') { (*pp)++; break; }
        if (!**pp) return 0;
        if (**pp == '"') {
            if (ns < 4) ReadString(pp, s[ns], sizeof(s[0])), ns++;
            else SkipValue(pp);
        } else {
            int v;
            if (ReadInt(pp, &v)) { if (nn < QJ_ARG_MAX) nums[nn++] = v; }
            else SkipValue(pp);
        }
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }
    if (ns == 0 || g_neline >= QJ_LINE_MAX) return 0;

    l = &g_eline[g_neline];
    memset(l, 0, sizeof(*l));
    for (i = 0; i < OP_N; i++) if (lstrcmpA(s[0], kOp[i].name) == 0) break;
    if (i == OP_N) {
        // "명성+" 처럼 능력치 이름에 부호를 붙여 쓴 형태
        int id = StatWord(s[0], &mode);
        if (id < 0 || nn < 1) return 0;
        l->op = QJ_OP_STAT; l->arg[0] = mode; l->arg[1] = id; l->arg[2] = nums[0];
        g_neline++; return 1;
    }
    l->op = kOp[i].op;
    switch (l->op) {
    case QJ_OP_TEXT:
        if (ns < 2) return 0;
        lstrcpynA(l->str, s[1], QJ_STR_MAX);
        l->arg[0] = (nn > 0) ? nums[0] : 0;               // 플래그. 없으면 00
        if (ns > 2) lstrcpynA(l->who, s[2], 32);          // 화자. 없으면 초상화 없이 나온다
        break;
    case QJ_OP_WHERE:
        if (nn < 1) return 0;
        l->arg[0] = kOp[i].a0; l->arg[1] = nums[0];
        break;
    case QJ_OP_YEAR:
    case QJ_OP_DAYS:
        if (nn < 1) return 0;
        l->arg[0] = nums[0];
        break;
    case QJ_OP_CMP:
        if (kOp[i].a0 >= 0) { if (nn < 1) return 0; l->arg[0] = kOp[i].a0; l->arg[1] = nums[0]; }
        else { if (ns < 2 || nn < 1) return 0; l->arg[0] = StatId(s[1]); l->arg[1] = nums[0];
               if (l->arg[0] < 0) return 0; }
        break;
    case QJ_OP_GOLD:
        if (nn < 1) return 0;
        l->arg[0] = kOp[i].a0; l->arg[1] = nums[0];
        break;
    case QJ_OP_IFITEM:
        // ["만약","아이템",id,"@라벨"] / ["만약","교역품",산지,품목,수량,"@라벨"]
        if (ns < 3) return 0;
        if (lstrcmpA(s[1], "교역품") == 0) {
            if (nn < 3) return 0;
            l->op = QJ_OP_IFGOODS;
            l->arg[0] = nums[0]; l->arg[1] = nums[1]; l->arg[2] = nums[2];
        } else {
            if (nn < 1) return 0;
            l->arg[0] = nums[0];
        }
        lstrcpynA(l->str, s[2], QJ_STR_MAX);              // 라벨
        break;
    case QJ_OP_JUMP:
    case QJ_OP_LABEL:
    case QJ_OP_RAW:
        if (ns < 2) return 0;
        lstrcpynA(l->str, s[1], QJ_STR_MAX);
        break;
    case QJ_OP_END:
        break;
    default: return 0;
    }
    g_neline++;
    return 1;
}

// "1:1:07" / "2:0:$" 를 뜯는다. 성공 1.
static int ParseAddr(const char* k, short* part, short* slot, short* idx)
{
    int v[3] = { -1, -1, -1 }, n = 0, i = 0;
    while (k[i] && n < 3) {
        if (k[i] == '$') { v[n++] = -1; i++; }
        else if (k[i] >= '0' && k[i] <= '9') {
            int x = 0;
            while (k[i] >= '0' && k[i] <= '9') x = x * 10 + (k[i++] - '0');
            v[n++] = x;
        } else i++;
        if (k[i] == ':') i++;
    }
    if (n < 3 || v[0] < 0 || v[1] < 0) return 0;
    *part = (short)v[0]; *slot = (short)v[1]; *idx = (short)v[2];
    return 1;
}

// "script": { "주소": 줄들, ... }
static void ParseScript(const char** pp, QJEntry* e)
{
    e->editFirst = (short)g_nedit;
    e->editCount = 0;
    if (**pp != '{') { SkipValue(pp); return; }
    (*pp)++;
    for (;;) {
        char key[32];
        QJEdit ed;
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') { if (!**pp) return; (*pp)++; continue; }
        ReadString(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);
        memset(&ed, 0, sizeof(ed));
        if (!ParseAddr(key, &ed.part, &ed.slot, &ed.idx) || **pp != '[' || g_nedit >= QJ_EDIT_MAX) {
            SkipValue(pp);
        } else {
            const char* q = *pp + 1;
            ed.first = (short)g_neline;
            SkipWS(&q);
            if (*q == '[') {                    // 줄 여러 개
                (*pp)++;
                for (;;) {
                    SkipWS(pp);
                    if (**pp == ']') { (*pp)++; break; }
                    if (!**pp) break;
                    if (**pp == '[') ParseLine(pp); else SkipValue(pp);
                    SkipWS(pp);
                    if (**pp == ',') { (*pp)++; continue; }
                }
            } else if (*q == ']') {             // [] = 그 줄 지우기
                SkipValue(pp);
            } else {                            // 줄 하나
                ParseLine(pp);
            }
            ed.count = (short)(g_neline - ed.first);
            g_edit[g_nedit++] = ed;
            e->editCount++;
        }
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }
}

static void ParseEntry(const char** pp, QJFile* f)
{
    QJEntry e;
    char key[32];
    int k;

    memset(&e, 0, sizeof(e));
    e.clone = -1; e.index = -1;

    if (**pp != '{') { SkipValue(pp); return; }
    (*pp)++;
    for (;;) {
        SkipWS(pp);
        if (**pp == '}') { (*pp)++; break; }
        if (**pp != '"') { if (!**pp) return; (*pp)++; continue; }
        ReadString(pp, key, sizeof(key));
        SkipWS(pp);
        if (**pp == ':') (*pp)++;
        SkipWS(pp);

        if (lstrcmpA(key, "index") == 0)      { int v; if (ReadInt(pp, &v)) e.index = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "clone") == 0) { int v; if (ReadInt(pp, &v)) e.clone = v; else SkipValue(pp); }
        else if (lstrcmpA(key, "script") == 0) ParseScript(pp, &e);
        else {
            for (k = 0; k < QF_N; k++) if (lstrcmpA(key, kKey[k]) == 0) break;
            if (k < QF_N) { int v; if (ReadInt(pp, &v)) { e.set[k] = 1; e.val[k] = v; } else SkipValue(pp); }
            else SkipValue(pp);
        }
        SkipWS(pp);
        if (**pp == ',') { (*pp)++; continue; }
    }
    if (e.clone < 0 && e.index < 0) return;       // 대상이 없는 항목은 버린다
    if (f->n < QJ_ENTRY_MAX) f->e[f->n++] = e;
}

static void JsonPath(HINSTANCE hinst)
{
    wchar_t* q; wchar_t* slash;
    GetModuleFileNameW(hinst, g_path, MAX_PATH);    // ...\CDS95Util\CharacterUtilKR.plugin
    slash = g_path;
    for (q = g_path; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    lstrcatW(g_path, L"quests.json");
}

void QJson_Load(HINSTANCE hinst)
{
    HANDLE h;
    DWORD sz, got = 0;
    char* buf;
    const char* p;

    g_nf = 0;
    JsonPath(hinst);

    h = CreateFileW(g_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;         // 없으면 빈 목록 — 원본 그대로 돈다
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 1u * 1024 * 1024) { CloseHandle(h); return; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!buf) { CloseHandle(h); return; }
    if (!ReadFile(h, buf, sz, &got, NULL)) { HeapFree(GetProcessHeap(), 0, buf); CloseHandle(h); return; }
    buf[got] = 0;
    CloseHandle(h);

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    SkipWS(&p);
    if (*p != '{') { HeapFree(GetProcessHeap(), 0, buf); return; }
    p++;
    for (;;) {
        char name[24];
        QJFile* f;
        SkipWS(&p);
        if (*p == '}' || !*p) break;
        if (*p != '"') { p++; continue; }
        ReadString(&p, name, sizeof(name));
        SkipWS(&p);
        if (*p == ':') p++;
        SkipWS(&p);
        // '_' 로 시작하는 키는 설명글이다. 읽지 않고 넘긴다(저장할 때 새로 써 준다).
        if (name[0] == '_') {
            SkipValue(&p);
            SkipWS(&p);
            if (*p == ',') { p++; continue; }
            continue;
        }
        {
            wchar_t wname[24];
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 24);
            f = QJson_File(wname, 1);
        }
        if (*p != '[' || !f) { SkipValue(&p); }
        else {
            p++;
            for (;;) {
                SkipWS(&p);
                if (*p == ']' || !*p) { if (*p) p++; break; }
                if (*p == '{') ParseEntry(&p, f);
                else SkipValue(&p);
                SkipWS(&p);
                if (*p == ',') { p++; continue; }
            }
        }
        SkipWS(&p);
        if (*p == ',') { p++; continue; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

// ---- 쓰기 ----

typedef struct { char* p; int len, cap; } Sb;

static void SbPut(Sb* s, const char* t)
{
    int n = lstrlenA(t);
    if (s->len + n >= s->cap) return;
    memcpy(s->p + s->len, t, n);
    s->len += n;
    s->p[s->len] = 0;
}
static void SbNum(Sb* s, const char* key, int v, int first)
{
    char t[64];
    wsprintfA(t, "%s\"%s\": %d", first ? "" : ", ", key, v);
    SbPut(s, t);
}

// JSON 문자열. 큰따옴표와 역슬래시만 막으면 된다(한글은 UTF-8 그대로 나간다).
static void SbStr(Sb* s, const char* t)
{
    char buf[QJ_STR_MAX * 2 + 4];
    int n = 0;
    buf[n++] = '"';
    for (; *t && n < (int)sizeof(buf) - 2; t++) {
        if (*t == '"' || *t == '\\') buf[n++] = '\\';
        buf[n++] = *t;
    }
    buf[n++] = '"'; buf[n] = 0;
    SbPut(s, buf);
}

static const char* StatNameOf(int id)
{
    int i;
    for (i = 0; i < STAT_N; i++) if (kStat[i].id == id) return kStat[i].name;
    return "?";
}

static void SbLine(Sb* s, const QJLine* l)
{
    char t[96];
    SbPut(s, "[");
    switch (l->op) {
    case QJ_OP_TEXT:
        SbPut(s, "\"대사\", "); SbStr(s, l->str);
        // 화자를 적으려면 플래그가 앞에 있어야 한다(자리로 가린다). 그래서 같이 낸다.
        if (l->arg[0] || l->who[0]) { wsprintfA(t, ", %d", l->arg[0]); SbPut(s, t); }
        if (l->who[0]) { SbPut(s, ", "); SbStr(s, l->who); }
        break;
    case QJ_OP_WHERE:
        SbPut(s, l->arg[0] == 0x00 ? "\"국가\", " : l->arg[0] == 0x08 ? "\"도시\", "
                : l->arg[0] == 0x10 ? "\"건물\", " : "\"지역\", ");
        wsprintfA(t, "%d", l->arg[1]); SbPut(s, t);
        break;
    case QJ_OP_YEAR: wsprintfA(t, "\"연도\", %d", l->arg[0]); SbPut(s, t); break;
    case QJ_OP_DAYS: wsprintfA(t, "\"기한\", %d", l->arg[0]); SbPut(s, t); break;
    case QJ_OP_CMP:
        if (l->arg[0] == 17) { wsprintfA(t, "\"명성조건\", %d", l->arg[1]); SbPut(s, t); }
        else { wsprintfA(t, "\"조건\", \"%s\", %d", StatNameOf(l->arg[0]), l->arg[1]); SbPut(s, t); }
        break;
    case QJ_OP_GOLD:
        wsprintfA(t, "\"금화%s\", %d", l->arg[0] ? "-" : "+", l->arg[1]); SbPut(s, t);
        break;
    case QJ_OP_STAT:
        wsprintfA(t, "\"%s%s\", %d", StatNameOf(l->arg[1]),
                  l->arg[0] == 0 ? "+" : (l->arg[0] == 1 ? "-" : "="), l->arg[2]);
        SbPut(s, t);
        break;
    case QJ_OP_IFITEM:
        wsprintfA(t, "\"만약\", \"아이템\", %d, ", l->arg[0]); SbPut(s, t); SbStr(s, l->str);
        break;
    case QJ_OP_IFGOODS:
        wsprintfA(t, "\"만약\", \"교역품\", %d, %d, %d, ", l->arg[0], l->arg[1], l->arg[2]);
        SbPut(s, t); SbStr(s, l->str);
        break;
    case QJ_OP_JUMP:  SbPut(s, "\"점프\", ");   SbStr(s, l->str); break;
    case QJ_OP_LABEL: SbPut(s, "\"라벨\", ");   SbStr(s, l->str); break;
    case QJ_OP_RAW:   SbPut(s, "\"생바이트\", "); SbStr(s, l->str); break;
    case QJ_OP_END:   SbPut(s, "\"끝\""); break;
    }
    SbPut(s, "]");
}

static void SbScript(Sb* s, const QJEntry* e)
{
    int k, j;
    if (e->editCount <= 0) return;
    SbPut(s, ",\n      \"script\": {\n");
    for (k = 0; k < e->editCount; k++) {
        const QJEdit* ed = &g_edit[e->editFirst + k];
        char t[64];
        if (ed->idx < 0) wsprintfA(t, "        \"%d:%d:$\": ", ed->part, ed->slot);
        else             wsprintfA(t, "        \"%d:%d:%02d\": ", ed->part, ed->slot, ed->idx);
        SbPut(s, t);
        if (ed->count == 1) SbLine(s, &g_eline[ed->first]);
        else {
            SbPut(s, "[");
            for (j = 0; j < ed->count; j++) {
                if (j) SbPut(s, ", ");
                SbLine(s, &g_eline[ed->first + j]);
            }
            SbPut(s, "]");
        }
        SbPut(s, k + 1 < e->editCount ? ",\n" : "\n");
    }
    SbPut(s, "      }");
}

int QJson_Save(HINSTANCE hinst)
{
    Sb s;
    int i, j, k, ok, wroteFile = 0;
    HANDLE h;
    DWORD put = 0;

    if (!g_path[0]) JsonPath(hinst);
    s.cap = 64 * 1024;
    s.p = (char*)HeapAlloc(GetProcessHeap(), 0, s.cap);
    if (!s.p) return 0;
    s.len = 0; s.p[0] = 0;

    // 파일을 열었을 때 형식을 바로 알 수 있게 설명을 늘 앞에 붙인다.
    // 읽을 때는 '_' 로 시작하는 키를 건너뛰므로 마음대로 고쳐도 된다.
    {
        static const char* kHelp[] = {
"퀘스트 이벤트 고치기. 원본은 <이름>.CDS.orig 로 남고, 게임이 읽는 .CDS 는",
"플러그인이 뜰 때마다 [원본 + 이 목록]으로 다시 만들어진다. 항목을 지우면 원래대로 돌아온다.",
"이벤트 파일을 밖에서 다른 것으로 갈아 끼우면(다른 통합수정판 등) 그것을 알아채고 새 파일을",
"원본으로 다시 잡는다. 전에 쓰던 원본은 <이름>.CDS.orig.old 로 남고, <이름>.CDS.stamp 는",
"그것을 가려내려고 두는 표식이다. 손으로 다시 잡으려면 <이름>.CDS.orig 를 지우면 된다.",
"",
"  { \"index\": 0, ... }   0번 퀘스트(인게임 목록의 1번)를 덮어쓴다",
"  { \"clone\": 0, ... }   0번을 통째로 본떠 파일 끝에 새 퀘스트로 붙인다",
"",
"파일 이름 칸(\"EHT.CDS\") 대신 \"*\" 칸을 쓰면 8직업 파일 전부에 같은 편집이 걸린다.",
"  \"*\": [ { \"index\": 11, \"script\": { \"11:0:08\": [\"대사\",\"글\",11,\"교회\"] } } ]",
"모드를 quests.json 하나로 배포할 때 쓰라고 둔 것이다. 다만 8개 파일의 파트 배치가",
"같아야 뜻이 있다 — 퀘스트패치는 8개가 바이트까지 같은 파일이라 되고, 바닐라는 파일마다",
"파트 수가 달라(17~23개) 주소가 안 맞는다. 한 퀘스트를 파일 칸과 \"*\" 칸이 둘 다 건드리면",
"파일 칸이 이긴다. 창에서 고친 값은 늘 파일 칸에 쌓인다.",
"",
"값 키 — year fame city building days advance reward pay fameGain item goodsFrom goods quantity",
"",
"script 는 줄 단위 편집이다. 주소 \"파트:슬롯:줄\" 은 인게임 [정보]>[퀘스트]>[대사] 탭에",
"찍히는 번호 그대로다. 값은 그 줄을 대신할 줄 목록이라 한 형태로 다 된다:",
"  \"2:0:04\": [\"대사\",\"바꿀 말\"]                한 줄로 바꾸기",
"  \"2:0:11\": [[\"대사\",\"가\"],[\"대사\",\"나\"]]     여러 줄로 늘리기",
"  \"2:0:06\": []                                그 줄 지우기",
"  \"2:0:$\":  [[\"대사\",\"덧붙임\"]]                슬롯 끝에 덧붙이기",
"주소는 늘 원본 기준이라 줄을 넣고 빼도 다른 주소가 밀리지 않는다.",
"어느 퀘스트가 고쳐지는지는 index 가 아니라 주소의 파트 번호가 정한다. 그러니 index 는",
"아무 퀘스트나 가리켜도 script 는 제대로 먹는다(값 키를 같이 쓸 때만 index 가 대상이 된다).",
"참고로 목록 번호는 1부터, index 는 0부터다 — 목록 22번은 index 21 이다.",
"",
"쓸 수 있는 줄:",
"  [\"대사\",\"글\"]                       대사 (cp949 로 바뀌어 들어간다)",
"  [\"대사\",\"글\",플래그,\"화자\"]          셋째·넷째는 없어도 된다. 자세한 것은 아래.",
"  [\"도시\",7] [\"건물\",9] [\"지역\",n] [\"국가\",n]",
"  [\"연도\",1500]                        연도 조건",
"  [\"명성조건\",2000]  [\"조건\",\"악명\",5]",
"  [\"금화+\",10000]  [\"금화-\",1000]",
"  [\"명성+\",240]  [\"악명+\",80]  [\"매력=\",2]   (악명 무력 명성 운 지력 매력)",
"  [\"기한\",183]                         기한(일)",
"  [\"만약\",\"아이템\",41,\"@라벨\"]          아이템 있으면 그 라벨로 뛴다",
"  [\"만약\",\"교역품\",30,39,50,\"@라벨\"]     산지·품목·수량",
"  [\"점프\",\"@라벨\"]   [\"라벨\",\"@이름\"]",
"  [\"생바이트\",\"06 4D\"]                 뜻 모르는 바이트 그대로",
"  [\"끝\"]",
"",
"대사의 플래그 — 그 대사가 어떤 창으로 뜨는가. 빼면 0 이다.",
"  0    확인 버튼 하나                     (원본 3849줄)",
"  11   예/아니오                          (206줄) 바로 뒤의 43 47 분기가 답을 받는다",
"  16   여러 개 중에 고르기                 (87줄) 본문을 \"／\" 로 나눠 적고 뒤의 43 11 분기가 받는다",
"       [\"대사\",\"교섭한다／침입한다\",16]",
"  32   술집·여관 손님의 소문               (56줄) 뒤에 19 <지역> 이 붙어 어느 지역에 뿌릴지 정한다",
"예/아니오를 확인 하나로 바꿔 버리는 사고가 흔하다 — 원본이 11 이면 11 을 그대로 적어야 한다.",
"",
"대사의 화자 — 초상화를 정한다. 비우면 초상화 없이 글만 나온다.",
"  한글로 적으면 된다:",
"    교회 조합 부관 교역소 술집 여관 성문 집사 조선소 병사 딸 왕녀 주인공",
"    마누엘1세 카를로스1세 조안2세 레오10세 가정제 야코프푸거 미켈레스피놀라",
"    우르그백 조안바로스 누진가누쿠우 맘루크 예니체리",
"  원본에는 cp932 일본어(教会 ギルド …)로 박혀 있는데 플러그인이 짝을 지어 바꿔 넣는다.",
"  표에 없는 이름은 \"教会\" 처럼 일본어로, 또는 \"8B B3 89 EF\" 처럼 16진수로 적어도 된다.",
"",
"  보기:  \"11:0:08\": [\"대사\",\"교회에 기부하시겠습니까?\",11,\"교회\"]",
"  둘 다 [대사] 탭에 \"·화자 교회 ·플래그 11\" 로 찍히니 보고 그대로 옮겨 적으면 된다.",
"",
"주의 — 인게임에서 ?? 로 나오는 바이트(06 4D 58 …)는 뜻을 아직 모른다. 지우면 이벤트가",
"안 끝나거나 진행이 멈출 수 있으니 그대로 두는 편이 낫다. 고친 것은 게임을 껐다 켜야 반영된다.",
        };
        int hn = (int)(sizeof(kHelp)/sizeof(kHelp[0])), q;
        SbPut(&s, "{\n  \"_읽어보기\": [\n");
        for (q = 0; q < hn; q++) {
            SbPut(&s, "    ");
            SbStr(&s, kHelp[q]);
            SbPut(&s, q + 1 < hn ? ",\n" : "\n");
        }
        SbPut(&s, "  ]");
        wroteFile = 1;
    }
    for (i = 0; i < g_nf; i++) {
        char name[32];
        WideCharToMultiByte(CP_UTF8, 0, g_f[i].file, -1, name, sizeof(name), NULL, NULL);
        if (wroteFile) SbPut(&s, ",\n");
        // 목록이 비어도 칸은 남겨 둔다 — 어디에 써 넣어야 하는지 보이라고.
        if (g_f[i].n == 0) { SbPut(&s, "  \""); SbPut(&s, name); SbPut(&s, "\": []"); continue; }
        SbPut(&s, "  \""); SbPut(&s, name); SbPut(&s, "\": [\n");
        for (j = 0; j < g_f[i].n; j++) {
            const QJEntry* e = &g_f[i].e[j];
            SbPut(&s, "    { ");
            if (e->clone >= 0) SbNum(&s, "clone", e->clone, 1);
            else               SbNum(&s, "index", e->index, 1);
            for (k = 0; k < QF_N; k++) if (e->set[k]) SbNum(&s, kKey[k], e->val[k], 0);
            SbScript(&s, e);
            SbPut(&s, j + 1 < g_f[i].n ? " },\n" : " }\n");
        }
        SbPut(&s, "  ]");
        wroteFile = 1;
    }
    SbPut(&s, "\n}\n");

    h = CreateFileW(g_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, s.p); return 0; }
    ok = WriteFile(h, s.p, (DWORD)s.len, &put, NULL) && put == (DWORD)s.len;
    CloseHandle(h);
    HeapFree(GetProcessHeap(), 0, s.p);
    return ok;
}
