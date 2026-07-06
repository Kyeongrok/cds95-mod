#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string.h>
#include "ship_palette.h"   // kFacePalette[768] — 게임 실제 해상 팔레트(캘리브레이션 역산)

// ShipSkinKR — "함선 스프라이트" 창 (인물 브라우저 패턴). fb5-1:
//   함선 메뉴 → 이 창이 뜨고, 4형태×8방향을 컬러로 보여줌.
//   배(행) 클릭 = 그 형태를 해상 스킨으로 선택(스탯 불변). [내보내기]/[불러오기] 버튼으로 파일 편집 루프.
// 렌더: 아틀라스 0x5D68C8(48×48 팔레트인덱스) → kFacePalette 로 24bpp 변환 → StretchDIBits.

#define WC_SHIP   L"ShipSkinKR_Window"

#define ATLAS_RVA  0x1D68C8u
#define FRAME_SZ   2304u
#define FW         48
#define FH         48
#define NGID       4
#define NDIR       8

#define WFRAME     3
#define TITLE_H    24
#define LBL_W      78
#define DISP       54           // 프레임 표시 크기(48→54)
#define CGAP       4
#define PITCH      (DISP + CGAP)
#define GX         (WFRAME + LBL_W)
#define GY         (WFRAME + TITLE_H + 6)
#define GRID_H     (NGID * PITCH)
#define BTN_Y      (GY + GRID_H + 8)
#define BTN_H      24
#define WIN_W      (GX + NDIR*PITCH + WFRAME + 4)
#define WIN_H      (BTN_Y + BTN_H + WFRAME + 6)

#define COL_BG      RGB(38,48,66)
#define COL_PANEL   RGB(28,36,52)
#define COL_LINE    RGB(90,110,140)
#define COL_TITLE   RGB(70,92,124)
#define COL_TEXT    RGB(226,232,240)
#define COL_SEL     RGB(210,170,70)
#define COL_BTN     RGB(72,92,120)

// hook.c / menu.c 공유
void ShipSkin_OverlayDir(wchar_t* out);
void ShipSkin_OverlayPath(wchar_t* out);

// 형태 4종(gid0~3) 대표 함선종류 + 라벨
static const struct { int type; const wchar_t* label; } kForms[NGID] = {
    { 0, L"코구·다우" }, { 1, L"카라벨" }, { 3, L"카락" }, { 6, L"갤리온" },
};

static HINSTANCE g_hinst = NULL;
static HWND    g_wnd = NULL;
static HFONT   g_font = NULL;
static HCURSOR g_hand = NULL, g_arrow = NULL;   // 클릭 요소 위 손가락 커서
static int     g_sel = -1;            // 선택된 형태(gid) — 없으면 -1

// %TEMP%\cds_shiptype.txt 에 형태 type write (getter 리다이렉트가 읽어 즉시 반영)
static void WriteShipType(int type)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH]; HANDLE f; char buf[16]; DWORD wr; int n;
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    wsprintfW(path, L"%scds_shiptype.txt", tmp);
    n = wsprintfA(buf, "%d", type);
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, buf, (DWORD)n, &wr, NULL); CloseHandle(f); }
}

// ---- 버튼 사각형 ----
static RECT CloseRect(void)  { RECT r; r.right=WIN_W-WFRAME-4; r.left=r.right-20; r.top=WFRAME+3; r.bottom=r.top+18; return r; }
#define NBTN 3
static RECT BtnRect(int i)   { RECT r; int w=120; r.left=GX + i*(w+8); r.right=r.left+w; r.top=BTN_Y; r.bottom=BTN_Y+BTN_H; return r; }
static RECT RowRect(int gid) { RECT r; r.left=WFRAME+2; r.right=GX-2; r.top=GY+gid*PITCH; r.bottom=r.top+DISP; return r; }
static RECT RowChangeRect(int gid) { RECT r; int t=GY+gid*PITCH; r.left=WFRAME+8; r.right=GX-6; r.top=t+30; r.bottom=t+48; return r; }  // sp5-10 배별 [변경]
static const wchar_t* kBtn[NBTN] = { L"내보내기", L"불러오기", L"원래대로(전체)" };

// 미리보기용 아틀라스 캐시: 오버레이 ship_atlas.bin(=반영될 스킨) 우선, 없으면 라이브 0x5D68C8.
// (라이브는 타이틀/비-해상 상태에서 비어있을 수 있어 창이 검게 나오는 것 방지.)
static BYTE g_atlasCache[0x12000];
static void LoadAtlasCache(void)
{
    wchar_t ov[MAX_PATH]; HANDLE f; DWORD rd = 0; BOOL ok = FALSE;
    ShipSkin_OverlayPath(ov);
    f = CreateFileW(ov, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        ok = ReadFile(f, g_atlasCache, sizeof(g_atlasCache), &rd, NULL) && rd == sizeof(g_atlasCache);
        CloseHandle(f);
        if (ok) { DWORD i, nz = 0; for (i = 0; i < sizeof(g_atlasCache); i += 64) { if (g_atlasCache[i]) { nz++; if (nz >= 8) break; } } if (nz < 8) ok = FALSE; }
    }
    if (!ok) memcpy(g_atlasCache, (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA, sizeof(g_atlasCache));
}

// ---- 한 프레임 렌더 (gid,dir) → (x,y) DISP×DISP ----
static void DrawFrame(HDC dc, int x, int y, int gid, int dir, COLORREF bg)
{
    const BYTE* fr = g_atlasCache + ((SIZE_T)(gid*NDIR + dir)) * FRAME_SZ;
    static BYTE rgb[FW*FH*3];
    BITMAPINFO bi; int i;
    BYTE br = GetRValue(bg), bgc = GetGValue(bg), bb = GetBValue(bg);
    for (i = 0; i < FW*FH; i++) {
        BYTE v = fr[i];
        if (v == 0) { rgb[i*3+0]=bb; rgb[i*3+1]=bgc; rgb[i*3+2]=br; }   // 투명 = 셀 배경
        else { rgb[i*3+0]=kFacePalette[v*3+2]; rgb[i*3+1]=kFacePalette[v*3+1]; rgb[i*3+2]=kFacePalette[v*3+0]; }
    }
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = FW; bi.bmiHeader.biHeight = -FH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, x, y, DISP, DISP, 0, 0, FW, FH, rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
}

static void DrawBtn(HDC dc, RECT r, const wchar_t* t)
{
    HBRUSH b = CreateSolidBrush(COL_BTN); FillRect(dc, &r, b); DeleteObject(b);
    b = CreateSolidBrush(COL_LINE); FrameRect(dc, &r, b); DeleteObject(b);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    DrawTextW(dc, t, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}

static void OnPaint(HWND h)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT rc, tb, cb; HBRUSH b; HFONT of; int gid, dir;
    GetClientRect(h, &rc);
    b = CreateSolidBrush(COL_BG); FillRect(dc, &rc, b); DeleteObject(b);
    b = CreateSolidBrush(COL_LINE); FrameRect(dc, &rc, b); DeleteObject(b);
    // 타이틀바
    tb.left=WFRAME; tb.top=WFRAME; tb.right=rc.right-WFRAME; tb.bottom=WFRAME+TITLE_H;
    b = CreateSolidBrush(COL_TITLE); FillRect(dc, &tb, b); DeleteObject(b);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_font);
    { RECT tr=tb; tr.left+=8; DrawTextW(dc, L"함선 스프라이트 — [변경]=스킨 선택/추가/원본, 아래=편집(내보내기·불러오기)", -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
    cb = CloseRect(); DrawBtn(dc, cb, L"×");
    // 그리드 배경
    { RECT gr; gr.left=GX-1; gr.top=GY-1; gr.right=GX+NDIR*PITCH; gr.bottom=GY+GRID_H;
      b=CreateSolidBrush(COL_PANEL); FillRect(dc,&gr,b); DeleteObject(b); }
    for (gid = 0; gid < NGID; gid++) {
        // 행 라벨 + 선택 하이라이트
        RECT lr = RowRect(gid);
        if (gid == g_sel) { HBRUSH s=CreateSolidBrush(COL_SEL); FrameRect(dc,&lr,s); DeleteObject(s);
                            InflateRect(&lr,-1,-1); s=CreateSolidBrush(COL_SEL); FrameRect(dc,&lr,s); DeleteObject(s); lr=RowRect(gid); }
        SetTextColor(dc, gid==g_sel ? COL_SEL : COL_TEXT);
        { RECT tr=lr; tr.left+=4; tr.bottom=tr.top+26; DrawTextW(dc, kForms[gid].label, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
        DrawBtn(dc, RowChangeRect(gid), L"변경");   // sp5-10: 스킨 목록 열기
        for (dir = 0; dir < NDIR; dir++)
            DrawFrame(dc, GX + dir*PITCH, GY + gid*PITCH, gid, dir, COL_PANEL);
    }
    // 버튼
    { int i; for (i=0;i<NBTN;i++) DrawBtn(dc, BtnRect(i), kBtn[i]); }
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

// ---- BMP 라운드트립 (그림판 편집용) ----
// fb5-5: 배(gid) 한 종 = 8방향 = 384×48 BMP 로 개별 내보내기/불러오기. 프레임 48×48, 마젠타(255,0,255)=투명.
#define SHEET_W   (NDIR*FW)      // 384 (8방향)
#define SHEET_H   FH             // 48  (1종)
#define ATLAS_LEN 0x12000u       // 전체 4종
#define GID_LEN   (NDIR*(int)FRAME_SZ)   // 18432 (gid 하나 = 8프레임)

// 종별 파일 경로: shipskin\ship_g{gid}.{ext}
static void GidFile(wchar_t* out, int gid, const wchar_t* ext)
{
    wchar_t dir[MAX_PATH];
    ShipSkin_OverlayDir(dir);
    wsprintfW(out, L"%s\\ship_g%d.%s", dir, gid, ext);
}

// per-gid BMP 이미지 (X,Y) → 그 gid 내부 인덱스 오프셋 (0..GID_LEN)
static int GidOff(int X, int Y)
{
    int dir = X / FW, fx = X % FW, fy = Y;   // Y = fy (0..47)
    return dir*(int)FRAME_SZ + fy*FW + fx;
}

// 인덱스 → 편집용 RGB (0=마젠타)
static void IdxToRGB(BYTE v, BYTE* R, BYTE* G, BYTE* B)
{
    if (v == 0) { *R=255; *G=0; *B=255; }
    else { *R=kFacePalette[v*3+0]; *G=kFacePalette[v*3+1]; *B=kFacePalette[v*3+2]; }
}

// RGB → 가장 가까운 팔레트 인덱스 (1~255, 투명 제외)
static BYTE NearestIdx(BYTE R, BYTE G, BYTE B)
{
    int best = 1; long bestd = 0x7FFFFFFF; int v;
    for (v = 1; v < 256; v++) {
        int dr = (int)R - kFacePalette[v*3+0];
        int dg = (int)G - kFacePalette[v*3+1];
        int db = (int)B - kFacePalette[v*3+2];
        long d = (long)dr*dr + (long)dg*dg + (long)db*db;
        if (d < bestd) { bestd = d; best = v; if (d == 0) break; }
    }
    return (BYTE)best;
}

// gid 하나의 인덱스(GID_LEN) → 384×48 24bpp BMP 파일
static BOOL WriteGidBmp(const wchar_t* path, const BYTE* gidIdx)
{
    BITMAPFILEHEADER fh; BITMAPINFOHEADER ih; HANDLE f; DWORD wr;
    int stride = SHEET_W*3, X, Y; static BYTE row[SHEET_W*3];
    ZeroMemory(&fh, sizeof(fh)); ZeroMemory(&ih, sizeof(ih));
    ih.biSize = sizeof(ih); ih.biWidth = SHEET_W; ih.biHeight = SHEET_H;   // 양수 = bottom-up
    ih.biPlanes = 1; ih.biBitCount = 24; ih.biCompression = BI_RGB;
    fh.bfType = 0x4D42;   // 'BM'
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + stride*SHEET_H;
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    WriteFile(f, &fh, sizeof(fh), &wr, NULL);
    WriteFile(f, &ih, sizeof(ih), &wr, NULL);
    for (Y = SHEET_H-1; Y >= 0; Y--) {           // bottom-up
        for (X = 0; X < SHEET_W; X++) {
            BYTE R,G,B; IdxToRGB(gidIdx[GidOff(X,Y)], &R,&G,&B);
            row[X*3+0]=B; row[X*3+1]=G; row[X*3+2]=R;
        }
        WriteFile(f, row, stride, &wr, NULL);
    }
    CloseHandle(f);
    return TRUE;
}

// 24/32bpp BMP 파일 → RGB 픽셀(top-down, SHEET_W*SHEET_H*3). 크기/형식 검증(편집기별 24/32 모두 수용).
static BOOL ReadBmpRGB(const wchar_t* path, BYTE* outRGB)
{
    BITMAPFILEHEADER fh; BITMAPINFOHEADER ih; HANDLE f; DWORD rd;
    int bpp, stride, Y, X; static BYTE row[SHEET_W*4];
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (!ReadFile(f, &fh, sizeof(fh), &rd, NULL) || rd != sizeof(fh) || fh.bfType != 0x4D42) { CloseHandle(f); return FALSE; }
    if (!ReadFile(f, &ih, sizeof(ih), &rd, NULL) || rd != sizeof(ih)) { CloseHandle(f); return FALSE; }
    bpp = ih.biBitCount;
    if (ih.biWidth != SHEET_W || (ih.biHeight != SHEET_H && ih.biHeight != -SHEET_H) ||
        (bpp != 24 && bpp != 32) || ih.biCompression != BI_RGB) { CloseHandle(f); return FALSE; }
    SetFilePointer(f, fh.bfOffBits, NULL, FILE_BEGIN);
    stride = ((SHEET_W * (bpp/8)) + 3) & ~3;     // 4바이트 정렬
    { int bpx = bpp/8; BOOL bottomUp = (ih.biHeight > 0);
      for (Y = 0; Y < SHEET_H; Y++) {
        int dy = bottomUp ? (SHEET_H-1-Y) : Y;
        if (!ReadFile(f, row, stride, &rd, NULL) || rd != (DWORD)stride) { CloseHandle(f); return FALSE; }
        for (X = 0; X < SHEET_W; X++) {
            outRGB[(dy*SHEET_W+X)*3+0]=row[X*bpx+2]; // R
            outRGB[(dy*SHEET_W+X)*3+1]=row[X*bpx+1]; // G
            outRGB[(dy*SHEET_W+X)*3+2]=row[X*bpx+0]; // B
        }
      }
    }
    CloseHandle(f);
    return TRUE;
}

static void NeedSelect(HWND h)
{
    MessageBoxW(h, L"먼저 편집할 배를 클릭해 선택하세요.\n(창에서 배 한 종을 클릭하면 노랑 테두리로 선택됩니다.)",
                L"배 선택 필요", MB_OK | MB_ICONINFORMATION);
}

// ---- 내보내기(선택 종만): 그 gid 8방향 → shipskin\ship_g{gid}.bmp + .src(diff 기준) ----
static void DoExport(HWND h, int gid)
{
    const BYTE* gidIdx = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA + gid*GID_LEN;
    wchar_t dir[MAX_PATH], bmp[MAX_PATH], src[MAX_PATH], msg[512]; HANDLE f; DWORD wr;
    ShipSkin_OverlayDir(dir); CreateDirectoryW(dir, NULL);
    GidFile(bmp, gid, L"bmp");
    GidFile(src, gid, L"src");
    f = CreateFileW(src, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, gidIdx, GID_LEN, &wr, NULL); CloseHandle(f); }
    if (!WriteGidBmp(bmp, gidIdx)) { MessageBoxW(h, L"BMP 저장 실패.", L"내보내기", MB_OK|MB_ICONWARNING); return; }
    ShellExecuteW(NULL, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
    wsprintfW(msg,
        L"[%s] 스프라이트를 ship_g%d.bmp 로 내보냈습니다 (384×48, 8방향).\n\n"
        L"그림판 등으로 열어 색/도트를 편집하세요.\n"
        L"  · 분홍(마젠타)= 투명(바다). 그대로 두세요.\n"
        L"  · 크기(384×48)·24비트 BMP 유지, 같은 이름으로 저장.\n\n"
        L"편집 후 이 배를 선택한 채 [불러오기]를 누르세요(재시작 반영).",
        kForms[gid].label, gid);
    MessageBoxW(h, msg, L"내보내기 — 그림판 편집", MB_OK | MB_ICONINFORMATION);
}

// ---- 불러오기(선택 종만): ship_g{gid}.bmp → 오버레이 ship_atlas.bin 의 그 gid 영역만 갱신 ----
static void DoImport(HWND h, int gid)
{
    const BYTE* live = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA;
    wchar_t bmp[MAX_PATH], src[MAX_PATH], ov[MAX_PATH], dir[MAX_PATH], msg[256]; HANDLE f; DWORD rd, wr;
    static BYTE rgb[SHEET_W*SHEET_H*3];   // 384*48*3
    static BYTE base[GID_LEN];            // 선택 gid baseline
    static BYTE atlasBuf[ATLAS_LEN];      // 전체 오버레이(다른 gid 유지)
    BOOL haveBase, loaded; int X, Y;
    GidFile(bmp, gid, L"bmp");
    GidFile(src, gid, L"src");
    ShipSkin_OverlayPath(ov);
    if (!ReadBmpRGB(bmp, rgb)) {
        wsprintfW(msg, L"ship_g%d.bmp 를 읽을 수 없습니다.\n먼저 이 배를 선택하고 [내보내기] 후, 384×48·24비트 BMP로 저장했는지 확인하세요.", gid);
        MessageBoxW(h, msg, L"불러오기 실패", MB_OK|MB_ICONWARNING);
        return;
    }
    // 선택 gid baseline(.src). 없으면 현재 아틀라스의 그 gid 영역.
    haveBase = FALSE;
    f = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { haveBase = ReadFile(f, base, GID_LEN, &rd, NULL) && rd == GID_LEN; CloseHandle(f); }
    if (!haveBase) memcpy(base, live + gid*GID_LEN, GID_LEN);
    // 전체 오버레이 시드: 기존 .bin 있으면 로드(다른 gid 편집 유지), 없으면 현재 아틀라스.
    loaded = FALSE;
    f = CreateFileW(ov, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { loaded = ReadFile(f, atlasBuf, ATLAS_LEN, &rd, NULL) && rd == ATLAS_LEN; CloseHandle(f); }
    if (!loaded) memcpy(atlasBuf, live, ATLAS_LEN);
    // 선택 gid 영역만 baseline-diff 로 갱신 (미편집 픽셀=원본 보존)
    for (Y = 0; Y < SHEET_H; Y++) for (X = 0; X < SHEET_W; X++) {
        int off = GidOff(X, Y);
        BYTE R = rgb[(Y*SHEET_W+X)*3+0], G = rgb[(Y*SHEET_W+X)*3+1], B = rgb[(Y*SHEET_W+X)*3+2];
        BYTE bR,bG,bB; IdxToRGB(base[off], &bR,&bG,&bB);
        BYTE outIdx;
        if (R==bR && G==bG && B==bB) outIdx = base[off];
        else if (R==255 && G==0 && B==255) outIdx = 0;
        else outIdx = NearestIdx(R,G,B);
        atlasBuf[gid*GID_LEN + off] = outIdx;
    }
    ShipSkin_OverlayDir(dir); CreateDirectoryW(dir, NULL);
    f = CreateFileW(ov, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, atlasBuf, ATLAS_LEN, &wr, NULL); CloseHandle(f); }
    wsprintfW(msg, L"[%s] 편집본을 적용했습니다.\n게임을 재시작하면 반영됩니다.\n(다른 배는 그대로 유지됩니다.)", kForms[gid].label);
    MessageBoxW(h, msg, L"불러오기", MB_OK | MB_ICONINFORMATION);
}

// 스킨 BMP(384×48, 배 한 종) → 오버레이 ship_atlas.bin 의 gid 영역 전체 교체. (편집 아닌 교체)
static BOOL ApplySkinBmp(int gid, const wchar_t* path)
{
    const BYTE* live = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA;
    static BYTE rgb[SHEET_W*SHEET_H*3];
    static BYTE atlasBuf[ATLAS_LEN];
    wchar_t ov[MAX_PATH], dir[MAX_PATH]; HANDLE f; DWORD rd, wr; int X, Y; BOOL loaded = FALSE;
    if (!ReadBmpRGB(path, rgb)) return FALSE;
    ShipSkin_OverlayPath(ov); ShipSkin_OverlayDir(dir); CreateDirectoryW(dir, NULL);
    f = CreateFileW(ov, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { loaded = ReadFile(f, atlasBuf, ATLAS_LEN, &rd, NULL) && rd == ATLAS_LEN; CloseHandle(f); }
    if (!loaded) memcpy(atlasBuf, live, ATLAS_LEN);
    for (Y = 0; Y < SHEET_H; Y++) for (X = 0; X < SHEET_W; X++) {
        int off = GidOff(X, Y);
        BYTE R = rgb[(Y*SHEET_W+X)*3+0], G = rgb[(Y*SHEET_W+X)*3+1], B = rgb[(Y*SHEET_W+X)*3+2];
        atlasBuf[gid*GID_LEN + off] = (R==255 && G==0 && B==255) ? 0 : NearestIdx(R,G,B);
    }
    f = CreateFileW(ov, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, atlasBuf, ATLAS_LEN, &wr, NULL); CloseHandle(f); return TRUE; }
    return FALSE;
}

// ---- 스킨 가져오기(fb5-6): 파일 다이얼로그로 외부 BMP 가져와 적용 + 라이브러리(skins\) 보관 ----
static void DoImportSkin(HWND h, int gid)
{
    OPENFILENAMEW ofn; wchar_t file[MAX_PATH], skins[MAX_PATH], dir[MAX_PATH], dest[MAX_PATH], msg[320];
    const wchar_t* p; const wchar_t* nm;
    ShipSkin_OverlayDir(dir); wsprintfW(skins, L"%s\\skins", dir); CreateDirectoryW(dir, NULL); CreateDirectoryW(skins, NULL);
    file[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
    ofn.lpstrFilter = L"배 스킨 BMP (384x48)\0*.bmp\0모든 파일\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrInitialDir = skins;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"배 스킨 가져오기 (384x48 BMP, 8방향)";
    if (!GetOpenFileNameW(&ofn)) return;
    if (!ApplySkinBmp(gid, file)) { MessageBoxW(h, L"BMP를 읽을 수 없습니다. 384×48 · 24/32비트인지 확인하세요.", L"스킨 가져오기 실패", MB_OK|MB_ICONWARNING); return; }
    nm = file; for (p = file; *p; p++) if (*p == L'\\' || *p == L'/') nm = p + 1;
    wsprintfW(dest, L"%s\\%s", skins, nm);
    if (lstrcmpiW(dest, file) != 0) CopyFileW(file, dest, FALSE);
    wsprintfW(msg, L"[%s] 슬롯에 스킨 「%s」을 적용했습니다.\n재시작 시 반영. (skins\\ 에 보관 — [변경]에서 다시 선택 가능)", kForms[gid].label, nm);
    MessageBoxW(h, msg, L"스킨 가져오기", MB_OK|MB_ICONINFORMATION);
}

// ---- sp5-10: 스킨 목록 피커 (배별 [변경] → 라이브러리 스킨 썸네일 목록에서 골라 적용) ----
#define WC_PICK    L"ShipSkinKR_Picker"
#define MAX_SKINS  64
#define PK_COL     4
#define PK_CW      118
#define PK_CH      88
#define PK_PAD     10
#define PK_TITLE   24
#define PK_THUMB   64
static HWND    g_pick = NULL, g_pickMain = NULL;
static int     g_pickGid = -1, g_skinCount = 0;
static wchar_t g_skinPaths[MAX_SKINS][MAX_PATH];

static void EnumSkinLib(void)
{
    wchar_t skins[MAX_PATH], pat[MAX_PATH], dir[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE hf;
    g_skinCount = 0;
    ShipSkin_OverlayDir(dir); wsprintfW(skins, L"%s\\skins", dir); wsprintfW(pat, L"%s\\*.bmp", skins);
    hf = FindFirstFileW(pat, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && g_skinCount < MAX_SKINS)
                 { wsprintfW(g_skinPaths[g_skinCount], L"%s\\%s", skins, fd.cFileName); g_skinCount++; }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
}

// 스킨 BMP의 dir2(동향) 프레임을 (x,y) size×size 썸네일로.
static void DrawSkinThumb(HDC dc, int x, int y, int size, const wchar_t* path, COLORREF bg)
{
    static BYTE rgb[SHEET_W*SHEET_H*3]; static BYTE cell[FW*FH*3];
    BITMAPINFO bi; int fx, fy; BYTE br=GetRValue(bg), bgc=GetGValue(bg), bb=GetBValue(bg);
    RECT rr; rr.left=x; rr.top=y; rr.right=x+size; rr.bottom=y+size;
    { HBRUSH b=CreateSolidBrush(bg); FillRect(dc,&rr,b); DeleteObject(b); }
    if (!ReadBmpRGB(path, rgb)) return;
    for (fy=0; fy<FH; fy++) for (fx=0; fx<FW; fx++) {
        int sx = 2*FW + fx;   // dir2
        BYTE R=rgb[(fy*SHEET_W+sx)*3+0], G=rgb[(fy*SHEET_W+sx)*3+1], B=rgb[(fy*SHEET_W+sx)*3+2];
        if (R==255&&G==0&&B==255) { cell[(fy*FW+fx)*3+0]=bb; cell[(fy*FW+fx)*3+1]=bgc; cell[(fy*FW+fx)*3+2]=br; }
        else { cell[(fy*FW+fx)*3+0]=B; cell[(fy*FW+fx)*3+1]=G; cell[(fy*FW+fx)*3+2]=R; }
    }
    ZeroMemory(&bi,sizeof(bi)); bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=FW; bi.bmiHeader.biHeight=-FH; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=24; bi.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(dc,COLORONCOLOR);
    StretchDIBits(dc,x,y,size,size,0,0,FW,FH,cell,&bi,DIB_RGB_COLORS,SRCCOPY);
}

// 배별 원본 복원: vanilla.bin(훅이 저장) 의 gid 영역을 오버레이 ship_atlas.bin 의 gid 영역에 덮음.
static BOOL RevertGidToVanilla(int gid)
{
    const BYTE* live = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA;
    static BYTE atlasBuf[ATLAS_LEN]; static BYTE vanilla[ATLAS_LEN];
    wchar_t ov[MAX_PATH], vp[MAX_PATH], dir[MAX_PATH]; HANDLE f; DWORD rd, wr; BOOL haveV=FALSE, loaded=FALSE;
    ShipSkin_OverlayDir(dir); wsprintfW(vp, L"%s\\vanilla.bin", dir);
    f = CreateFileW(vp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { haveV = ReadFile(f, vanilla, ATLAS_LEN, &rd, NULL) && rd==ATLAS_LEN; CloseHandle(f); }
    if (!haveV) return FALSE;
    ShipSkin_OverlayPath(ov);
    f = CreateFileW(ov, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { loaded = ReadFile(f, atlasBuf, ATLAS_LEN, &rd, NULL) && rd==ATLAS_LEN; CloseHandle(f); }
    if (!loaded) memcpy(atlasBuf, live, ATLAS_LEN);
    memcpy(atlasBuf + gid*GID_LEN, vanilla + gid*GID_LEN, GID_LEN);
    f = CreateFileW(ov, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, atlasBuf, ATLAS_LEN, &wr, NULL); CloseHandle(f); return TRUE; }
    return FALSE;
}

// 피커 타일: [0]=원본(바닐라), [1..N]=스킨, [N+1]=+새 스킨 추가
static int PkTiles(void){ return g_skinCount + 2; }
static RECT PkCell(int i){ RECT r; int col=i%PK_COL, row=i/PK_COL; r.left=PK_PAD+col*PK_CW; r.top=PK_PAD+PK_TITLE+row*PK_CH; r.right=r.left+PK_CW; r.bottom=r.top+PK_CH; return r; }
static RECT PkClose(int w){ RECT r; r.right=w-6; r.left=r.right-20; r.top=WFRAME+2; r.bottom=r.top+18; return r; }

static LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT rc, tb; HBRUSH b; HFONT of; int i;
        GetClientRect(h,&rc);
        b=CreateSolidBrush(COL_BG); FillRect(dc,&rc,b); DeleteObject(b);
        b=CreateSolidBrush(COL_LINE); FrameRect(dc,&rc,b); DeleteObject(b);
        tb.left=WFRAME;tb.top=WFRAME;tb.right=rc.right-WFRAME;tb.bottom=WFRAME+PK_TITLE;
        b=CreateSolidBrush(COL_TITLE); FillRect(dc,&tb,b); DeleteObject(b);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,COL_TEXT); of=(HFONT)SelectObject(dc,g_font);
        { RECT tr=tb; tr.left+=8; wchar_t t[80]; wsprintfW(t, L"[%s] 스킨 선택", (g_pickGid>=0&&g_pickGid<NGID)?kForms[g_pickGid].label:L""); DrawTextW(dc,t,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
        { RECT cb=PkClose(rc.right); DrawBtn(dc,cb,L"×"); }
        for (i=0;i<PkTiles();i++) {
            RECT c=PkCell(i), ir=c; int tx=c.left+(PK_CW-PK_THUMB)/2, ty=c.top+4;
            InflateRect(&ir,-3,-3);
            { HBRUSH bb2=CreateSolidBrush(COL_PANEL); FillRect(dc,&ir,bb2); DeleteObject(bb2);
              bb2=CreateSolidBrush(COL_LINE); FrameRect(dc,&ir,bb2); DeleteObject(bb2); }
            if (i==0) { RECT tr=ir; tr.top+=18; SetTextColor(dc,COL_SEL); DrawTextW(dc,L"원본\n(바닐라로)",-1,&tr,DT_CENTER); }
            else if (i==PkTiles()-1) { RECT tr=ir; tr.top+=18; SetTextColor(dc,COL_TEXT); DrawTextW(dc,L"＋\n새 스킨 추가",-1,&tr,DT_CENTER); }
            else {
                const wchar_t* path=g_skinPaths[i-1]; const wchar_t* nm=path; const wchar_t* p; for(p=path;*p;p++) if(*p==L'\\')nm=p+1;
                DrawSkinThumb(dc,tx,ty,PK_THUMB,path,COL_PANEL);
                { RECT nr; nr.left=c.left+2; nr.right=c.right-2; nr.top=ty+PK_THUMB+1; nr.bottom=c.bottom-2;
                  SetTextColor(dc,COL_TEXT); DrawTextW(dc,nm,-1,&nr,DT_CENTER|DT_SINGLELINE|DT_PATH_ELLIPSIS|DT_NOPREFIX); }
            }
        }
        SelectObject(dc,of); EndPaint(h,&ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt; RECT rc; int i; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp); GetClientRect(h,&rc);
        { RECT cb=PkClose(rc.right); if(PtInRect(&cb,pt)){DestroyWindow(h);return 0;} }
        for (i=0;i<PkTiles();i++){ RECT c=PkCell(i); if(PtInRect(&c,pt)){
            HWND mainw = g_pickMain; int gid = g_pickGid;
            if (i==PkTiles()-1) {   // + 새 스킨 추가 → 파일에서 가져오기
                DestroyWindow(h);
                DoImportSkin(mainw, gid);
                if (mainw) { LoadAtlasCache(); InvalidateRect(mainw, NULL, FALSE); }
                return 0;
            }
            if (i==0) {   // 원본(바닐라)로
                BOOL ok = RevertGidToVanilla(gid);
                DestroyWindow(h);
                if (ok && mainw) { LoadAtlasCache(); InvalidateRect(mainw, NULL, FALSE);
                    MessageBoxW(mainw, L"원본(바닐라)으로 되돌립니다.\n게임을 재시작하면 반영됩니다.", L"원본 복원", MB_OK|MB_ICONINFORMATION); }
                else if (!ok) MessageBoxW(mainw, L"바닐라 스냅샷이 아직 없습니다.\n게임을 정상 실행(해상 진입 1회)하면 vanilla.bin 이 생겨 이 기능이 활성화됩니다.", L"원본 복원", MB_OK|MB_ICONWARNING);
                return 0;
            }
            { BOOL ok = ApplySkinBmp(gid, g_skinPaths[i-1]);   // 스킨 선택
              DestroyWindow(h);
              if (ok && mainw) { LoadAtlasCache(); InvalidateRect(mainw, NULL, FALSE);
                  MessageBoxW(mainw, L"스킨을 적용했습니다.\n게임을 재시작하면 반영됩니다.", L"스킨 선택", MB_OK|MB_ICONINFORMATION); }
              return 0; }
        } }
        if (pt.y<WFRAME+PK_TITLE){ ReleaseCapture(); SendMessageW(h,WM_NCLBUTTONDOWN,HTCAPTION,0); }
        return 0;
    }
    case WM_KEYDOWN: if(wp==VK_ESCAPE) DestroyWindow(h); return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_pick=NULL; return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}

static void ShowPicker(HWND owner, int gid)
{
    static BOOL reg=FALSE; int rows, W, H, x=CW_USEDEFAULT, y=CW_USEDEFAULT; RECT orc;
    EnumSkinLib();
    g_pickGid=gid; g_pickMain=owner;
    if (g_pick) DestroyWindow(g_pick);
    if (!reg) { WNDCLASSW wc; ZeroMemory(&wc,sizeof(wc)); wc.lpfnWndProc=PickProc; wc.hInstance=g_hinst; wc.lpszClassName=WC_PICK; wc.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW); wc.hbrBackground=NULL; RegisterClassW(&wc); reg=TRUE; }
    rows=(PkTiles()+PK_COL-1)/PK_COL; if(rows<1)rows=1;
    W=PK_PAD*2+PK_COL*PK_CW; H=PK_PAD*2+PK_TITLE+rows*PK_CH;
    if (owner && GetWindowRect(owner,&orc)) { x=orc.left+40; y=orc.top+40; if(x<0)x=0; if(y<0)y=0; }
    g_pick=CreateWindowExW(0,WC_PICK,L"스킨 선택",WS_POPUP,x,y,W,H,owner,NULL,g_hinst,NULL);
    if (g_pick){ ShowWindow(g_pick,SW_SHOW); UpdateWindow(g_pick); SetFocus(g_pick); }
}

static void SelectForm(HWND h, int gid)
{
    g_sel = gid;
    WriteShipType(kForms[gid].type);
    InvalidateRect(h, NULL, FALSE);
}

// fb5-2: 원래대로 = 편집 전 바닐라로. 오버레이 삭제(재시작 시 게임이 원본 재디코드) + 형태 스킨 해제.
static void DoRevert(HWND h)
{
    wchar_t ov[MAX_PATH];
    ShipSkin_OverlayPath(ov);
    DeleteFileW(ov);            // ship_atlas.bin 제거 → 다음 실행 시 바닐라
    WriteShipType(-1);          // 형태(getter 리다이렉트) 스킨도 끔
    g_sel = -1;
    InvalidateRect(h, NULL, FALSE);
    MessageBoxW(h,
        L"편집 전 원래(바닐라) 배로 되돌립니다.\n"
        L"게임을 재시작하면 원본 이미지로 나옵니다.\n"
        L"(내보낸 ship_atlas.bmp 는 남겨둡니다 — 잘못 편집해도 언제든 이 버튼으로 원복.)",
        L"원래대로 (바닐라 복원)", MB_OK | MB_ICONINFORMATION);
}

// fb5-2: 클릭 가능한 요소(닫기·버튼·배 그리드) 위인지 → 손가락 커서 판정
static BOOL OverClickable(HWND h)
{
    POINT pt; RECT r; int i;
    GetCursorPos(&pt); ScreenToClient(h, &pt);
    r = CloseRect(); if (PtInRect(&r, pt)) return TRUE;
    for (i = 0; i < NBTN; i++) { r = BtnRect(i); if (PtInRect(&r, pt)) return TRUE; }
    if (pt.x >= WFRAME && pt.x < GX + NDIR*PITCH && pt.y >= GY && pt.y < GY + GRID_H) return TRUE;
    return FALSE;
}

static LRESULT CALLBACK ShipProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE:
        g_font = CreateFontW(-13,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
        g_hand  = LoadCursorW(NULL, (LPCWSTR)IDC_HAND);
        g_arrow = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        LoadAtlasCache();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(OverClickable(h) ? g_hand : g_arrow); return TRUE; }
        return DefWindowProcW(h, m, wp, lp);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt; int gid; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp);
        { RECT cb=CloseRect(); if (PtInRect(&cb,pt)) { DestroyWindow(h); return 0; } }
        { RECT b0=BtnRect(0); if (PtInRect(&b0,pt)) { if (g_sel<0) NeedSelect(h); else DoExport(h, g_sel); return 0; } }
        { RECT b1=BtnRect(1); if (PtInRect(&b1,pt)) { if (g_sel<0) NeedSelect(h); else { DoImport(h, g_sel); LoadAtlasCache(); InvalidateRect(h,NULL,FALSE); } return 0; } }
        { RECT b2=BtnRect(2); if (PtInRect(&b2,pt)) { DoRevert(h); LoadAtlasCache(); InvalidateRect(h,NULL,FALSE); return 0; } }
        // 그리드(배) 클릭 → 형태 선택 / [변경] 버튼 → 스킨 목록 피커
        if (pt.x >= WFRAME && pt.x < GX+NDIR*PITCH && pt.y >= GY && pt.y < GY+GRID_H) {
            gid = (pt.y - GY) / PITCH;
            if (gid >= 0 && gid < NGID) {
                RECT cr = RowChangeRect(gid);
                if (PtInRect(&cr, pt)) { g_sel = gid; InvalidateRect(h, NULL, FALSE); ShowPicker(h, gid); return 0; }
                SelectForm(h, gid); return 0;
            }
        }
        if (pt.y < WFRAME+TITLE_H) { ReleaseCapture(); SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
        return 0;
    }
    case WM_KEYDOWN: if (wp == VK_ESCAPE) { DestroyWindow(h); } return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        g_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void ShipWin_Show(HWND owner, HINSTANCE hinst)
{
    static BOOL reg = FALSE;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT; RECT orc;
    g_hinst = hinst;
    if (g_wnd) { SetForegroundWindow(g_wnd); return; }
    if (!reg) {
        WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = ShipProc; wc.hInstance = hinst; wc.lpszClassName = WC_SHIP;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = NULL;
        RegisterClassW(&wc); reg = TRUE;
    }
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right-orc.left)-WIN_W)/2;
        y = orc.top  + ((orc.bottom-orc.top)-WIN_H)/2;
        if (x < 0) x = 0; if (y < 0) y = 0;
    }
    g_wnd = CreateWindowExW(0, WC_SHIP, L"함선 스프라이트", WS_POPUP, x, y, WIN_W, WIN_H, owner, NULL, hinst, NULL);
    if (g_wnd) { ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd); SetFocus(g_wnd); }
}
