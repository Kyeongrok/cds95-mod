#include "dialog.h"

// 여관 숙박일수 입력 계산기 — 게임 분위기에 맞춘 고풍스러운(갈색 엠보싱/serif) 오너드로우
// 모달 다이얼로그. 리소스 없이 in-memory 템플릿 + WM_DRAWITEM 커스텀 페인팅.
//
//   [           0 ]   (표시창, 우측정렬 serif italic)
//   7   8   9   AC
//   4   5   6   DEL
//   1   2   3   MAX
//   0   00  000 MIN
//   [   ENTER   ] CANCEL
//
// 게임 내장 계산기(0x482xxx)는 프레임 루프에 물린 대형 화면 클래스라 동기 훅에서 재사용이
// 어려워, 같은 룩앤필을 자체 모달로 근사 재현한다.

#define IDC_DISPLAY 100
#define ID_DIGIT0   200   // 200+digit
#define ID_AC       220
#define ID_MAX      221
#define ID_MIN      222
#define ID_00       224
#define ID_000      225
#define ID_DEL      226
// ENTER=IDOK(1), CANCEL/닫기=IDCANCEL(2)

#define DAY_MIN 1
#define DAY_MAX 127

static int   g_val = 0;
static BOOL  g_fresh = TRUE;
static HFONT g_btnFont = NULL;
static HFONT g_dispFont = NULL;

// --- 팔레트 (fb23: 게임(sc04/sc05)풍으로 더 밝은 베이지/탄) ---
#define COL_BG        RGB(196,180,152)
#define COL_FACE_TOP  RGB(234,224,204)
#define COL_FACE_BOT  RGB(206,190,164)
#define COL_FACE_TOP_P RGB(200,184,158)
#define COL_FACE_BOT_P RGB(168,150,124)
#define COL_LIGHT     RGB(248,242,228)
#define COL_DARK      RGB(120,100, 78)
#define COL_TEXT      RGB( 60, 44, 28)
#define COL_DISP_BG   RGB(230,222,202)
// 취소(CANCEL) 버튼은 좀 더 진한 갈색 (sc05: 맨 아래 취소가 더 진함)
#define COL_CAN_TOP   RGB(176,150,118)
#define COL_CAN_BOT   RGB(140,114, 86)

static int ClampDays(int v) { if (v < DAY_MIN) v = DAY_MIN; if (v > DAY_MAX) v = DAY_MAX; return v; }

static void VGradient(HDC dc, RECT r, COLORREF top, COLORREF bot)
{
    int h = r.bottom - r.top;
    int i;
    if (h <= 0) return;
    for (i = 0; i < h; i++)
    {
        int rr = GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * i / h;
        int gg = GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * i / h;
        int bb = GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * i / h;
        RECT line; HBRUSH br = CreateSolidBrush(RGB(rr, gg, bb));
        line.left = r.left; line.right = r.right; line.top = r.top + i; line.bottom = r.top + i + 1;
        FillRect(dc, &line, br); DeleteObject(br);
    }
}

static void Bevel(HDC dc, RECT r, BOOL sunken)
{
    COLORREF lt = sunken ? COL_DARK : COL_LIGHT;
    COLORREF dk = sunken ? COL_LIGHT : COL_DARK;
    HPEN pl = CreatePen(PS_SOLID, 1, lt), pd = CreatePen(PS_SOLID, 1, dk);
    HPEN old = (HPEN)SelectObject(dc, pl);
    MoveToEx(dc, r.left, r.bottom - 1, NULL);
    LineTo(dc, r.left, r.top); LineTo(dc, r.right - 1, r.top);
    SelectObject(dc, pd);
    LineTo(dc, r.right - 1, r.bottom - 1); LineTo(dc, r.left, r.bottom - 1);
    SelectObject(dc, old); DeleteObject(pl); DeleteObject(pd);
}

static void DrawButton(LPDRAWITEMSTRUCT di)
{
    HDC dc = di->hDC; RECT r = di->rcItem, face, m, o;
    BOOL pressed = (di->itemState & ODS_SELECTED) != 0;
    BOOL isCancel = (di->CtlID == IDCANCEL);
    WCHAR t[24]; HFONT of; HBRUSH br; int th, oh;
    // 패딩 배경(갈색) + 진한 갈색 외곽 테두리 (액자 느낌)
    br = CreateSolidBrush(COL_BG);   FillRect(dc, &r, br);  DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &r, br); DeleteObject(br);
    // 엠보싱 면 — 테두리 안쪽으로 패딩만큼 들여서. 취소는 더 진한 갈색.
    face = r; InflateRect(&face, -3, -3);
    VGradient(dc, face,
              pressed ? COL_FACE_TOP_P : (isCancel ? COL_CAN_TOP : COL_FACE_TOP),
              pressed ? COL_FACE_BOT_P : (isCancel ? COL_CAN_BOT : COL_FACE_BOT));
    Bevel(dc, face, pressed);
    // 텍스트 (멀티라인 세로 중앙정렬 — "CAN\nCEL" 대응)
    GetWindowTextW(di->hwndItem, t, 24);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, isCancel ? COL_LIGHT : COL_TEXT);
    of = (HFONT)SelectObject(dc, g_btnFont);
    m = face; m.top = 0; m.bottom = 0;
    th = DrawTextW(dc, t, -1, &m, DT_CENTER | DT_CALCRECT);
    o = face; oh = o.bottom - o.top; o.top += (oh - th) / 2;
    if (pressed) { o.left++; o.top++; }
    DrawTextW(dc, t, -1, &o, DT_CENTER);
    SelectObject(dc, of);
}

static void DrawDisplay(LPDRAWITEMSTRUCT di)
{
    HDC dc = di->hDC; RECT r = di->rcItem, tr;
    WCHAR t[16]; HFONT of; HBRUSH br;
    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &r, br); DeleteObject(br);
    Bevel(dc, r, TRUE);
    wsprintfW(t, L"%d", g_val);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, COL_TEXT);
    of = (HFONT)SelectObject(dc, g_dispFont);
    tr = r; tr.right -= 10;
    DrawTextW(dc, t, -1, &tr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        g_val = (int)lp; g_fresh = TRUE;
        g_btnFont  = CreateFontW(-12, 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
                                 DEFAULT_CHARSET, 0, 0, 0, 0, L"Times New Roman");
        g_dispFont = CreateFontW(-24, 0, 0, 0, FW_BOLD, TRUE, FALSE, FALSE,
                                 DEFAULT_CHARSET, 0, 0, 0, 0, L"Times New Roman");
        return TRUE;

    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wp; RECT rc, r2; HBRUSH br;
        GetClientRect(hDlg, &rc);
        br = CreateSolidBrush(COL_BG); FillRect(dc, &rc, br); DeleteObject(br);
        // 갈색 테두리: 바깥쪽 진한 갈색 1px + 이중 베벨(입체감) → 두툼한 갈색 프레임
        br = CreateSolidBrush(COL_DARK); FrameRect(dc, &rc, br); DeleteObject(br);
        r2 = rc; InflateRect(&r2, -1, -1); Bevel(dc, r2, FALSE);
        InflateRect(&r2, -1, -1); Bevel(dc, r2, FALSE);
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lp;
        if (di->CtlType == ODT_BUTTON) DrawButton(di);
        else if (di->CtlType == ODT_STATIC) DrawDisplay(di);
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        if (id >= ID_DIGIT0 && id <= ID_DIGIT0 + 9)
        {
            int d = id - ID_DIGIT0;
            if (g_fresh) { g_val = d; g_fresh = FALSE; }
            else         { g_val = g_val * 10 + d; }
            if (g_val > DAY_MAX) g_val = DAY_MAX;
        }
        else switch (id)
        {
        case ID_00:  if (g_fresh) { g_val = 0; g_fresh = FALSE; } else g_val *= 100;  if (g_val > DAY_MAX) g_val = DAY_MAX; break;
        case ID_000: if (g_fresh) { g_val = 0; g_fresh = FALSE; } else g_val *= 1000; if (g_val > DAY_MAX) g_val = DAY_MAX; break;
        case ID_DEL: g_val /= 10; g_fresh = FALSE; break;
        case ID_AC:  g_val = 0;       g_fresh = TRUE; break;
        case ID_MAX: g_val = DAY_MAX; g_fresh = TRUE; break;
        case ID_MIN: g_val = DAY_MIN; g_fresh = TRUE; break;
        case IDOK:     EndDialog(hDlg, ClampDays(g_val)); return TRUE;
        case IDCANCEL: EndDialog(hDlg, 0);                return TRUE;
        default: return FALSE;
        }
        InvalidateRect(GetDlgItem(hDlg, IDC_DISPLAY), NULL, FALSE); // 표시창만 갱신
        return TRUE;
    }

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;

    case WM_DESTROY:
        if (g_btnFont)  { DeleteObject(g_btnFont);  g_btnFont = NULL; }
        if (g_dispFont) { DeleteObject(g_dispFont); g_dispFont = NULL; }
        return FALSE;
    }
    return FALSE;
}

// --- in-memory DLGTEMPLATE 빌더 ---
static void PutW(BYTE** p, WORD w)   { *(WORD*)(*p) = w;  *p += 2; }
static void PutDW(BYTE** p, DWORD d) { *(DWORD*)(*p) = d; *p += 4; }
static void PutStr(BYTE** p, const WCHAR* s) { while (*s) PutW(p, (WORD)*s++); PutW(p, 0); }
static void AlignDW(BYTE** p, BYTE* base) { while (((SIZE_T)(*p - base)) & 3) *(*p)++ = 0; }

static void AddCtl(BYTE** p, BYTE* base, DWORD style, int x, int y, int cx, int cy,
                   WORD id, WORD atom, const WCHAR* text)
{
    AlignDW(p, base);
    PutDW(p, WS_CHILD | WS_VISIBLE | style);
    PutDW(p, 0);
    PutW(p, (WORD)x); PutW(p, (WORD)y); PutW(p, (WORD)cx); PutW(p, (WORD)cy);
    PutW(p, id);
    PutW(p, 0xFFFF); PutW(p, atom);
    PutStr(p, text);
    PutW(p, 0);
}
static void AddBtn(BYTE** p, BYTE* base, int x, int y, int cx, int cy, WORD id, const WCHAR* t)
{
    AddCtl(p, base, WS_TABSTOP | BS_OWNERDRAW, x, y, cx, cy, id, 0x0080, t);
}

int HotelKR_AskDays(int defaultDays)
{
    BYTE buf[2048];
    BYTE* base = buf;
    BYTE* p = buf;
    const int GX[4] = { 6, 34, 62, 90 };
    const int BW = 26, BH = 18;
    int r1 = 38, r2 = 60, r3 = 82, r4 = 104, r5 = 126;

    // DLGTEMPLATE 헤더 (시스템 프레임 없음 — 갈색 테두리는 WM_ERASEBKGND에서 직접, 화면 중앙)
    PutDW(&p, WS_POPUP | DS_CENTER | DS_SETFONT);
    PutDW(&p, 0);
    PutW(&p, 19);                 // 컨트롤 19개 (표시창1 + 버튼18)
    PutW(&p, 0); PutW(&p, 0);
    PutW(&p, 122); PutW(&p, 152); // cx, cy
    PutW(&p, 0); PutW(&p, 0);     // menu, class 기본
    PutStr(&p, L"숙박일수");
    PutW(&p, 9);
    PutStr(&p, L"맑은 고딕");

    // 표시창 (오너드로우 STATIC)
    AddCtl(&p, base, SS_OWNERDRAW, 6, 6, 110, 26, IDC_DISPLAY, 0x0082, L"");

    // Row1: 7 8 9 AC
    AddBtn(&p, base, GX[0], r1, BW, BH, ID_DIGIT0 + 7, L"7");
    AddBtn(&p, base, GX[1], r1, BW, BH, ID_DIGIT0 + 8, L"8");
    AddBtn(&p, base, GX[2], r1, BW, BH, ID_DIGIT0 + 9, L"9");
    AddBtn(&p, base, GX[3], r1, BW, BH, ID_AC, L"AC");
    // Row2: 4 5 6 DEL
    AddBtn(&p, base, GX[0], r2, BW, BH, ID_DIGIT0 + 4, L"4");
    AddBtn(&p, base, GX[1], r2, BW, BH, ID_DIGIT0 + 5, L"5");
    AddBtn(&p, base, GX[2], r2, BW, BH, ID_DIGIT0 + 6, L"6");
    AddBtn(&p, base, GX[3], r2, BW, BH, ID_DEL, L"DEL");
    // Row3: 1 2 3 MAX
    AddBtn(&p, base, GX[0], r3, BW, BH, ID_DIGIT0 + 1, L"1");
    AddBtn(&p, base, GX[1], r3, BW, BH, ID_DIGIT0 + 2, L"2");
    AddBtn(&p, base, GX[2], r3, BW, BH, ID_DIGIT0 + 3, L"3");
    AddBtn(&p, base, GX[3], r3, BW, BH, ID_MAX, L"MAX");
    // Row4: 0 00 000 MIN
    AddBtn(&p, base, GX[0], r4, BW, BH, ID_DIGIT0 + 0, L"0");
    AddBtn(&p, base, GX[1], r4, BW, BH, ID_00, L"00");
    AddBtn(&p, base, GX[2], r4, BW, BH, ID_000, L"000");
    AddBtn(&p, base, GX[3], r4, BW, BH, ID_MIN, L"MIN");
    // Row5: ENTER(1~3열) CANCEL(4열)
    AddCtl(&p, base, WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
           GX[0], r5, (GX[2] + BW) - GX[0], 20, IDOK, 0x0080, L"ENTER");
    AddBtn(&p, base, GX[3], r5, BW, 20, IDCANCEL, L"CAN\nCEL");

    {
        HWND parent = GetActiveWindow();
        INT_PTR rr = DialogBoxIndirectParamW(GetModuleHandleW(NULL),
                                             (LPCDLGTEMPLATEW)buf, parent, DlgProc,
                                             (LPARAM)defaultDays);
        if (rr <= 0) return defaultDays;
        return (int)rr;
    }
}
