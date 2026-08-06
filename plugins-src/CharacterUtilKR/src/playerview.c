#include "playerview.h"
#include "livechar.h"
#include "faces.h"
#include "chardb.h"
#include "maids.h"     // Maid_BloodName — 혈액형 이름표 하나를 두 벌 두지 않으려고 빌려 쓴다
#include "ui.h"
#include <commdlg.h>

// [플레이어] 탭.
//   위: 주인공의 지금 초상화(크게) + 이름/성별/나이/직업/명성 요약
//   아래: 얼굴 격자. 한 칸 누르면 그 자리에서 주인공 레코드(0x1B60A8 +0x00)에 바로 쓴다.
//
// 격자는 [남 표]/[여 표] 로 MALE.CDS · FEMALE.CDS 를 오가며 볼 수 있다. 두 파일을 읽는 것
// 자체는 어렵지 않다(Face_Draw 가 이미 표를 골라 그린다) — 걸리는 건 파일이 나뉜 게 아니라,
// 얼굴코드에 "어느 표인지"가 안 들어 있다는 점이다. 게임은 성별(+0x08)로 표를 고르므로
// 반대쪽 표의 코드만 넣으면 제 표의 엉뚱한 얼굴이 나온다. 그래서 반대쪽 표에서 고르면
// 성별도 같이 쓴다(초상화 말고 게임 진행에도 영향이 가므로 창에 경고를 띄운다).
//
// 고친 값은 세이브할 때 게임이 같이 저장한다. 그래서 여급·스폰서(EXE 표)와 달리
// character.state 에 적어 두지 않는다 — 저장 안 하고 끄면 원래 얼굴로 돌아간다.

// 필터는 CharDb_Cat 의 네 갈래를 하나도 빠뜨리지 않고 덮는다.
// 예전에 [전체][기본][인물] 셋만 뒀더니 남자 414개 = 기본 98 + 인물 246 이 안 맞았다
// (나머지 70개가 스폰서였는데 고를 버튼이 없었다). 개수가 안 맞아 보이면 필터가 샌 것이다.
#define PL_FILT_N 5
static const struct { const wchar_t* label; int cat; } kPlCat[PL_FILT_N] = {
    { L"전체",   -1 },  // -1 = 거르지 않는다
    { L"기본",    0 },  // 이름표가 없는 얼굴. 인물 만들기 화면에 나오는 것들이다
    { L"인물",    1 },
    { L"여급",    2 },  // 여자 표에만 있다(남자는 0개)
    { L"스폰서",  3 },
};

// 직업 이름표 (ce/CDS_95.CT "직업" 드롭다운). 주인공이 실제로 갖는 것은 앞쪽 8개지만
// 값이 어긋났을 때 숫자만 뜨는 것보다는 나아서 표 전체를 들고 있는다.
static const wchar_t* kJob[] = {
    L"탐험가", L"발굴자", L"사냥꾼", L"정복자", L"해적", L"전도사", L"상인", L"군인",
    L"예비1", L"예비2", L"부관", L"항해사", L"측량사", L"통역",
    L"국왕", L"교황", L"총독", L"귀족", L"신부", L"상인", L"관리", L"학자",
};
#define JOB_N ((int)(sizeof(kJob)/sizeof(kJob[0])))

static int      g_tbl    = FACE_MALE;  // 지금 보고 있는 표. 탭을 켤 때 주인공 성별로 맞춘다
static int      g_cat    = -1;      // 격자 필터. kPlCat[].cat
static int      g_scroll = 0;
static int      g_list[PL_MAX];     // 지금 필터에 맞는 얼굴코드들
static int      g_n      = 0;
static wchar_t  g_msg[96] = L"";

// 보고 있는 표가 주인공 성별과 다른가 — 다르면 고를 때 성별도 같이 쓴다.
static int CrossTable(void) { return Player_Ready() && g_tbl != Player_Gender(); }

// ---- 자리 ----
static RECT RcReload(void)
{ RECT r; r.left=FRAME+8; r.right=r.left+66; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcTbl(int gender)
{ RECT r; r.left=FRAME+84+gender*46; r.right=r.left+42; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcCat(int i)
{ RECT r; r.left=FRAME+184+i*52; r.right=r.left+48; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcHint(void)
{ RECT r; r.left=FRAME+450; r.right=WIN_W-FRAME-8; r.top=FILTER_Y; r.bottom=r.top+22; return r; }
static RECT RcInfo(void)
{ RECT r; r.left=PL_GX+PL_PORT_W+12; r.right=WIN_W-FRAME-GAP; r.top=PL_Y; r.bottom=PL_Y+PL_PORT_H; return r; }
static RECT RcCell(int vis)
{
    RECT r;
    r.left = PL_GX + (vis % PL_COLS) * PL_CELL_W;
    r.right = r.left + PL_CELL_W;
    r.top = PL_GY + (vis / PL_COLS) * PL_CELL_H;
    r.bottom = r.top + PL_CELL_H;
    return r;
}
static RECT RcTrack(void)
{ RECT r; r.right=WIN_W-FRAME-2; r.left=r.right-SB_W; r.top=PL_GY; r.bottom=PL_GY+PL_ROWS*PL_CELL_H; return r; }
// 정보 패널 아래쪽 단추 줄. 0 내보내기 1 갈아 끼우기 2 끝에 추가
#define PL_BTN_N 3
static RECT RcBtn(int i)
{
    static const int kx[PL_BTN_N] = { 0, 122, 224 };
    static const int kw[PL_BTN_N] = { 118,  98,  98 };
    RECT r;
    r.left   = PL_GX + PL_PORT_W + 12 + 8 + kx[i];
    r.right  = r.left + kw[i];
    r.bottom = PL_Y + PL_PORT_H - 8;
    r.top    = r.bottom - 22;
    return r;
}

static int TotalRows(void) { return (g_n + PL_COLS - 1) / PL_COLS; }
static int MaxScroll(void) { int m = TotalRows() - PL_ROWS; return m > 0 ? m : 0; }

static const wchar_t* JobName(int job)
{ return (job >= 0 && job < JOB_N) ? kJob[job] : L"?"; }

// 지금 필터에 맞는 얼굴코드 목록을 다시 만든다.
static void Rebuild(void)
{
    int total = Face_Count(g_tbl), i;
    g_n = 0;
    if (total > PL_MAX) total = PL_MAX;
    for (i = 0; i < total; i++) {
        if (g_cat >= 0 && CharDb_Cat(g_tbl, i) != (unsigned char)g_cat) continue;
        g_list[g_n++] = i;
    }
    if (g_scroll > MaxScroll()) g_scroll = MaxScroll();
}

// 지금 쓰는 얼굴이 보이는 자리로 스크롤한다. 다른 표를 보고 있으면 맨 위에서 시작한다.
static void ScrollToCurrent(void)
{
    int cur = CrossTable() ? -1 : Player_Face(), i, row, mx;
    for (i = 0; i < g_n; i++) if (g_list[i] == cur) break;
    if (i >= g_n) { g_scroll = 0; return; }
    row = i / PL_COLS - PL_ROWS / 2;
    mx = MaxScroll();
    g_scroll = row < 0 ? 0 : (row > mx ? mx : row);
}

// ---- 그리기 ----
static void PaintHead(HDC dc)
{
    RECT box = RcInfo(), ln;
    HBRUSH br;
    wchar_t nm[64], buf[160];
    int gender = Player_Gender(), face = Player_Face();
    int age = Player_Age(), born = Player_BirthYear();
    int blood = Player_Blood(), job = Player_Job();
    int fame = Player_Fame(), infamy = Player_Infamy();
    int y;

    Face_Draw(dc, PL_GX, PL_Y, PL_PORT_W, PL_PORT_H, gender, face);

    br = CreateSolidBrush(COL_DISP_BG); FillRect(dc, &box, br); DeleteObject(br);
    br = CreateSolidBrush(COL_DARK);    FrameRect(dc, &box, br); DeleteObject(br);

    Player_Name(nm, 64);
    y = box.top + 8;
    ln.left = box.left + 10; ln.right = box.right - 10;

    ln.top = y; ln.bottom = y + 22;
    UI_Text(dc, ln, nm[0] ? nm : L"(이름 없음)", g_font, COL_TEXT,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    y += 26;

    wsprintfW(buf, L"얼굴 #%d · %s · %s형", face, gender == 1 ? L"여" : L"남",
              blood >= 0 ? Maid_BloodName(blood) : L"?");
    ln.top = y; ln.bottom = y + 20;
    UI_Text(dc, ln, buf, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    y += 21;

    // 나이는 +0x04 를 믿지 않는다 — 주인공 레코드는 그 자리가 0 이고(NPC 만 쓴다),
    // 게임 화면의 "연령"도 생년과 지금 연도로 계산한 값이다. 연도를 못 읽을 때만 +0x04 를 쓴다.
    { int now = LiveChar_Year();
      if (now && born)          age = now - born;
      if (age != -9999 && born) wsprintfW(buf, L"%d세 · %d년생 · %s", age, born, JobName(job));
      else if (born)            wsprintfW(buf, L"%d년생 · %s", born, JobName(job));
      else                      wsprintfW(buf, L"%s", JobName(job)); }
    ln.top = y; ln.bottom = y + 20;
    UI_Text(dc, ln, buf, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    y += 21;

    wsprintfW(buf, L"명성 %d · 악명 %d", fame < 0 ? 0 : fame, infamy < 0 ? 0 : infamy);
    ln.top = y; ln.bottom = y + 20;
    UI_Text(dc, ln, buf, g_smallFont, COL_TEXT, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    y += 24;

    ln.top = y; ln.bottom = box.bottom - 34;   // 아래 34 는 단추 줄 몫
    if (CrossTable())
        UI_Text(dc, ln,
                g_tbl == FACE_FEMALE
                    ? L"[여 표]를 보고 있습니다. 게임은 성별로 얼굴 파일을 고르므로, 여기서 "
                      L"고르면 주인공 성별도 '여'로 함께 바뀝니다 — 초상화뿐 아니라 게임 진행에도 "
                      L"영향이 갑니다."
                    : L"[남 표]를 보고 있습니다. 게임은 성별로 얼굴 파일을 고르므로, 여기서 "
                      L"고르면 주인공 성별도 '남'으로 함께 바뀝니다 — 초상화뿐 아니라 게임 진행에도 "
                      L"영향이 갑니다.",
                g_smallFont, COL_WARN_TX, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);
    else
        UI_Text(dc, ln,
                L"아래에서 얼굴을 누르면 바로 바뀝니다. 게임 화면에는 인물 정보를 다시 열 때 "
                L"보이고, 게임에서 저장하면 그대로 남습니다. 반대쪽 표도 볼 수 있습니다.",
                g_smallFont, COL_TEXT, DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);

    UI_Button(dc, RcBtn(0), L"이 얼굴 PNG로", FALSE);
    UI_Button(dc, RcBtn(1), L"PNG 넣기",      FALSE);
    UI_Button(dc, RcBtn(2), L"끝에 추가",     FALSE);

    if (g_msg[0]) {
        RECT m = RcHint();
        UI_Text(dc, m, g_msg, g_smallFont, COL_WARN_TX,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
}

static void PaintGrid(HDC dc)
{
    // 지금 쓰는 얼굴 표시는 같은 표를 볼 때만 뜻이 있다(코드 번호는 표마다 따로 센다).
    int cur = CrossTable() ? -1 : Player_Face(), vis;

    for (vis = 0; vis < PL_COLS * PL_ROWS; vis++) {
        int i = g_scroll * PL_COLS + vis, code;
        RECT cell, lbl;
        wchar_t t[16];
        HBRUSH br;

        if (i >= g_n) break;
        code = g_list[i];
        cell = RcCell(vis);

        if (code == cur) {   // 지금 쓰는 얼굴
            br = CreateSolidBrush(COL_SEL_BG); FillRect(dc, &cell, br); DeleteObject(br);
        }
        Face_Draw(dc, cell.left + 3, cell.top + 2, PL_THUMB_W, PL_THUMB_H, g_tbl, code);

        lbl = cell;
        lbl.top = cell.top + 2 + PL_THUMB_H;
        lbl.bottom = cell.bottom;
        wsprintfW(t, L"%d", code);
        UI_Text(dc, lbl, t, g_smallFont, code == cur ? COL_LIGHT : COL_TEXT,
                DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    UI_Scrollbar(dc, RcTrack(), g_scroll, MaxScroll(), PL_ROWS, TotalRows());
}

void Pl_Paint(HDC dc)
{
    int i;

    UI_Button(dc, RcReload(), L"새로고침", FALSE);

    if (!Player_Ready()) {
        RECT e;
        const wchar_t* why;
        switch (Player_Status()) {
        case LIVECHAR_E_EMPTY:  why = L"아직 세이브를 불러오지 않았습니다. 게임을 진행한 뒤 새로고침하세요."; break;
        case LIVECHAR_E_SLOTS:  why = L"주인공 자리의 내용이 인물 정보로 보이지 않습니다(다른 버전의 실행 파일인 듯합니다)."; break;
        case LIVECHAR_E_MODULE: why = L"게임 모듈을 찾지 못했습니다."; break;
        default:                why = L"주인공 자리를 읽지 못했습니다."; break;
        }
        e.left = PL_GX; e.right = WIN_W - FRAME - GAP; e.top = PL_Y + 40; e.bottom = e.top + 40;
        UI_Text(dc, e, why, g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return;
    }

    UI_Button(dc, RcTbl(FACE_MALE),   L"남 표", g_tbl == FACE_MALE);
    UI_Button(dc, RcTbl(FACE_FEMALE), L"여 표", g_tbl == FACE_FEMALE);
    for (i = 0; i < PL_FILT_N; i++)
        UI_Button(dc, RcCat(i), kPlCat[i].label, g_cat == kPlCat[i].cat);

    if (!g_msg[0]) {
        wchar_t buf[80];
        wsprintfW(buf, L"%s.CDS · 고를 수 있는 얼굴 %d개",
                  g_tbl == FACE_FEMALE ? L"FEMALE" : L"MALE", g_n);
        UI_Text(dc, RcHint(), buf, g_smallFont, COL_TEXT,
                DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }

    PaintHead(dc);
    if (g_n > 0) PaintGrid(dc);
    else {
        RECT e;
        e.left = PL_GX; e.right = WIN_W - FRAME - GAP; e.top = PL_GY + 20; e.bottom = e.top + 30;
        UI_Text(dc, e, L"보여줄 얼굴이 없습니다 — MALE.CDS / FEMALE.CDS 를 열지 못했거나 필터가 비어 있습니다.",
                g_font, COL_TEXT, DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
}

// ---- PNG 내보내기 / 넣기 ----
static const wchar_t* ErrText(int rc)
{
    switch (rc) {
    case FACE_ERR_GDIP:    return L"gdiplus.dll 을 못 써서 PNG 를 다룰 수 없습니다";
    case FACE_ERR_IMAGE:   return L"그림 파일을 읽지 못했습니다";
    case FACE_ERR_ARCHIVE: return L"얼굴 파일(MALE/FEMALE.CDS)이 안 열려 있습니다";
    case FACE_ERR_ENCODE:  return L"다시 묶는 데 실패했습니다";
    case FACE_ERR_VERIFY:  return L"검사에 걸려 파일을 건드리지 않았습니다";
    case FACE_ERR_WRITE:   return L"파일을 쓰지 못했습니다";
    case FACE_ERR_RANGE:   return L"얼굴 번호가 표 밖입니다";
    default:               return L"실패했습니다";
    }
}

static int PickFile(HWND h, wchar_t* path, int save)
{
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = h;
    ofn.lpstrFilter  = save ? L"PNG 그림\0*.png\0"
                            : L"그림 파일\0*.png;*.bmp;*.jpg;*.jpeg;*.gif\0모든 파일\0*.*\0";
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = L"png";
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER)
                     : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static void DoExport(HWND h)
{
    wchar_t path[MAX_PATH];
    int gender = Player_Gender(), code = Player_Face(), rc;
    if (gender < 0 || code < 0) { lstrcpyW(g_msg, L"지금 얼굴을 알 수 없습니다"); return; }
    wsprintfW(path, L"%s_%d.png", gender == FACE_FEMALE ? L"FEMALE" : L"MALE", code);
    if (!PickFile(h, path, 1)) return;
    rc = Face_ExportPng(gender, code, path);
    if (rc == FACE_ERR_OK) wsprintfW(g_msg, L"#%d 을 PNG 로 내보냈습니다", code);
    else                   lstrcpynW(g_msg, ErrText(rc), 96);
}

// append 면 표 끝에 새 얼굴로 붙이고 주인공에게 그 번호를 바로 물린다.
// 아니면 주인공이 지금 쓰는 자리를 갈아 끼운다.
static void DoImport(HWND h, int append)
{
    wchar_t path[MAX_PATH], msg[512], nm[64];
    int gender = Player_Gender(), code = Player_Face(), rc, newCode = -1;

    if (gender < 0) { lstrcpyW(g_msg, L"주인공을 읽지 못했습니다"); return; }
    path[0] = 0;
    if (!PickFile(h, path, 0)) return;

    // 얼굴 파일을 다시 쓰는 일이라 한 번 묻는다. 되돌릴 길(.orig)도 같이 알려 준다.
    if (append) {
        wsprintfW(msg,
            L"%s.CDS 끝에 얼굴을 새로 붙이고, 주인공이 그 얼굴을 쓰게 합니다.\n\n"
            L"기존 얼굴은 하나도 건드리지 않습니다.\n"
            L"원본은 %s.CDS.orig 로 남깁니다(처음 한 번만).\n\n계속할까요?",
            gender == FACE_FEMALE ? L"FEMALE" : L"MALE",
            gender == FACE_FEMALE ? L"FEMALE" : L"MALE");
    } else {
        lstrcpynW(nm, CharDb_Name(gender, code), 64);   // 그 자리를 쓰는 NPC 가 있으면 이름을 밝힌다
        wsprintfW(msg,
            L"%s.CDS 의 #%d 자리를 이 그림으로 바꿉니다.%s%s\n\n"
            L"그 얼굴을 쓰는 인물이 있으면 그쪽도 같이 바뀝니다.\n"
            L"원본은 %s.CDS.orig 로 남깁니다(처음 한 번만).\n\n계속할까요?",
            gender == FACE_FEMALE ? L"FEMALE" : L"MALE", code,
            nm[0] ? L"\n\n이 자리는 인물 " : L"",
            nm[0] ? nm : L"",
            gender == FACE_FEMALE ? L"FEMALE" : L"MALE");
    }
    if (MessageBoxW(h, msg, L"초상화 바꾸기", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;

    rc = append ? Face_AppendPng(gender, path, &newCode)
                : Face_ImportPng(gender, code, path);
    if (rc != FACE_ERR_OK) { lstrcpynW(g_msg, ErrText(rc), 96); return; }

    if (append && newCode >= 0) {
        Player_SetFace(newCode);
        wsprintfW(g_msg, L"#%d 로 새로 넣었습니다", newCode);
    } else {
        wsprintfW(g_msg, L"#%d 를 바꿨습니다", code);
    }
    Rebuild();          // 끝에 붙였으면 목록이 한 칸 길어졌다
    ScrollToCurrent();
}

// ---- 조작 ----
static void ScrollTo(HWND h, int row)
{
    int mx = MaxScroll();
    if (row < 0) row = 0;
    if (row > mx) row = mx;
    if (row != g_scroll) { g_scroll = row; InvalidateRect(h, NULL, FALSE); }
}

void Pl_Activate(HWND h, int active)
{
    if (!active) return;
    // 켤 때마다 다시 읽는다 — 세이브를 나중에 불러왔을 수 있다.
    Face_Load();
    Player_Load();
    g_msg[0] = 0;
    if (Player_Gender() >= 0) g_tbl = Player_Gender();   // 켤 때는 늘 제 표에서 시작한다
    Rebuild();
    ScrollToCurrent();
    if (h) InvalidateRect(h, NULL, FALSE);
}

int Pl_Click(HWND h, POINT pt)
{
    RECT r;
    int i;

    r = RcReload();
    if (PtInRect(&r, pt)) { Pl_Activate(h, 1); return 1; }
    if (!Player_Ready()) return 0;

    for (i = 0; i < PL_BTN_N; i++) {   // PNG 내보내기 / 넣기 / 끝에 추가
        r = RcBtn(i);
        if (!PtInRect(&r, pt)) continue;
        if      (i == 0) DoExport(h);
        else if (i == 1) DoImport(h, 0);
        else             DoImport(h, 1);
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }

    for (i = 0; i < 2; i++) {          // [남 표] / [여 표]
        r = RcTbl(i);
        if (!PtInRect(&r, pt)) continue;
        if (g_tbl != i) {
            g_tbl = i;
            g_msg[0] = 0;
            Rebuild();
            ScrollToCurrent();
            InvalidateRect(h, NULL, FALSE);
        }
        return 1;
    }

    for (i = 0; i < PL_FILT_N; i++) {
        r = RcCat(i);
        if (!PtInRect(&r, pt)) continue;
        if (g_cat != kPlCat[i].cat) {
            g_cat = kPlCat[i].cat;
            Rebuild();
            ScrollToCurrent();
            InvalidateRect(h, NULL, FALSE);
        }
        return 1;
    }

    r = RcTrack();
    if (PtInRect(&r, pt)) {
        int mid = (r.top + r.bottom) / 2;
        ScrollTo(h, g_scroll + (pt.y < mid ? -PL_ROWS : PL_ROWS));
        return 1;
    }

    for (i = 0; i < PL_COLS * PL_ROWS; i++) {
        int k = g_scroll * PL_COLS + i;
        if (k >= g_n) break;
        r = RcCell(i);
        if (!PtInRect(&r, pt)) continue;
        // 반대쪽 표에서 골랐으면 성별부터 맞춘다 — 안 그러면 게임이 제 표에서 같은 번호의
        // 엉뚱한 얼굴을 꺼낸다. 순서가 중요하다(성별이 먼저 바뀌어야 얼굴이 뜻을 갖는다).
        if (CrossTable()) {
            if (!Player_SetGender(g_tbl)) {
                lstrcpyW(g_msg, L"성별을 바꾸지 못했습니다");
                InvalidateRect(h, NULL, FALSE);
                return 1;
            }
        }
        if (Player_SetFace(g_list[k]))
            wsprintfW(g_msg, L"%s.CDS #%d 로 바꿨습니다",
                      g_tbl == FACE_FEMALE ? L"FEMALE" : L"MALE", g_list[k]);
        else
            lstrcpyW(g_msg, L"바꾸지 못했습니다");
        InvalidateRect(h, NULL, FALSE);
        return 1;
    }
    return 0;
}

int Pl_Key(HWND h, WPARAM wp)
{
    switch (wp) {
    case VK_UP:    ScrollTo(h, g_scroll - 1); return 1;
    case VK_DOWN:  ScrollTo(h, g_scroll + 1); return 1;
    case VK_PRIOR: ScrollTo(h, g_scroll - PL_ROWS); return 1;
    case VK_NEXT:  ScrollTo(h, g_scroll + PL_ROWS); return 1;
    case VK_HOME:  ScrollTo(h, 0); return 1;
    case VK_END:   ScrollTo(h, MaxScroll()); return 1;
    case 'R':      Pl_Activate(h, 1); return 1;
    }
    return 0;
}

void Pl_Wheel(HWND h, int notches)
{
    ScrollTo(h, g_scroll - notches);
}
