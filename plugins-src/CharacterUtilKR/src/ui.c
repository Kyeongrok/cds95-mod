#include "ui.h"

HFONT g_font = NULL;
HFONT g_smallFont = NULL;

void UI_CreateFonts(void)
{
    if (!g_font)
        g_font = CreateFontW(-14,0,0,0,FW_BOLD, FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
    if (!g_smallFont)
        g_smallFont = CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"바탕");
}

void UI_DestroyFonts(void)
{
    if (g_font)      { DeleteObject(g_font);      g_font = NULL; }
    if (g_smallFont) { DeleteObject(g_smallFont); g_smallFont = NULL; }
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

void UI_Text(HDC dc, RECT r, const wchar_t* t, HFONT f, COLORREF c, UINT fmt)
{
    HFONT of = (HFONT)SelectObject(dc, f ? f : g_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, t, -1, &r, fmt);
    SelectObject(dc, of);
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
