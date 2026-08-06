#include "imgio.h"

// GDI+ 평면(flat) API 중 쓰는 것만 직접 선언한다. gdiplus.h 는 C++ 전용이다.
typedef struct {
    UINT32 GdiplusVersion;
    void*  DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

typedef int  (WINAPI *PFN_Startup)(ULONG_PTR*, const GpStartupInput*, void*);
typedef void (WINAPI *PFN_Shutdown)(ULONG_PTR);
typedef int  (WINAPI *PFN_BitmapFromFile)(const WCHAR*, void**);
typedef int  (WINAPI *PFN_BitmapFromHBITMAP)(HBITMAP, HPALETTE, void**);
typedef int  (WINAPI *PFN_DisposeImage)(void*);
typedef int  (WINAPI *PFN_FromHDC)(HDC, void**);
typedef int  (WINAPI *PFN_DeleteGraphics)(void*);
typedef int  (WINAPI *PFN_SetInterp)(void*, int);
typedef int  (WINAPI *PFN_DrawImageRectI)(void*, void*, int, int, int, int);
typedef int  (WINAPI *PFN_SaveToFile)(void*, const WCHAR*, const CLSID*, const void*);

#define INTERP_HQ_BICUBIC 7

static HMODULE               g_dll   = NULL;
static ULONG_PTR             g_token = 0;
static int                   g_tried = 0;
static PFN_Startup           pStartup;
static PFN_Shutdown          pShutdown;
static PFN_BitmapFromFile    pFromFile;
static PFN_BitmapFromHBITMAP pFromHBITMAP;
static PFN_DisposeImage      pDispose;
static PFN_FromHDC           pFromHDC;
static PFN_DeleteGraphics    pDelGraphics;
static PFN_SetInterp         pSetInterp;
static PFN_DrawImageRectI    pDrawRect;
static PFN_SaveToFile        pSave;

// {557CF406-1A04-11D3-9A73-0000F81EF32E} — PNG 인코더
static const CLSID kPngClsid =
    { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };

static int Boot(void)
{
    GpStartupInput in;
    if (g_tried) return g_dll != NULL;
    g_tried = 1;
    g_dll = LoadLibraryW(L"gdiplus.dll");
    if (!g_dll) { OutputDebugStringW(L"[CharacterUtilKR] gdiplus.dll 없음 — PNG 기능 꺼짐"); return 0; }

    pStartup     = (PFN_Startup)          GetProcAddress(g_dll, "GdiplusStartup");
    pShutdown    = (PFN_Shutdown)         GetProcAddress(g_dll, "GdiplusShutdown");
    pFromFile    = (PFN_BitmapFromFile)   GetProcAddress(g_dll, "GdipCreateBitmapFromFile");
    pFromHBITMAP = (PFN_BitmapFromHBITMAP)GetProcAddress(g_dll, "GdipCreateBitmapFromHBITMAP");
    pDispose     = (PFN_DisposeImage)     GetProcAddress(g_dll, "GdipDisposeImage");
    pFromHDC     = (PFN_FromHDC)          GetProcAddress(g_dll, "GdipCreateFromHDC");
    pDelGraphics = (PFN_DeleteGraphics)   GetProcAddress(g_dll, "GdipDeleteGraphics");
    pSetInterp   = (PFN_SetInterp)        GetProcAddress(g_dll, "GdipSetInterpolationMode");
    pDrawRect    = (PFN_DrawImageRectI)   GetProcAddress(g_dll, "GdipDrawImageRectI");
    pSave        = (PFN_SaveToFile)       GetProcAddress(g_dll, "GdipSaveImageToFile");

    if (!pStartup || !pShutdown || !pFromFile || !pFromHBITMAP || !pDispose ||
        !pFromHDC || !pDelGraphics || !pSetInterp || !pDrawRect || !pSave) {
        FreeLibrary(g_dll); g_dll = NULL; return 0;
    }

    ZeroMemory(&in, sizeof(in));
    in.GdiplusVersion = 1;
    if (pStartup(&g_token, &in, NULL) != 0) { FreeLibrary(g_dll); g_dll = NULL; return 0; }
    return 1;
}

int Img_Available(void) { return Boot(); }

// w x h 24bpp DIB 하나. bits 로 픽셀 자리를 돌려준다(아래에서 위가 아니라 위에서 아래).
static HBITMAP MakeDib(int w, int h, HDC dc, void** bits)
{
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;          // 음수 = 위에서 아래로
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

static int Stride(int w) { return (w * 3 + 3) & ~3; }

int Img_LoadScaled(const wchar_t* path, int w, int h,
                   int bgR, int bgG, int bgB, unsigned char* rgb)
{
    HDC screen, mem;
    HBITMAP dib, old;
    void* bits = NULL;
    void* img = NULL;
    void* gfx = NULL;
    int ok = 0, x, y, st;

    if (!Boot() || !path || !rgb || w <= 0 || h <= 0) return 0;
    if (pFromFile(path, &img) != 0 || !img) return 0;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    dib = MakeDib(w, h, screen, &bits);
    ReleaseDC(NULL, screen);
    if (!mem || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        pDispose(img);
        return 0;
    }
    old = (HBITMAP)SelectObject(mem, dib);

    {   // 투명한 곳이 검게 남지 않도록 먼저 바탕을 깐다
        RECT r; HBRUSH br;
        r.left = 0; r.top = 0; r.right = w; r.bottom = h;
        br = CreateSolidBrush(RGB(bgR, bgG, bgB));
        FillRect(mem, &r, br);
        DeleteObject(br);
    }

    if (pFromHDC(mem, &gfx) == 0 && gfx) {
        pSetInterp(gfx, INTERP_HQ_BICUBIC);
        ok = (pDrawRect(gfx, img, 0, 0, w, h) == 0);
        pDelGraphics(gfx);
    }

    if (ok) {
        st = Stride(w);
        for (y = 0; y < h; y++) {
            const unsigned char* row = (const unsigned char*)bits + (size_t)y * st;
            for (x = 0; x < w; x++) {
                rgb[(y * w + x) * 3 + 0] = row[x * 3 + 2];   // DIB 는 B,G,R
                rgb[(y * w + x) * 3 + 1] = row[x * 3 + 1];
                rgb[(y * w + x) * 3 + 2] = row[x * 3 + 0];
            }
        }
    }

    SelectObject(mem, old);
    DeleteObject(dib);
    DeleteDC(mem);
    pDispose(img);
    return ok;
}

int Img_SavePng(const wchar_t* path, int w, int h, const unsigned char* rgb)
{
    HDC screen, mem;
    HBITMAP dib, old;
    void* bits = NULL;
    void* bmp = NULL;
    int ok = 0, x, y, st;

    if (!Boot() || !path || !rgb || w <= 0 || h <= 0) return 0;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    dib = MakeDib(w, h, screen, &bits);
    ReleaseDC(NULL, screen);
    if (!mem || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        return 0;
    }
    old = (HBITMAP)SelectObject(mem, dib);

    st = Stride(w);
    for (y = 0; y < h; y++) {
        unsigned char* row = (unsigned char*)bits + (size_t)y * st;
        for (x = 0; x < w; x++) {
            row[x * 3 + 0] = rgb[(y * w + x) * 3 + 2];
            row[x * 3 + 1] = rgb[(y * w + x) * 3 + 1];
            row[x * 3 + 2] = rgb[(y * w + x) * 3 + 0];
        }
    }
    // GDI+ 로 넘기기 전에 GDI 쪽 그리기를 끝내 둔다
    GdiFlush();
    SelectObject(mem, old);

    if (pFromHBITMAP(dib, NULL, &bmp) == 0 && bmp) {
        ok = (pSave(bmp, path, &kPngClsid, NULL) == 0);
        pDispose(bmp);
    }

    DeleteObject(dib);
    DeleteDC(mem);
    return ok;
}
