#include "itemdb.h"

// 표를 믿기 전에 관문 다섯을 다 지난다. 하나라도 걸리면 Ready 가 0 이고,
// 부르는 쪽은 이름만 보여주는 쪽으로 내려앉는다. 여기서 쓰는 검사 방식은
// maids.c(모듈 범위 · 줄마다 검사 · 다음 줄은 반드시 걸려야 함)와 같다.

static ItemRec              g_rec[ITEMDB_N];
static const unsigned char* g_base = NULL;
static SIZE_T               g_imgSize = 0;
static const unsigned*      g_desc = NULL;   // 설명문 포인터표
static int                  g_ready = 0;
static int                  g_status = ITEMDB_E_MODULE;
static wchar_t              g_descBuf[256];

static const wchar_t* kCat[ITEMDB_CAT_N] = {
    L"발명품", L"선물·보석", L"항해도구", L"무기", L"방어구",
    L"교역품·미술품", L"조각상", L"서적·유물", L"동물"
};

int ItemDb_Ready(void)  { return g_ready; }
int ItemDb_Status(void) { return g_status; }
void ItemDb_Reset(void) { if (!g_ready) g_status = ITEMDB_E_MODULE; }

const wchar_t* ItemDb_CatName(int cat)
{
    return (cat >= 0 && cat < ITEMDB_CAT_N) ? kCat[cat] : L"?";
}

const ItemRec* ItemDb_At(int id)
{
    if (!g_ready || id < 0 || id >= ITEMDB_N) return NULL;
    return &g_rec[id];
}

// ------------------------------------------------------------------ 모듈 훑기

static const IMAGE_NT_HEADERS32* NtHeaders(void)
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS32* nt;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (const IMAGE_NT_HEADERS32*)((const unsigned char*)dos + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    g_base    = (const unsigned char*)dos;
    g_imgSize = nt->OptionalHeader.SizeOfImage;
    return g_imgSize > 0 ? nt : NULL;
}

// 파일 오프셋이 속한 섹션을 찾아 로드된 자리로 옮긴다(PatchUtilKR 의 OffToMem 과 같다).
static const unsigned char* OffToMem(const IMAGE_NT_HEADERS32* nt, unsigned off)
{
    const IMAGE_SECTION_HEADER* s;
    int n, i;
    if (!nt) return NULL;
    if (off < nt->OptionalHeader.SizeOfHeaders) return g_base + off;   // 머리 영역은 오프셋 == RVA
    s = IMAGE_FIRST_SECTION(nt);
    n = nt->FileHeader.NumberOfSections;
    for (i = 0; i < n; i++) {
        DWORD rp = s[i].PointerToRawData;
        DWORD rs = s[i].SizeOfRawData;
        if (rs && off >= rp && off < rp + rs)
            return g_base + s[i].VirtualAddress + (off - rp);
    }
    return NULL;
}

static int Readable(const void* p, SIZE_T n)
{
    const unsigned char* q   = (const unsigned char*)p;
    const unsigned char* end = q + n;
    while (q < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(q, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        q = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

static int InModule(unsigned addr)
{
    return addr >= (unsigned)(UINT_PTR)g_base
        && addr <  (unsigned)(UINT_PTR)g_base + (unsigned)g_imgSize;
}

// 이 28바이트가 아이템 레코드로 말이 되는지. 286줄이 전부 통과하고
// 287번째 줄은 반드시 걸려야 표를 믿는다(길이를 못 박는 관문).
static int RowOk(const unsigned char* r, const unsigned* desc)
{
    int pic = *(const int*)(r + 0x04);
    int cat = *(const int*)(r + 0x14);
    int a   = *(const int*)(r + 0x08);
    int b   = *(const int*)(r + 0x0C);
    if (!InModule(*(const unsigned*)(r + 0x00))) return 0;   // 이름 포인터
    if (pic < -1 || pic > ITEMDB_PIC_MAX) return 0;
    if (cat < 0 || cat >= ITEMDB_CAT_N) return 0;
    if (a < 0 || a > 10000000) return 0;
    if (b < 0 || b > 10000000) return 0;
    if (desc) {
        if (!InModule(*desc)) return 0;
        if (*(const char*)(UINT_PTR)(*desc) == 0) return 0;  // 286개 전부 비어 있지 않다
    }
    return 1;
}

int ItemDb_Load(void)
{
    const IMAGE_NT_HEADERS32* nt;
    const unsigned char* tbl;
    const unsigned* desc;
    int i;

    if (g_ready) return 1;

    nt = NtHeaders();
    if (!nt) { g_status = ITEMDB_E_MODULE; return 0; }

    tbl  = OffToMem(nt, ITEMDB_REC_OFF);
    desc = (const unsigned*)OffToMem(nt, ITEMDB_DESC_OFF);
    if (!tbl || !desc) { g_status = ITEMDB_E_MODULE; return 0; }

    // 287개분을 본다 — 마지막 한 줄은 "걸려야" 하므로 그것도 읽을 수 있어야 한다.
    if (!Readable(tbl, (ITEMDB_N + 1) * ITEMDB_REC_SZ) ||
        !Readable(desc, (ITEMDB_N + 1) * sizeof(unsigned))) { g_status = ITEMDB_E_READ; return 0; }

    for (i = 0; i < ITEMDB_N; i++)
        if (!RowOk(tbl + i * ITEMDB_REC_SZ, desc + i)) { g_status = ITEMDB_E_TABLE; return 0; }
    if (RowOk(tbl + ITEMDB_N * ITEMDB_REC_SZ, NULL)) { g_status = ITEMDB_E_TABLE; return 0; }

    for (i = 0; i < ITEMDB_N; i++) {
        const unsigned char* r = tbl + i * ITEMDB_REC_SZ;
        g_rec[i].pic  = *(const int*)(r + 0x04);
        g_rec[i].valA = *(const int*)(r + 0x08);
        g_rec[i].valB = *(const int*)(r + 0x0C);
        g_rec[i].arg  = *(const int*)(r + 0x10);
        g_rec[i].cat  = *(const int*)(r + 0x14);
        g_rec[i].misc = *(const int*)(r + 0x18);
    }
    g_desc = desc;
    g_ready = 1;
    g_status = ITEMDB_OK;
    return 1;
}

// 설명문은 화면에 한 번에 하나만 뜨므로 그때그때 옮긴다(286개를 미리 다 옮기면 헛되다).
// 널 · 버퍼 상한 · 모듈 끝 셋으로 끊어 읽는다 — strlen 은 쓰지 않는다.
const wchar_t* ItemDb_Desc(int id)
{
    const char* s;
    const char* lim;
    char buf[512];
    int n = 0, w;

    if (!g_ready || id < 0 || id >= ITEMDB_N) return NULL;
    s = (const char*)(UINT_PTR)g_desc[id];
    lim = (const char*)(g_base + g_imgSize);
    while (n < (int)sizeof(buf) - 1 && s + n < lim && s[n]) { buf[n] = s[n]; n++; }
    buf[n] = 0;
    if (n == 0) return NULL;

    // 949 = cp949. maids.c / savedata.c 의 이름 읽기와 같은 전제(한글 통합수정판)다.
    w = MultiByteToWideChar(949, 0, buf, n, g_descBuf,
                            (int)(sizeof(g_descBuf)/sizeof(g_descBuf[0])) - 1);
    if (w <= 0) return NULL;
    if (w > (int)(sizeof(g_descBuf)/sizeof(g_descBuf[0])) - 1)
        w = (int)(sizeof(g_descBuf)/sizeof(g_descBuf[0])) - 1;
    g_descBuf[w] = 0;
    return g_descBuf;
}
