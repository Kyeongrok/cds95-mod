#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <string.h>
#include "ship_palette.h"   // kFacePalette[768] — 게임 캡처 역산 근사 팔레트(배에도 코히어런트)

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
static RECT BtnRect(int i)   { RECT r; int w=100; r.left=GX + i*(w+8); r.right=r.left+w; r.top=BTN_Y; r.bottom=BTN_Y+BTN_H; return r; }
static RECT RowRect(int gid) { RECT r; r.left=WFRAME+2; r.right=GX-2; r.top=GY+gid*PITCH; r.bottom=r.top+DISP; return r; }
static const wchar_t* kBtn[3] = { L"내보내기", L"불러오기", L"원래대로" };

// ---- 한 프레임 렌더 (gid,dir) → (x,y) DISP×DISP ----
static void DrawFrame(HDC dc, int x, int y, int gid, int dir, COLORREF bg)
{
    const BYTE* atlas = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA;
    const BYTE* fr = atlas + ((SIZE_T)(gid*NDIR + dir)) * FRAME_SZ;
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
    { RECT tr=tb; tr.left+=8; DrawTextW(dc, L"함선 스프라이트  (배를 눌러 형태 선택)", -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
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
        { RECT tr=lr; tr.left+=4; DrawTextW(dc, kForms[gid].label, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE); }
        for (dir = 0; dir < NDIR; dir++)
            DrawFrame(dc, GX + dir*PITCH, GY + gid*PITCH, gid, dir, COL_PANEL);
    }
    // 버튼
    { int i; for (i=0;i<3;i++) DrawBtn(dc, BtnRect(i), kBtn[i]); }
    SelectObject(dc, of);
    EndPaint(h, &ps);
}

// ---- BMP 라운드트립 (그림판 편집용) ----
// 배치: 384×192 = 8방향(가로) × 4종(세로), 각 프레임 48×48 1:1. 마젠타(255,0,255)=투명(인덱스0).
#define SHEET_W   (NDIR*FW)      // 384
#define SHEET_H   (NGID*FH)      // 192
#define ATLAS_LEN 0x12000u

static void ShipFile(wchar_t* out, const wchar_t* name)
{
    wchar_t dir[MAX_PATH];
    ShipSkin_OverlayDir(dir);
    wsprintfW(out, L"%s\\%s", dir, name);
}

// 이미지 (X,Y) → 아틀라스 인덱스 오프셋
static int AtlasOff(int X, int Y)
{
    int gid = Y / FH, dir = X / FW, fx = X % FW, fy = Y % FH;
    return (gid*NDIR + dir)*(int)FRAME_SZ + fy*FW + fx;
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

// 인덱스 아틀라스(0x12000) → 24bpp BMP 파일
static BOOL WriteAtlasBmp(const wchar_t* path, const BYTE* idx)
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
            BYTE R,G,B; IdxToRGB(idx[AtlasOff(X,Y)], &R,&G,&B);
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

// ---- 내보내기: 현재 아틀라스 → ship_atlas.bmp(그림판 편집용) + ship_atlas.src(diff 기준) ----
static void DoExport(HWND h)
{
    const BYTE* atlas = (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA;
    wchar_t dir[MAX_PATH], bmp[MAX_PATH], src[MAX_PATH]; HANDLE f; DWORD wr;
    ShipSkin_OverlayDir(dir); CreateDirectoryW(dir, NULL);
    ShipFile(bmp, L"ship_atlas.bmp");
    ShipFile(src, L"ship_atlas.src");
    // baseline(.src) = 현재 아틀라스 인덱스 (불러오기 때 미편집 픽셀 원본 보존용)
    f = CreateFileW(src, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, atlas, ATLAS_LEN, &wr, NULL); CloseHandle(f); }
    if (!WriteAtlasBmp(bmp, atlas)) { MessageBoxW(h, L"BMP 저장 실패.", L"내보내기", MB_OK|MB_ICONWARNING); return; }
    ShellExecuteW(NULL, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
    MessageBoxW(h,
        L"ship_atlas.bmp 로 내보냈습니다 (384×192, 8방향×4종).\n\n"
        L"그림판 등으로 열어 색/도트를 편집하세요.\n"
        L"  · 분홍색(마젠타)= 투명(바다). 그대로 두세요.\n"
        L"  · 크기(384×192)·24비트 BMP 유지, 다른 이름 저장 금지.\n\n"
        L"편집 후 [불러오기]를 누르면 반영됩니다(재시작 필요).",
        L"내보내기 — 그림판 편집", MB_OK | MB_ICONINFORMATION);
}

// ---- 불러오기: 편집된 ship_atlas.bmp → 오버레이 ship_atlas.bin (미편집 픽셀은 원본 인덱스 보존) ----
static void DoImport(HWND h)
{
    wchar_t bmp[MAX_PATH], src[MAX_PATH], ov[MAX_PATH]; HANDLE f; DWORD rd, wr;
    static BYTE rgb[SHEET_W*SHEET_H*3];
    static BYTE base[ATLAS_LEN];
    static BYTE out[ATLAS_LEN];
    BOOL haveBase; int X, Y;
    ShipFile(bmp, L"ship_atlas.bmp");
    ShipFile(src, L"ship_atlas.src");
    ShipSkin_OverlayPath(ov);   // ...\ship_atlas.bin (훅이 읽는 오버레이)
    if (!ReadBmpRGB(bmp, rgb)) {
        MessageBoxW(h, L"ship_atlas.bmp 를 읽을 수 없습니다.\n먼저 [내보내기]로 만들고, 384×192·24비트 BMP로 저장했는지 확인하세요.",
                    L"불러오기 실패", MB_OK|MB_ICONWARNING);
        return;
    }
    // baseline 로드(.src). 없으면 현재 게임 아틀라스로.
    haveBase = FALSE;
    f = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { haveBase = ReadFile(f, base, ATLAS_LEN, &rd, NULL) && rd == ATLAS_LEN; CloseHandle(f); }
    if (!haveBase) memcpy(base, (const BYTE*)GetModuleHandleW(NULL) + ATLAS_RVA, ATLAS_LEN);
    // 미편집 픽셀 = baseline 인덱스 그대로, 편집 픽셀만 근사 인덱스로.
    for (Y = 0; Y < SHEET_H; Y++) for (X = 0; X < SHEET_W; X++) {
        int off = AtlasOff(X, Y);
        BYTE R = rgb[(Y*SHEET_W+X)*3+0], G = rgb[(Y*SHEET_W+X)*3+1], B = rgb[(Y*SHEET_W+X)*3+2];
        BYTE bR,bG,bB; IdxToRGB(base[off], &bR,&bG,&bB);
        if (R==bR && G==bG && B==bB) out[off] = base[off];              // 미편집 → 원본 보존
        else if (R==255 && G==0 && B==255) out[off] = 0;                // 투명 칠함
        else out[off] = NearestIdx(R,G,B);                             // 편집색 → 근사 인덱스
    }
    ShipSkin_OverlayDir(bmp); CreateDirectoryW(bmp, NULL);              // (bmp 변수 재활용: dir)
    f = CreateFileW(ov, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, out, ATLAS_LEN, &wr, NULL); CloseHandle(f); }
    MessageBoxW(h, L"편집본을 적용했습니다.\n게임을 재시작하면 해상 배 이미지에 반영됩니다.\n(원래대로: 창의 [원래대로]는 형태선택 해제이며, 편집 취소는 shipskin\\ship_atlas.bin 삭제 후 재시작)",
                L"불러오기", MB_OK | MB_ICONINFORMATION);
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
    for (i = 0; i < 3; i++) { r = BtnRect(i); if (PtInRect(&r, pt)) return TRUE; }
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
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(OverClickable(h) ? g_hand : g_arrow); return TRUE; }
        return DefWindowProcW(h, m, wp, lp);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: OnPaint(h); return 0;
    case WM_LBUTTONDOWN: {
        POINT pt; int gid; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp);
        { RECT cb=CloseRect(); if (PtInRect(&cb,pt)) { DestroyWindow(h); return 0; } }
        { RECT b0=BtnRect(0); if (PtInRect(&b0,pt)) { DoExport(h); return 0; } }
        { RECT b1=BtnRect(1); if (PtInRect(&b1,pt)) { DoImport(h); return 0; } }
        { RECT b2=BtnRect(2); if (PtInRect(&b2,pt)) { DoRevert(h); return 0; } }
        // 그리드(배) 클릭 → 형태 선택
        if (pt.x >= WFRAME && pt.x < GX+NDIR*PITCH && pt.y >= GY && pt.y < GY+GRID_H) {
            gid = (pt.y - GY) / PITCH;
            if (gid >= 0 && gid < NGID) { SelectForm(h, gid); return 0; }
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
