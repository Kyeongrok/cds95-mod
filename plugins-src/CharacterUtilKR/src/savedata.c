#include "savedata.h"
#include "chardb.h"
#include "cities_data.h"   // TradeUtilKR/src 의 kCities[226] (CMake include 경로로 잡아둠)

#define CHAR_START  0x924A
#define CHAR_SIZE   0x90
#define CHAR_COUNT  461

#define OFF_HP      0x00
#define OFF_INT     0x01
#define OFF_STR     0x02
#define OFF_CHM     0x03
#define OFF_LUK     0x04
#define OFF_SKILL0  0x0A   // +0x0A+k = 특기 k
#define OFF_FAME    0x26
#define OFF_LOC     0x2E
#define OFF_BLDG    0x30
#define OFF_NAME1   0x32
#define OFF_NAME2   0x45
#define OFF_FACE    0x58   // u16 LE. 0xFFFF = 얼굴 없음(몬스터 등)
#define OFF_AGE     0x5C
#define OFF_ZODIAC  0x60
#define OFF_HIRE    0x62
#define NAME1_LEN   20
#define NAME2_LEN   19     // 0x45~0x57. 0x58 부터는 얼굴코드다

#define PLAYER_FAME 0x53
#define PLAYER_CITY 0x57
#define SAVE_YEAR   0x15
#define SAVE_MONTH  0x19
#define SAVE_DAY    0x1A

static const wchar_t* kSkillName[SAVE_SKILL_MAX + 1] = {
    L"",
    L"항해술", L"운용술", L"검술", L"포술", L"사격술", L"의학", L"웅변술",
    L"측량술", L"역사학", L"회계", L"조선술", L"신학", L"과학",
    L"스페인어", L"포르투갈어", L"로망스어", L"게르만어", L"슬라브어",
    L"아랍어", L"페르시아어", L"중국어", L"힌두어", L"위그르어",
    L"아프리카어", L"아메리카어", L"동남아시아어", L"동아시아어",
};

static const wchar_t* kSkillShort[SAVE_SKILL_MAX + 1] = {
    L"",
    L"항", L"운", L"검", L"포", L"사", L"의", L"웅",
    L"측", L"역", L"회", L"조", L"신", L"과",
    L"스", L"갈", L"로", L"게", L"슬",
    L"랍", L"페", L"중", L"힌", L"위",
    L"아", L"미", L"남", L"동",
};

const wchar_t* Save_SkillName(int id)
{
    return (id >= 1 && id <= SAVE_SKILL_MAX) ? kSkillName[id] : L"";
}
const wchar_t* Save_SkillShort(int id)
{
    return (id >= 1 && id <= SAVE_SKILL_MAX) ? kSkillShort[id] : L"";
}

const wchar_t* Save_CityName(unsigned char id)
{
    if (id == 255) return L"함대소속";
    if (id < (unsigned char)(sizeof(kCities)/sizeof(kCities[0]))) return kCities[id].name;
    return L"?";
}

const wchar_t* Save_BuildingName(unsigned char b)
{
    switch (b) {
    case 4:  return L"주점";
    case 5:  return L"여관";
    case 255:return L"";
    default: return L"";
    }
}

// cp949 고정길이 필드에서 문자열을 꺼낸다. 널 종료가 없을 수도 있어 길이로도 자른다.
static void ReadStr(const unsigned char* p, int len, wchar_t* out, int cap)
{
    char buf[64];
    int n = 0, w;
    out[0] = 0;
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    while (n < len && p[n] != 0) { buf[n] = (char)p[n]; n++; }
    buf[n] = 0;
    if (n == 0) return;

    // 949 = cp949(한국어). 게임 원본이 cp932 인 판본이라도 여기서는 한글 통합수정판을 전제한다.
    // 길이를 넘겨 부르면 MultiByteToWideChar 는 널을 붙여주지 않는다. 반환값 자리에 직접 붙여야
    // 하는데, 이걸 빼먹으면 뒤의 초기화 안 된 스택이 이름 꼬리로 딸려 들어온다.
    w = MultiByteToWideChar(949, 0, buf, n, out, cap - 1);
    if (w <= 0) { out[0] = 0; return; }
    if (w > cap - 1) w = cap - 1;
    out[w] = 0;

    while (w > 0 && out[w-1] == L' ') out[--w] = 0;
}

static unsigned char* ReadWholeFile(const wchar_t* path, DWORD* sizeOut)
{
    HANDLE h;
    DWORD sz, got = 0;
    unsigned char* buf;

    // 게임이 세이브를 열어둔 채일 수 있으므로 공유 플래그를 넉넉히 준다.
    h = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz < CHAR_START) { CloseHandle(h); return NULL; }

    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return NULL; }

    if (!ReadFile(h, buf, sz, &got, NULL) || got != sz) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    *sizeOut = sz;
    return buf;
}

static void SavePath(wchar_t* out)
{
    wchar_t exe[MAX_PATH];
    wchar_t* p; wchar_t* last;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    last = exe;
    for (p = exe; *p; p++) if (*p == L'\\' || *p == L'/') last = p;
    *last = 0;
    wsprintfW(out, L"%s\\SAVEDATA.CDS", exe);
}

// 빈 슬롯/쓰레기 레코드 걸러내기.
// 미사용 슬롯은 0xFF 로 채워져 있는데 그 20바이트를 cp949 로 풀면 이름이 "있는 것처럼"
// 보여서, 이름만으로 판정하면 30여 개가 목록에 섞여 들어온다.
static int LooksValid(const unsigned char* r)
{
    if (r[OFF_HP] == 0xFF) return 0;
    if (r[OFF_HIRE] > 3) return 0;                        // 고용상태는 0~3 만 나온다
    if (r[OFF_BLDG] != 255 && r[OFF_BLDG] > 5) return 0;  // 건물은 0~5 / 255
    return 1;
}

// 얼굴코드는 +0x58 에 그대로 들어 있다(u16 LE). 다만 남/여 얼굴 파일을 구분하는 플래그는
// 레코드 어디에도 없어서(비트 단위로 전수 조사해도 남/여를 가르는 자리가 없음),
// 그 코드 자리의 이름표가 어느 쪽과 맞는지로 성별을 가린다.
// 이름 표기가 흔들리는 인물(예: 세이브 "크리스트발" vs 표 "크르스트발")은 이름이 안 맞으므로
// 표에 항목이 있는 쪽을 택한다 — 실제로 이 게임의 인물 얼굴은 대부분 남자 표에 있다.
static void ResolveFace(SaveChar* c, unsigned short raw)
{
    c->faceGender = -1;
    c->faceCode   = -1;
    if (raw == 0xFFFF) return;                          // 얼굴 없는 레코드(해수·이벤트 등)
    if (raw >= (unsigned short)CharDb_Count(CHARDB_MALE)) return;

    if      (CharDb_NameMatches(CHARDB_MALE,   raw, c->name)) c->faceGender = CHARDB_MALE;
    else if (CharDb_NameMatches(CHARDB_FEMALE, raw, c->name)) c->faceGender = CHARDB_FEMALE;
    else if (CharDb_Name(CHARDB_MALE,   raw)[0])              c->faceGender = CHARDB_MALE;
    else if (CharDb_Name(CHARDB_FEMALE, raw)[0])              c->faceGender = CHARDB_FEMALE;
    else                                                      c->faceGender = CHARDB_MALE;
    c->faceCode = raw;
}

void Save_Free(SaveData* s)
{
    if (s->chars) { HeapFree(GetProcessHeap(), 0, s->chars); s->chars = NULL; }
    s->count = 0;
    s->loaded = 0;
}

int Save_Load(SaveData* s)
{
    unsigned char* d;
    DWORD sz = 0;
    int i, k, n = 0;
    SaveChar* arr;

    Save_Free(s);
    SavePath(s->path);
    d = ReadWholeFile(s->path, &sz);
    if (!d) return 0;

    arr = (SaveChar*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SaveChar) * CHAR_COUNT);
    if (!arr) { HeapFree(GetProcessHeap(), 0, d); return 0; }

    if (sz > SAVE_DAY) {
        s->year  = (unsigned short)(d[SAVE_YEAR] | (d[SAVE_YEAR+1] << 8));
        s->month = d[SAVE_MONTH];
        s->day   = d[SAVE_DAY];
    }
    if (sz > PLAYER_CITY) {
        s->playerFame = (unsigned short)(d[PLAYER_FAME] | (d[PLAYER_FAME+1] << 8));
        s->playerCity = d[PLAYER_CITY];
    }

    for (i = 0; i < CHAR_COUNT; i++) {
        const unsigned char* r = d + CHAR_START + i * CHAR_SIZE;
        SaveChar* c;
        wchar_t n1[32], n2[32];

        if ((DWORD)(CHAR_START + (i + 1) * CHAR_SIZE) > sz) break;
        if (!LooksValid(r)) continue;

        ReadStr(r + OFF_NAME1, NAME1_LEN, n1, 32);
        ReadStr(r + OFF_NAME2, NAME2_LEN, n2, 32);
        if (!n1[0] && !n2[0]) continue;

        c = &arr[n];
        c->index = i;
        // n1/n2 는 각각 최대 31자라 "n1·n2" 가 name[64] 에 딱 맞아떨어진다(여유 0).
        // 넉넉한 버퍼에 만든 뒤 잘라 담아 그 아슬아슬함을 없앤다.
        {
            wchar_t full[80];
            if (n1[0] && n2[0]) wsprintfW(full, L"%s·%s", n1, n2);
            else                lstrcpynW(full, n1[0] ? n1 : n2, 80);
            lstrcpynW(c->name, full, (int)(sizeof(c->name)/sizeof(c->name[0])));
        }

        c->hp    = r[OFF_HP];
        c->intel = r[OFF_INT];
        c->str   = r[OFF_STR];
        c->chm   = r[OFF_CHM];
        c->luk   = r[OFF_LUK];
        for (k = 1; k <= SAVE_SKILL_MAX; k++) c->skill[k] = r[OFF_SKILL0 + k];

        c->fame   = (unsigned short)(r[OFF_FAME] | (r[OFF_FAME+1] << 8));
        c->loc    = r[OFF_LOC];
        c->bldg   = r[OFF_BLDG];
        c->age    = (signed char)r[OFF_AGE];
        c->zodiac = r[OFF_ZODIAC];
        c->hire   = r[OFF_HIRE];

        ResolveFace(c, (unsigned short)(r[OFF_FACE] | (r[OFF_FACE+1] << 8)));

        n++;
    }

    HeapFree(GetProcessHeap(), 0, d);
    s->chars  = arr;
    s->count  = n;
    s->loaded = 1;
    return 1;
}
