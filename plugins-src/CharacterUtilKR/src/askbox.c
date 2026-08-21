#include "askbox.h"
#include "ui.h"
#include "faces.h"       // 말하는 사람 초상화(MALE.CDS / FEMALE.CDS)
#include "gamefont.h"    // ButtonMakerKR/src — 게임 비트맵 글꼴

// 판 크기와 색. 게임 다이얼로그를 눈으로 대조해 맞췄다 —
// 바탕은 짙은 자주, 테두리는 크림 두 줄, 글자는 크림보다 조금 밝은 흰빛이다.
// 폭은 가장 긴 한 줄이 들어갈 만큼 — 게임 글꼴은 한글 한 자가 16픽셀이라 금세 찬다.
// (아래 ask_row_align_check 때문에 글 영역 폭은 4의 배수로 떨어져야 한다.)
#define ASK_W       660
#define ASK_X       ((WIN_W - ASK_W) / 2)

// 초상화는 게임 다이얼로그와 같은 크기로 그린다 — 얼굴 원본(80x96)을 그대로 쓰지 않고
// 조금 줄인다. 오리지널 화면을 2배 확대 캡처해 재 보면 초상화 상자가 142x161 픽셀,
// 곧 화면 픽셀로 72x80 이다(같은 캡처의 게임 글꼴 한글 피치 32픽셀 = 원본 16픽셀로
// 배율을 잡았다). 옵시디언 28번 노트 "분석-게임풍 다이얼로그 UI" 에 재는 법까지 적어 뒀다.
#define ASK_PORT_W  72
#define ASK_PORT_H  80
#define ASK_PAD     16
#define ASK_LINE_H  20                  // 게임 글꼴 한 줄(한글 14 + 여백)
#define ASK_TEXT_X  (ASK_PAD + ASK_PORT_W + 20)
#define ASK_TEXT_W  (ASK_W - ASK_TEXT_X - ASK_PAD)
#define ASK_BTN_W   92
#define ASK_BTN_H   24                  // 게임 띠의 제 높이(GAMESKIN_H)
#define ASK_BTN_GAP 24

#define ASK_BG    RGB( 58, 34, 42)      // 짙은 자주 바탕
#define ASK_EDGE  RGB(228, 216, 190)    // 크림 테두리
#define ASK_TEXT  RGB(244, 238, 224)

static int      g_active = 0;
static int      g_info = 0;             // 1 = 알릴 뿐이라 단추가 [확인] 하나다
static int      g_gender = -1, g_face = -1;
static int      g_nline = 0;
static wchar_t  g_line[ASK_MAX_LINES][128];

// 게임 글꼴 한 줄을 찍을 24bpp 버퍼. 매번 잡지 않도록 한 벌 잡아 둔다.
// 24bpp DIB 는 한 행이 4바이트 경계에 맞아야 한다 — 폭이 4의 배수면 3*폭 도 그렇다.
// 판 폭을 고치다 그 조건이 깨지면 여기서 컴파일이 멈춘다(글이 비스듬히 밀려 나가는
// 것보다 낫다).
typedef char ask_row_align_check[(ASK_TEXT_W % 4 == 0) ? 1 : -1];
static unsigned char g_row[ASK_TEXT_W * 16 * 3];

int Ask_Active(void) { return g_active; }
void Ask_Close(void) { g_active = 0; }

static void Open(const wchar_t* text, int gender, int faceCode, int info)
{
    const wchar_t* p = text;
    int n = 0;

    g_nline = 0;
    while (p && *p && n < ASK_MAX_LINES) {
        int k = 0;
        while (*p && *p != L'\n' && k < 127) g_line[n][k++] = *p++;
        g_line[n][k] = 0;
        n++;
        if (*p == L'\n') p++;
    }
    g_nline = n;
    g_gender = gender;
    g_face   = faceCode;
    g_info   = info;
    g_active = 1;
}

void Ask_Open(const wchar_t* text, int gender, int faceCode) { Open(text, gender, faceCode, 0); }
void Ask_Info(const wchar_t* text, int gender, int faceCode) { Open(text, gender, faceCode, 1); }

// ---------------------------------------------------------------- 자리

// 판 높이는 담긴 것에 맞춘다 — 글 줄과 초상화 중 큰 쪽에 위아래 여백과 단추 줄을 더한다.
// 그래서 한 줄짜리 알림은 게임 메시지 창처럼 납작하게 뜬다.
static int PanelH(void)
{
    int body = g_nline * ASK_LINE_H;
    int port = (g_face >= 0) ? ASK_PORT_H : 0;
    int h = body > port ? body : port;
    return ASK_PAD + 6 + h + 14 + ASK_BTN_H + ASK_PAD;
}
static int PanelY(void)
{
    int y = (WIN_H - PanelH()) / 2 - 30;
    return y < FRAME + TITLE_H + 8 ? FRAME + TITLE_H + 8 : y;
}

static RECT RcPanel(void)
{ RECT r; r.left=ASK_X; r.top=PanelY(); r.right=ASK_X+ASK_W; r.bottom=r.top+PanelH(); return r; }

// 단추는 판 전체 가운데에 둔다 — 게임도 그렇다. 글 영역 가운데로 잡으면 초상화 폭만큼
// 오른쪽으로 밀려 치우쳐 보인다.
static int BtnTop(void)  { return PanelY() + PanelH() - ASK_PAD - ASK_BTN_H; }
static int BtnLeft(void)
{
    int w = g_info ? ASK_BTN_W : (ASK_BTN_W * 2 + ASK_BTN_GAP);
    return ASK_X + (ASK_W - w) / 2;
}

static RECT RcYes(void)
{ RECT r; r.left=BtnLeft(); r.right=r.left+ASK_BTN_W; r.top=BtnTop(); r.bottom=r.top+ASK_BTN_H; return r; }
static RECT RcNo(void)
{ RECT r = RcYes(); r.left += ASK_BTN_W + ASK_BTN_GAP; r.right = r.left + ASK_BTN_W; return r; }

// ---------------------------------------------------------------- 게임 글꼴로 한 줄

// 글자를 하나씩 점 찍으면 느리고 깜빡이므로, 줄 하나를 통째로 24bpp 그림에 그린 뒤
// 한 번에 옮긴다. 바탕은 판 색으로 채우니 판 위에 그대로 얹힌다.
static void GameLine(HDC dc, int x, int y, const wchar_t* s)
{
    unsigned char mask[GF_MAX_W * GF_MAX_H];
    BITMAPINFO bi;
    const wchar_t* p;
    int cx = 0, i, n = ASK_TEXT_W * 16;

    for (i = 0; i < n; i++) {
        g_row[i * 3 + 0] = GetBValue(ASK_BG);
        g_row[i * 3 + 1] = GetGValue(ASK_BG);
        g_row[i * 3 + 2] = GetRValue(ASK_BG);
    }
    for (p = s; *p; p++) {
        int gw, gh, r, c, top;
        if (!GameFont_Glyph(*p, mask, &gw, &gh)) { cx += GF_ANK_W; continue; }
        if (cx + gw > ASK_TEXT_W) break;
        top = (16 - gh) / 2;
        for (r = 0; r < gh; r++)
            for (c = 0; c < gw; c++) {
                int o;
                if (!mask[r * GF_MAX_W + c]) continue;
                o = ((top + r) * ASK_TEXT_W + cx + c) * 3;
                g_row[o + 0] = GetBValue(ASK_TEXT);
                g_row[o + 1] = GetGValue(ASK_TEXT);
                g_row[o + 2] = GetRValue(ASK_TEXT);
            }
        cx += gw;
    }

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = ASK_TEXT_W;
    bi.bmiHeader.biHeight = -16;              // 위에서 아래로
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, x, y, ASK_TEXT_W, 16, 0, 0, 0, 16, g_row, &bi, DIB_RGB_COLORS);
}

// 글꼴 파일을 못 읽었을 때. 모양만 수수해지고 자리는 같다.
static void GdiLine(HDC dc, int x, int y, const wchar_t* s)
{
    RECT r;
    r.left = x; r.right = x + ASK_TEXT_W; r.top = y; r.bottom = y + 16;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ASK_TEXT);
    if (g_font) SelectObject(dc, g_font);
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// ---------------------------------------------------------------- 그리기

void Ask_Paint(HDC dc)
{
    RECT p = RcPanel(), r;
    HBRUSH br;
    int useFont, i, ty;

    if (!g_active) return;
    useFont = GameFont_Load();

    br = CreateSolidBrush(ASK_BG); FillRect(dc, &p, br); DeleteObject(br);

    // 크림 테두리 한 줄. 게임 것도 1픽셀이다 — 두 줄로 두르면 눈에 띄게 굵어 보인다.
    br = CreateSolidBrush(ASK_EDGE);
    r = p; FrameRect(dc, &r, br);
    DeleteObject(br);

    if (g_face >= 0)
        Face_Draw(dc, ASK_X + ASK_PAD, p.top + ASK_PAD, ASK_PORT_W, ASK_PORT_H, g_gender, g_face);

    ty = p.top + ASK_PAD + 6;
    for (i = 0; i < g_nline; i++) {
        if (g_line[i][0]) {
            if (useFont) GameLine(dc, ASK_X + ASK_TEXT_X, ty, g_line[i]);
            else         GdiLine(dc, ASK_X + ASK_TEXT_X, ty, g_line[i]);
        }
        ty += ASK_LINE_H;
    }

    if (g_info) {
        UI_Button(dc, RcYes(), L"확인", FALSE);
    } else {
        UI_Button(dc, RcYes(), L"YES", FALSE);
        UI_Button(dc, RcNo(),  L"NO",  FALSE);
    }
}

// ---------------------------------------------------------------- 입력

int Ask_Click(POINT pt, int* answer)
{
    RECT y, n;
    if (!g_active) return 0;
    *answer = -1;
    y = RcYes(); n = RcNo();
    if (PtInRect(&y, pt))                { *answer = 1; g_active = 0; }
    else if (!g_info && PtInRect(&n, pt)) { *answer = 0; g_active = 0; }
    return 1;                   // 판이 떠 있는 동안 다른 클릭은 판이 삼킨다
}

int Ask_Key(WPARAM wp, int* answer)
{
    if (!g_active) return 0;
    *answer = -1;
    if (g_info) {
        // 알림 판은 무엇으로 닫든 결과가 같다.
        if (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_SPACE) { *answer = 1; g_active = 0; }
        return 1;
    }
    switch (wp) {
    case VK_RETURN: case 'Y': *answer = 1; g_active = 0; break;
    case VK_ESCAPE: case 'N': *answer = 0; g_active = 0; break;
    default: break;
    }
    return 1;
}
