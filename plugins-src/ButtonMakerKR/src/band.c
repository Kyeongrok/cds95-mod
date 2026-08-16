#include "band.h"
#include "gamefont.h"
#include "game_palette.h"   // CharacterUtilKR/src — kGamePalette[768] (R,G,B 차례)

int Band_Width(int cells)
{
    if (cells < 1) cells = 1;
    if (cells > BAND_MAX_CELLS) cells = BAND_MAX_CELLS;
    return SKIN_CAP_W * 2 + SKIN_MID_W * cells;
}

int Band_AutoCells(const wchar_t* text, int margin)
{
    int need = GameFont_TextWidth(text) + margin * 2 - SKIN_CAP_W * 2;
    int cells;
    if (need < 0) need = 0;
    cells = (need + SKIN_MID_W - 1) / SKIN_MID_W;
    if (cells < 1) cells = 1;
    if (cells > BAND_MAX_CELLS) cells = BAND_MAX_CELLS;
    return cells;
}

// 조각 하나를 x 자리에 세로로 통째 옮긴다.
static void Blit(unsigned char* idx, int bw, int x, const unsigned char* src, int sw)
{
    int r, c;
    for (r = 0; r < SKIN_H; r++)
        for (c = 0; c < sw; c++)
            idx[r * bw + x + c] = src[r * sw + c];
}

static void PutGlyph(unsigned char* idx, int bw, int x, int y,
                     const unsigned char* mask, int gw, int gh, unsigned char color)
{
    int r, c;
    for (r = 0; r < gh; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= SKIN_H) continue;
        for (c = 0; c < gw; c++) {
            int xx = x + c;
            if (xx < 0 || xx >= bw) continue;
            if (mask[r * GF_MAX_W + c]) idx[yy * bw + xx] = color;
        }
    }
}

int Band_Build(int style, const wchar_t* text, int cells,
               unsigned char color, int shadow, unsigned char shadowColor,
               unsigned char* idx)
{
    const unsigned char *left, *mid, *right;
    unsigned char mask[GF_MAX_W * GF_MAX_H];
    int lw = 0, mw = 0, rw = 0, bw, x, i, pass;
    const wchar_t* p;

    if (!MiscSkin_Ready()) return 0;
    left  = MiscSkin_Piece(style, 0, &lw);
    mid   = MiscSkin_Piece(style, 1, &mw);
    right = MiscSkin_Piece(style, 2, &rw);
    if (!left || !mid || !right) return 0;

    if (cells < 1) cells = 1;
    if (cells > BAND_MAX_CELLS) cells = BAND_MAX_CELLS;
    bw = Band_Width(cells);

    Blit(idx, bw, 0, left, lw);
    for (i = 0; i < cells; i++) Blit(idx, bw, lw + i * mw, mid, mw);
    Blit(idx, bw, bw - rw, right, rw);

    if (!text || !*text || !GameFont_Ready()) return bw;

    // 그림자를 먼저 깔고 그 위에 본 글자를 찍는다(획이 겹쳐도 본 글자가 이긴다).
    for (pass = shadow ? 0 : 1; pass < 2; pass++) {
        int tw = GameFont_TextWidth(text);
        int off = pass == 0 ? 1 : 0;
        unsigned char col = pass == 0 ? shadowColor : color;
        x = (bw - tw) / 2;
        if (x < 0) x = 0;
        for (p = text; *p; p++) {
            int gw, gh;
            if (!GameFont_Glyph(*p, mask, &gw, &gh)) continue;
            PutGlyph(idx, bw, x + off, (SKIN_H - gh) / 2 + off, mask, gw, gh, col);
            x += gw;
        }
    }
    return bw;
}

void Band_ToBgr(const unsigned char* idx, int w, int h, unsigned char* bgr)
{
    int i, n = w * h;
    for (i = 0; i < n; i++) {
        unsigned char v = idx[i];
        bgr[i * 3 + 0] = kGamePalette[v * 3 + 2];
        bgr[i * 3 + 1] = kGamePalette[v * 3 + 1];
        bgr[i * 3 + 2] = kGamePalette[v * 3 + 0];
    }
}

void Band_ToRgb(const unsigned char* idx, int w, int h, unsigned char* rgb)
{
    int i, n = w * h;
    for (i = 0; i < n; i++) {
        unsigned char v = idx[i];
        rgb[i * 3 + 0] = kGamePalette[v * 3 + 0];
        rgb[i * 3 + 1] = kGamePalette[v * 3 + 1];
        rgb[i * 3 + 2] = kGamePalette[v * 3 + 2];
    }
}
