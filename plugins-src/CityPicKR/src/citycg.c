#include "citycg.h"
#include "ls12.h"
#include "imgio.h"          // PNG 읽기/쓰기(GDI+ 를 실행 중에 부른다)
#include "quant.h"          // 24bpp 를 색인 + 86색 팔레트로 되돌리기
#include "game_palette.h"   // kGamePalette[768] — 낮은 색인의 공용 색표

// itempic.c 와 같은 모양이다 — 아카이브 하나를 열어 두고, 한 장을 풀어 24bpp DIB 로 찍는다.
// 다른 점은 그림 크기(400x320)와 제 팔레트가 얹히는 첫 색인(74)뿐이다.

#define PAL_BASE  74           // 그림 제 팔레트가 얹히는 첫 색인
#define PAL_MAX   258          // 팔레트 파트 크기(86색 x 3바이트)

static Ls12File g_f;
static int      g_opened = 0;      // Ls12_Open 을 이미 해 봤나(실패해도 1 — 매번 다시 열지 않는다)
static int      g_count  = 0;

static unsigned char g_idx[CITYPIC_SZ];
static unsigned char g_pal[PAL_MAX];
static unsigned char g_rgb[CITYPIC_SZ * 3];
static int           g_cached = -1;   // g_rgb 에 들어 있는 그림 번호

int CityCg_Count(void) { return g_count; }

void CityCg_Load(void)
{
    char exe[MAX_PATH], path[MAX_PATH];
    int i, cut = -1;

    if (g_opened) return;
    g_opened = 1;

    // faces.c / itempic.c 와 같은 관용구 — 실행 파일이 있는 폴더에서 찾는다.
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfA(path, "%s\\CITYCG.CDS", exe);

    if (!Ls12_Open(&g_f, path)) return;
    g_count = g_f.count / 2;      // 그림 한 장이 (그림 + 팔레트) 두 파트를 쓴다
    OutputDebugStringA("[CityPicKR] CITYCG.CDS opened.");
}

void CityCg_Free(void)
{
    Ls12_Close(&g_f);
    g_opened = 0;
    g_count = 0;
    g_cached = -1;
}

// 한 장을 풀어 g_rgb 에 B,G,R 로 채운다. 성공 1.
static int Decode(int pic)
{
    int i, n, np;

    if (pic == g_cached) return 1;
    if (!g_f.data || pic < 0 || 2 * pic + 1 >= g_f.count) return 0;

    // Ls12_DecodePart 는 outcap 으로 조용히 잘라서 돌려주므로 길이를 직접 확인해야 한다.
    n = Ls12_DecodePart(&g_f, 2 * pic, g_idx, CITYPIC_SZ);
    if (n != CITYPIC_SZ) return 0;
    np = Ls12_DecodePart(&g_f, 2 * pic + 1, g_pal, PAL_MAX);
    if (np < 3) return 0;

    for (i = 0; i < CITYPIC_SZ; i++) {
        unsigned char v = g_idx[i];
        int k = (int)v - PAL_BASE;
        if (v >= PAL_BASE && k * 3 + 2 < np) {
            g_rgb[i*3+0] = g_pal[k*3+0];    // B
            g_rgb[i*3+1] = g_pal[k*3+2];    // G
            g_rgb[i*3+2] = g_pal[k*3+1];    // R
        } else {
            g_rgb[i*3+0] = kGamePalette[v*3+2];
            g_rgb[i*3+1] = kGamePalette[v*3+1];
            g_rgb[i*3+2] = kGamePalette[v*3+0];
        }
    }
    g_cached = pic;
    return 1;
}

const unsigned char* CityCg_Pixels(int pic)
{
    return Decode(pic) ? g_rgb : NULL;
}

int CityCg_Draw(HDC dc, int x, int y, int w, int h, int pic)
{
    BITMAPINFO bi;

    if (!Decode(pic)) return 0;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = CITYPIC_W;
    bi.bmiHeader.biHeight = -CITYPIC_H;      // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    // 원본 크기이거나 정수배로 늘릴 때는 도트를 그대로 살린다(그림이 각져 있어 그게 맞다).
    // 줄여 그릴 때만 HALFTONE 으로 섞는다.
    if (w < CITYPIC_W || h < CITYPIC_H) { SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL); }
    else SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, w, h, 0, 0, CITYPIC_W, CITYPIC_H, g_rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
    return 1;
}

// ================================================================== 내보내기 / 갈아 끼우기

#define CITYPIC_PAL_BASE 74           // 그림 제 팔레트가 앉는 첫 색인(PAL_BASE 와 같은 값)
#define CITYPIC_PAL_N    86           // 그 팔레트의 색 수

// 공용 색표에서 후보로 쓸 자리 — 그림 제 팔레트 바로 아래까지다.
// 도시 그림 226장을 다 풀어 보니 색인이 10~159 에만 나온다(0~9 는 게임이 딴 데 쓰는 자리라
// 그림에 섞이면 안 된다 — faces.c 가 같은 이유로 10 아래를 후보에서 뺀다).
#define FIX_LO   10
#define FIX_MAX  (CITYPIC_PAL_BASE - 1)

static unsigned char g_work[CITYPIC_SZ * 3];   // R,G,B 순 작업 버퍼(내보내기/넣기 공용)
static unsigned char g_newIdx[CITYPIC_SZ];
static unsigned char g_newPal[CITYPIC_PAL_N * 3];

static void ArchivePathW(wchar_t* out, int cch)
{
    wchar_t exe[MAX_PATH];
    int i, cut = -1;
    out[0] = 0;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    for (i = 0; exe[i]; i++) if (exe[i] == L'\\' || exe[i] == L'/') cut = i;
    if (cut < 0) return;
    exe[cut] = 0;
    wsprintfW(out, L"%s\\CITYCG.CDS", exe);
    (void)cch;
}

int CityCg_ExportPng(int pic, const wchar_t* path)
{
    int i;

    if (!Img_Available()) return CITYPIC_ERR_GDIP;
    CityCg_Load();
    if (!g_f.data || g_count <= 0) return CITYPIC_ERR_ARCHIVE;
    if (pic < 0 || pic >= g_count) return CITYPIC_ERR_RANGE;
    if (!Decode(pic)) return CITYPIC_ERR_IMAGE;

    // g_rgb 는 화면에 찍으려고 B,G,R 로 담아 둔 것이라 PNG 쪽(R,G,B)으로 뒤집는다.
    for (i = 0; i < CITYPIC_SZ; i++) {
        g_work[i*3+0] = g_rgb[i*3+2];
        g_work[i*3+1] = g_rgb[i*3+1];
        g_work[i*3+2] = g_rgb[i*3+0];
    }
    return Img_SavePng(path, CITYPIC_W, CITYPIC_H, g_work) ? CITYPIC_ERR_OK : CITYPIC_ERR_WRITE;
}

// 원본을 CITYCG.CDS.orig 로 딱 한 번 남긴다. 이미 있으면 그게 진짜 원본이므로 건드리지 않는다.
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

// 지금 열려 있는 파일에서 새 파트가 넘으면 안 되는 두 가지 한계를 잰다.
//   크기   짝수(그림)·홀수(팔레트) 파트의 최대 압축 크기. 원본은 113,474 · 258.
//   코드값 압축 스트림에 나오는 가장 큰 코드. 원본은 8,447(= 거리 8,191).
// 둘 다 넘겨 봤고 둘 다 게임이 죽었다 —
//   1차(2026-08-13) 크기를 넘김: 팔레트 371바이트, 그림 122,790
//   2차             크기는 원본보다 작은데 거리를 16,382 까지 씀 → 코드 16,638
// 그래서 넣기 전에 둘 다 확인한다. 코드값 쪽이 더 확실한 잣대다.
static void PartLimits(unsigned* imgMax, unsigned* palMax, unsigned* codeMax)
{
    int i;
    *imgMax = 0; *palMax = 0; *codeMax = 0;
    for (i = 0; i + 1 < g_f.count; i += 2) {
        unsigned c;
        if (g_f.comp[i]     > *imgMax) *imgMax = g_f.comp[i];
        if (g_f.comp[i + 1] > *palMax) *palMax = g_f.comp[i + 1];
        c = Ls12_PartMaxCode(&g_f, i);
        if (c > *codeMax) *codeMax = c;
    }
}

// 그림 파트와 팔레트 파트를 잇달아 갈아 끼운다. 한 장이 두 파트라 한 번에 끝나지 않는다 —
// 먼저 그림을 바꾼 결과를 메모리에서 다시 열어(Ls12_OpenMem) 그 위에 팔레트를 바꾼다.
// 둘 다 verify 와 크기 검사를 통과해야 파일에 쓴다.
static int WritePair(int pic, const unsigned char* idx8, const unsigned char* pal258)
{
    wchar_t path[MAX_PATH];
    unsigned char *buf1 = NULL, *buf2 = NULL;
    unsigned cap, len1 = 0, len2 = 0, imgMax = 0, palMax = 0, codeMax = 0;
    Ls12File mid;
    int rc = CITYPIC_ERR_OK;

    PartLimits(&imgMax, &palMax, &codeMax);
    cap = Ls12_RewriteCap(&g_f, (unsigned)CITYPIC_SZ);
    buf1 = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, cap);
    if (!buf1) return CITYPIC_ERR_ENCODE;

    len1 = Ls12_Rewrite(&g_f, 2 * pic, idx8, (unsigned)CITYPIC_SZ, buf1, cap);
    if (!len1) rc = CITYPIC_ERR_ENCODE;
    else if (!Ls12_VerifyPart(buf1, len1, 2 * pic, idx8, (unsigned)CITYPIC_SZ)) rc = CITYPIC_ERR_VERIFY;

    if (rc == CITYPIC_ERR_OK) {
        if (!Ls12_OpenMem(&mid, buf1, len1)) rc = CITYPIC_ERR_ENCODE;
        else if (imgMax && mid.comp[2 * pic] > imgMax) { rc = CITYPIC_ERR_TOOBIG; Ls12_Close(&mid); }
        else {
            cap = Ls12_RewriteCap(&mid, 258u);
            buf2 = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, cap);
            if (!buf2) rc = CITYPIC_ERR_ENCODE;
            else {
                // 팔레트는 원본이 전부 무압축(258바이트)이라 형식까지 똑같이 맞춘다.
                len2 = Ls12_RewriteEx(&mid, 2 * pic + 1, pal258, 258u, buf2, cap, 1);
                if (!len2) rc = CITYPIC_ERR_ENCODE;
                else if (!Ls12_VerifyPart(buf2, len2, 2 * pic + 1, pal258, 258u)) rc = CITYPIC_ERR_VERIFY;
                // 그림 파트도 두 번째 묶음에서 그대로 살아 있는지 한 번 더 본다.
                else if (!Ls12_VerifyPart(buf2, len2, 2 * pic, idx8, (unsigned)CITYPIC_SZ)) rc = CITYPIC_ERR_VERIFY;
            }
            Ls12_Close(&mid);
        }
    }

    // 다 만든 뒤 두 파트 크기를 마지막으로 확인한다(팔레트는 무압축이면 258 그대로다).
    if (rc == CITYPIC_ERR_OK) {
        Ls12File fin;
        if (!Ls12_OpenMem(&fin, buf2, len2)) rc = CITYPIC_ERR_ENCODE;
        else {
            if ((imgMax && fin.comp[2 * pic]     > imgMax) ||
                (palMax && fin.comp[2 * pic + 1] > palMax)) rc = CITYPIC_ERR_TOOBIG;
            // 크기가 맞아도 코드값이 원본 범위를 넘으면 게임이 못 읽는다(2차 사고가 이것이었다).
            else if (codeMax && Ls12_PartMaxCode(&fin, 2 * pic) > codeMax) rc = CITYPIC_ERR_TOOBIG;
            Ls12_Close(&fin);
        }
    }

    if (rc == CITYPIC_ERR_OK) {
        ArchivePathW(path, MAX_PATH);
        if (!path[0]) rc = CITYPIC_ERR_WRITE;
        else {
            BackupOnce(path);
            if (!WriteWhole(path, buf2, len2)) rc = CITYPIC_ERR_WRITE;
        }
    }

    if (buf2) HeapFree(GetProcessHeap(), 0, buf2);
    HeapFree(GetProcessHeap(), 0, buf1);

    if (rc == CITYPIC_ERR_OK) {
        CityCg_Free();      // 새 파일로 다시 연다 — 창에 바로 보이게
        CityCg_Load();
    }
    return rc;
}

static void OrigPathW(wchar_t* out, int cch)
{
    ArchivePathW(out, cch);
    if (out[0]) lstrcatW(out, L".orig");
}

int CityCg_HasOriginal(void)
{
    wchar_t orig[MAX_PATH];
    OrigPathW(orig, MAX_PATH);
    if (!orig[0]) return 0;
    return GetFileAttributesW(orig) != INVALID_FILE_ATTRIBUTES;
}

int CityCg_RestoreOriginal(void)
{
    wchar_t path[MAX_PATH], orig[MAX_PATH];

    ArchivePathW(path, MAX_PATH);
    OrigPathW(orig, MAX_PATH);
    if (!path[0] || !orig[0]) return CITYPIC_ERR_WRITE;
    if (GetFileAttributesW(orig) == INVALID_FILE_ATTRIBUTES) return CITYPIC_ERR_NOORIG;

    // 파일을 다시 열려면 먼저 놓아야 한다(우리가 쥐고 있진 않지만 캐시가 남는다).
    CityCg_Free();
    if (!CopyFileW(orig, path, FALSE)) { CityCg_Load(); return CITYPIC_ERR_WRITE; }
    CityCg_Load();
    return g_count > 0 ? CITYPIC_ERR_OK : CITYPIC_ERR_ARCHIVE;
}

// 색을 적게 쓸수록 색인 그림이 단조로워져 압축이 잘 먹는다. 새 파트가 원본 관례보다
// 크면(CITYPIC_ERR_TOOBIG) 색 수를 줄여 다시 해 본다 — 원본이 이미 파일에서 제일 큰
// 그림(#132)은 86색 그대로면 283바이트가 모자라서 못 들어간다.
static const int kPalTries[] = { CITYPIC_PAL_N, 64, 48, 32 };

int CityCg_ImportPng(int pic, const wchar_t* path, int* exact)
{
    unsigned char pal258[258];
    int i, t, ex = 0, rc = CITYPIC_ERR_TOOBIG;

    if (exact) *exact = 0;
    if (!Img_Available()) return CITYPIC_ERR_GDIP;
    CityCg_Load();
    if (!g_f.data || g_count <= 0) return CITYPIC_ERR_ARCHIVE;
    if (pic < 0 || pic >= g_count) return CITYPIC_ERR_RANGE;

    // 투명한 그림을 넣어도 얼룩이 안 지도록 검정을 깔아 둔다(액자 바깥이 검정인 그림이 많다).
    if (!Img_LoadScaled(path, CITYPIC_W, CITYPIC_H, 0, 0, 0, g_work))
        return CITYPIC_ERR_IMAGE;

    for (t = 0; t < (int)(sizeof(kPalTries)/sizeof(kPalTries[0])); t++) {
        int n = kPalTries[t];
        ex = Quant_Index(g_work, CITYPIC_SZ, kGamePalette, FIX_LO, FIX_MAX,
                         CITYPIC_PAL_BASE, n, g_newPal, g_newIdx);
        // 파일 속 팔레트는 한 색이 (파랑, 빨강, 초록) 순이다(citycg.h 참고).
        // 안 쓴 칸은 0 으로 남긴다 — 그 색인은 그림에 나오지 않는다.
        ZeroMemory(pal258, sizeof(pal258));
        for (i = 0; i < n; i++) {
            pal258[i*3+0] = g_newPal[i*3+2];   // B
            pal258[i*3+1] = g_newPal[i*3+0];   // R
            pal258[i*3+2] = g_newPal[i*3+1];   // G
        }
        rc = WritePair(pic, g_newIdx, pal258);
        if (rc != CITYPIC_ERR_TOOBIG) break;   // 크기 말고 다른 이유면 더 줄여도 소용없다
        if (t + 1 < (int)(sizeof(kPalTries)/sizeof(kPalTries[0]))) ex = 0;   // 줄이면 색이 밀린다
    }
    if (exact) *exact = (rc == CITYPIC_ERR_OK) ? ex : 0;
    return rc;
}
