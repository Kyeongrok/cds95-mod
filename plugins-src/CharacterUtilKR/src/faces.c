#include "faces.h"
#include "ls12.h"
#include "ui.h"
#include "imgio.h"
#include "game_palette.h"   // kGamePalette[768] — 아이템 그림에서 되짚은 게임 공용 색표

static Ls12File g_male, g_female;
static int      g_loaded = 0;
static unsigned char g_idx[LS12_FACE_SZ];

static Ls12File* FileOf(int gender)
{
    return gender == FACE_FEMALE ? &g_female : &g_male;
}

// 게임 폴더의 MALE.CDS / FEMALE.CDS 경로. 게임 폴더 이름에 한글이 들어가므로
// 파일을 쓸 때는 이 와이드 경로를 쓴다(Ls12_Open 은 예전부터 ANSI 를 쓴다).
static void ArchivePathW(int gender, wchar_t* out, int cch)
{
    wchar_t exe[MAX_PATH];
    wchar_t* p;
    wchar_t* last;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    last = exe;
    for (p = exe; *p; p++) if (*p == L'\\' || *p == L'/') last = p;
    *last = 0;
    wsprintfW(out, L"%s\\%s.CDS", exe, gender == FACE_FEMALE ? L"FEMALE" : L"MALE");
    (void)cch;
}

void Face_Load(void)
{
    char exe[MAX_PATH], dir[MAX_PATH], path[MAX_PATH]; char* p;
    if (g_loaded) return;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    lstrcpynA(dir, exe, MAX_PATH);
    p = dir; { char* last = dir; while (*p) { if (*p=='\\'||*p=='/') last = p; p++; } *last = 0; }
    wsprintfA(path, "%s\\MALE.CDS", dir);   Ls12_Open(&g_male, path);
    wsprintfA(path, "%s\\FEMALE.CDS", dir); Ls12_Open(&g_female, path);
    g_loaded = 1;
}

void Face_Unload(void)
{
    if (!g_loaded) return;
    Ls12_Close(&g_male);
    Ls12_Close(&g_female);
    g_loaded = 0;
}

int Face_Count(int gender)
{
    if (!g_loaded) return 0;
    return FileOf(gender)->count;
}

void Face_Draw(HDC dc, int x, int y, int w, int h, int gender, int code)
{
    static unsigned char rgb[LS12_FACE_SZ * 3];
    BITMAPINFO bi;
    RECT box;
    Ls12File* f;
    int i, ok = 0;

    box.left = x - 1; box.top = y - 1; box.right = x + w + 1; box.bottom = y + h + 1;

    if (g_loaded && gender >= 0 && code >= 0) {
        f = FileOf(gender);
        if (code < f->count && Ls12_DecodeFace(f, code, g_idx)) ok = 1;
    }

    if (!ok) {
        // 얼굴을 못 찾은 인물(이름 역추적 실패 등)은 빈 액자로 둔다.
        RECT in; HBRUSH br;
        in.left = x; in.top = y; in.right = x + w; in.bottom = y + h;
        br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &in, br); DeleteObject(br);
        UI_Bevel(dc, in, TRUE);
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br);
        return;
    }

    for (i = 0; i < LS12_FACE_SZ; i++) {
        unsigned char v = g_idx[i];
        rgb[i*3+0] = kGamePalette[v*3+2];
        rgb[i*3+1] = kGamePalette[v*3+1];
        rgb[i*3+2] = kGamePalette[v*3+0];
    }
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = LS12_FACE_W;
    bi.bmiHeader.biHeight = -LS12_FACE_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, LS12_FACE_W, LS12_FACE_H, rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    { HBRUSH br = CreateSolidBrush(COL_DARK); FrameRect(dc, &box, br); DeleteObject(br); }
}

// ================================================================== 내보내기 / 갈아 끼우기

// 얼굴이 실제로 쓰는 팔레트 자리. MALE/FEMALE.CDS 의 파트를 전수로 확인한 값이다
// (남 0·10~73, 여 10~74). 0~9 는 게임이 딴 데 쓰는 자리라 얼굴에 섞이면 안 된다 —
// 실제로 7680점 중 52점만 0번으로 넘어갔는데 게임 초상화가 통째로 안 나왔다.
// 위는 73 에서 끊는다. 74 는 여자 얼굴 144장을 통틀어 106점뿐이라 공용 색표를 되짚을 때
// 색이 안 잡힌 자리다(채움값이 들어 있다) — 후보로 두면 어두운 점이 그리로 몰린다.
#define FACE_IDX_MIN 10
#define FACE_IDX_MAX 73

// RGB 한 점을 팔레트에서 가장 가까운 인덱스로. 얼굴이 쓰는 자리만 후보로 둔다.
static unsigned char NearestIndex(int r, int g, int b)
{
    int best = FACE_IDX_MIN, bestd = 0x7FFFFFFF, i;
    for (i = FACE_IDX_MIN; i <= FACE_IDX_MAX; i++) {
        int dr = r - kGamePalette[i*3+0];
        int dg = g - kGamePalette[i*3+1];
        int db = b - kGamePalette[i*3+2];
        int d  = dr*dr + dg*dg + db*db;
        if (d < bestd) { bestd = d; best = i; if (!d) break; }
    }
    return (unsigned char)best;
}

int Face_ExportPng(int gender, int code, const wchar_t* path)
{
    static unsigned char rgb[LS12_FACE_SZ * 3];
    unsigned char idx[LS12_FACE_SZ];
    int i;

    if (!Img_Available()) return FACE_ERR_GDIP;
    Face_Load();
    if (!g_loaded || FileOf(gender)->count <= 0) return FACE_ERR_ARCHIVE;
    if (code < 0 || code >= FileOf(gender)->count) return FACE_ERR_RANGE;
    if (!Ls12_DecodeFace(FileOf(gender), code, idx)) return FACE_ERR_IMAGE;

    for (i = 0; i < LS12_FACE_SZ; i++) {
        unsigned char v = idx[i];
        rgb[i*3+0] = kGamePalette[v*3+0];
        rgb[i*3+1] = kGamePalette[v*3+1];
        rgb[i*3+2] = kGamePalette[v*3+2];
    }
    return Img_SavePng(path, LS12_FACE_W, LS12_FACE_H, rgb) ? FACE_ERR_OK : FACE_ERR_WRITE;
}

// 원본을 <이름>.CDS.orig 로 딱 한 번 남긴다. 이미 있으면 그게 진짜 원본이므로 건드리지 않는다.
static void BackupOnce(const wchar_t* path)
{
    wchar_t orig[MAX_PATH];
    lstrcpynW(orig, path, MAX_PATH);
    lstrcatW(orig, L".orig");
    if (GetFileAttributesW(orig) == INVALID_FILE_ATTRIBUTES)
        CopyFileW(path, orig, TRUE);
}

static int WriteWhole(const wchar_t* path, const unsigned char* buf, unsigned len)
{
    HANDLE f;
    DWORD wr = 0;
    BOOL ok;
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, buf, len, &wr, NULL) && wr == len;
    CloseHandle(f);
    return ok ? 1 : 0;
}

// idx8(80x96) 을 그 표의 index 자리에 써넣는다. index < 0 이면 끝에 붙인다.
static int WritePart(int gender, int index, const unsigned char* idx8, int* newCode)
{
    Ls12File* f = FileOf(gender);
    wchar_t path[MAX_PATH];
    unsigned char* buf;
    unsigned cap, len;
    int rc = FACE_ERR_OK;

    if (!g_loaded || f->count <= 0) return FACE_ERR_ARCHIVE;
    if (index >= f->count) return FACE_ERR_RANGE;

    cap = Ls12_RewriteCap(f, (unsigned)LS12_FACE_SZ);
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, cap);
    if (!buf) return FACE_ERR_ENCODE;

    len = Ls12_Rewrite(f, index, idx8, (unsigned)LS12_FACE_SZ, buf, cap);
    if (!len) rc = FACE_ERR_ENCODE;
    // 덮어쓰기 전에 도로 풀어 본다. 여기서 걸리면 파일은 손도 안 댄 채로 끝난다.
    else if (!Ls12_VerifyPart(buf, len, index, idx8, (unsigned)LS12_FACE_SZ)) rc = FACE_ERR_VERIFY;

    if (rc == FACE_ERR_OK) {
        if (newCode) *newCode = (index < 0) ? f->count : index;
        ArchivePathW(gender, path, MAX_PATH);
        BackupOnce(path);
        if (!WriteWhole(path, buf, len)) rc = FACE_ERR_WRITE;
    }
    HeapFree(GetProcessHeap(), 0, buf);

    if (rc == FACE_ERR_OK) {
        Face_Unload();       // 새 파일로 다시 연다 — 창에 바로 보이게
        Face_Load();
    }
    return rc;
}

static int ImportInto(int gender, int index, const wchar_t* path, int* newCode)
{
    static unsigned char rgb[LS12_FACE_SZ * 3];
    unsigned char idx[LS12_FACE_SZ];
    int i;

    if (!Img_Available()) return FACE_ERR_GDIP;
    Face_Load();
    if (!g_loaded || FileOf(gender)->count <= 0) return FACE_ERR_ARCHIVE;

    // 투명한 PNG 를 넣어도 액자처럼 보이게 바탕을 미리 깐다 — 얼굴이 쓰는 첫 색(FACE_IDX_MIN,
    // 밝은 아이보리)이다. 색인으로 되돌릴 때 그대로 그 자리에 떨어지므로 얼룩이 안 생긴다.
    if (!Img_LoadScaled(path, LS12_FACE_W, LS12_FACE_H,
                        kGamePalette[FACE_IDX_MIN*3+0],
                        kGamePalette[FACE_IDX_MIN*3+1],
                        kGamePalette[FACE_IDX_MIN*3+2], rgb))
        return FACE_ERR_IMAGE;

    for (i = 0; i < LS12_FACE_SZ; i++)
        idx[i] = NearestIndex(rgb[i*3+0], rgb[i*3+1], rgb[i*3+2]);

    return WritePart(gender, index, idx, newCode);
}

int Face_ImportPng(int gender, int code, const wchar_t* path)
{
    if (code < 0) return FACE_ERR_RANGE;
    return ImportInto(gender, code, path, NULL);
}

int Face_AppendPng(int gender, const wchar_t* path, int* newCode)
{
    return ImportInto(gender, -1, path, newCode);
}
