#include "uikit.h"

HFONT g_font = NULL;
HFONT g_smallFont = NULL;

void UI_CreateFonts(void)
{
    if (!g_font)
        g_font = CreateFontW(-14,0,0,0,FW_BOLD, FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
    if (!g_smallFont)
        g_smallFont = CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
}

// 이중 버퍼용 메모리 DC. 크기가 그대로면 계속 다시 쓴다 —
// 끌어서 움직이는 동안 매 프레임 비트맵을 새로 만들면 그 자체가 부담이다.
static HDC     g_bufDc = NULL;
static HBITMAP g_bufBmp = NULL, g_bufOld = NULL;
static int     g_bufW = 0, g_bufH = 0;

static void FreeBuffer(void)
{
    if (g_bufDc) {
        if (g_bufOld) SelectObject(g_bufDc, g_bufOld);
        DeleteDC(g_bufDc);
        g_bufDc = NULL; g_bufOld = NULL;
    }
    if (g_bufBmp) { DeleteObject(g_bufBmp); g_bufBmp = NULL; }
    g_bufW = g_bufH = 0;
}

HDC UI_BufBegin(UiBuf* b, HDC target, int w, int h)
{
    b->target = target; b->mem = NULL; b->w = w; b->h = h;
    if (w <= 0 || h <= 0) return target;

    if (!g_bufDc || g_bufW != w || g_bufH != h) {
        FreeBuffer();
        g_bufDc = CreateCompatibleDC(target);
        if (!g_bufDc) return target;
        g_bufBmp = CreateCompatibleBitmap(target, w, h);
        if (!g_bufBmp) { DeleteDC(g_bufDc); g_bufDc = NULL; return target; }
        g_bufOld = (HBITMAP)SelectObject(g_bufDc, g_bufBmp);
        g_bufW = w; g_bufH = h;
    }
    b->mem = g_bufDc;
    return g_bufDc;
}

void UI_BufEnd(UiBuf* b)
{
    if (b->mem) BitBlt(b->target, 0, 0, b->w, b->h, b->mem, 0, 0, SRCCOPY);
}

void UI_DestroyFonts(void)
{
    if (g_font)      { DeleteObject(g_font);      g_font = NULL; }
    if (g_smallFont) { DeleteObject(g_smallFont); g_smallFont = NULL; }
    FreeBuffer();
}

void UI_VGradient(HDC dc, RECT r, COLORREF top, COLORREF bot)
{
    int h = r.bottom - r.top, i;
    if (h <= 0) return;
    for (i = 0; i < h; i++) {
        int rr = GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * i / h;
        int gg = GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * i / h;
        int bb = GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * i / h;
        RECT ln; HBRUSH br = CreateSolidBrush(RGB(rr, gg, bb));
        ln.left = r.left; ln.right = r.right; ln.top = r.top + i; ln.bottom = r.top + i + 1;
        FillRect(dc, &ln, br); DeleteObject(br);
    }
}

void UI_Bevel(HDC dc, RECT r, BOOL sunken)
{
    COLORREF lt = sunken ? COL_DARK : COL_LIGHT, dk = sunken ? COL_LIGHT : COL_DARK;
    HPEN pl = CreatePen(PS_SOLID,1,lt), pd = CreatePen(PS_SOLID,1,dk);
    HPEN old = (HPEN)SelectObject(dc, pl);
    MoveToEx(dc, r.left, r.bottom-1, NULL);
    LineTo(dc, r.left, r.top); LineTo(dc, r.right-1, r.top);
    SelectObject(dc, pd);
    LineTo(dc, r.right-1, r.bottom-1); LineTo(dc, r.left, r.bottom-1);
    SelectObject(dc, old); DeleteObject(pl); DeleteObject(pd);
}

void UI_Button(HDC dc, RECT r, const wchar_t* t, BOOL active)
{
    HBRUSH br = CreateSolidBrush(COL_BG); FillRect(dc, &r, br); DeleteObject(br);
    br = CreateSolidBrush(COL_TEXT); FrameRect(dc, &r, br); DeleteObject(br);
    { RECT f=r; InflateRect(&f,-2,-2);
      if (active) { HBRUSH b2=CreateSolidBrush(COL_SEL_BG); FillRect(dc,&f,b2); DeleteObject(b2); UI_Bevel(dc,f,TRUE); }
      else        { UI_VGradient(dc,f,COL_FACE_TOP,COL_FACE_BOT); UI_Bevel(dc,f,FALSE); } }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, active?RGB(250,244,228):COL_TEXT);
    { HFONT of=(HFONT)SelectObject(dc, g_font);
      DrawTextW(dc, t, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX); SelectObject(dc, of); }
}

void UI_Select(HDC dc, RECT r, const wchar_t* text, BOOL open)
{
    RECT t = r, a;
    HBRUSH br = CreateSolidBrush(open ? COL_FACE_TOP : COL_DISP_BG);
    FillRect(dc, &r, br); DeleteObject(br);
    UI_Bevel(dc, r, TRUE);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &r, br); DeleteObject(br);

    t.left += 6; t.right -= 20;
    UI_Text(dc, t, text, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);

    a = r; a.left = r.right - 18; a.right = r.right - 2; a.top += 2; a.bottom -= 2;
    UI_VGradient(dc, a, COL_FACE_TOP, COL_FACE_BOT);
    UI_Bevel(dc, a, open);
    UI_Text(dc, a, L"▼", g_smallFont, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
}

void UI_Text(HDC dc, RECT r, const wchar_t* t, HFONT f, COLORREF c, UINT fmt)
{
    HFONT of = (HFONT)SelectObject(dc, f ? f : g_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt);
    SelectObject(dc, of);
}

void UI_WindowFrame(HDC dc, RECT client, const wchar_t* title, RECT* closeOut)
{
    RECT tb, tr, cb;
    HBRUSH br;

    br = CreateSolidBrush(COL_BG);   FillRect(dc, &client, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK); FrameRect(dc, &client, br); DeleteObject(br);

    tb.left = FRAME; tb.top = FRAME; tb.right = client.right - FRAME; tb.bottom = FRAME + TITLE_H;
    UI_VGradient(dc, tb, COL_FACE_TOP, COL_FACE_BOT);
    UI_Bevel(dc, tb, FALSE);
    if (title) {
        tr = tb; tr.left += 8;
        UI_Text(dc, tr, title, g_font, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    cb.right = client.right - FRAME - 4; cb.left = cb.right - 22;
    cb.top = FRAME + 4; cb.bottom = cb.top + 18;
    UI_Button(dc, cb, L"×", FALSE);
    if (closeOut) *closeOut = cb;
}

void UI_Scrollbar(HDC dc, RECT track, int scroll, int maxScroll, int visRows, int totalRows)
{
    HBRUSH br;
    int trackh = track.bottom - track.top;
    int rows = totalRows < 1 ? 1 : totalRows;
    int thumbh, ty;
    RECT th;

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &track, br); DeleteObject(br);
    UI_Bevel(dc, track, TRUE);

    thumbh = trackh * (visRows < rows ? visRows : rows) / rows;
    if (thumbh < 16) thumbh = 16;
    ty = track.top + (maxScroll > 0 ? (trackh - thumbh) * scroll / maxScroll : 0);
    th.left = track.left + 1; th.right = track.right - 1;
    th.top = ty; th.bottom = ty + thumbh;
    UI_VGradient(dc, th, COL_FACE_TOP, COL_FACE_BOT);
    UI_Bevel(dc, th, FALSE);
}
