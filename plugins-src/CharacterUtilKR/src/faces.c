#include "faces.h"
#include "ls12.h"
#include "ui.h"
#include "face_palette.h"   // kFacePalette[768]

static Ls12File g_male, g_female;
static int      g_loaded = 0;
static unsigned char g_idx[LS12_FACE_SZ];

static Ls12File* FileOf(int gender)
{
    return gender == FACE_FEMALE ? &g_female : &g_male;
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
        rgb[i*3+0] = kFacePalette[v*3+2];
        rgb[i*3+1] = kFacePalette[v*3+1];
        rgb[i*3+2] = kFacePalette[v*3+0];
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
