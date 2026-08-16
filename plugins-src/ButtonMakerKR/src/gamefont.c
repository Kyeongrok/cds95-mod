#include "gamefont.h"

#define HAN_REC   30                 // 레코드 한 개 크기
#define HAN_LO    0xA1A1u            // 파일에 들어 있는 가장 작은 코드
#define HAN_HI    0xC8FEu            // 가장 큰 코드
#define HAN_SPAN  (HAN_HI - HAN_LO + 1)
#define ANK_LO    0x20
#define ANK_N     96

static unsigned char* g_han = NULL;   // ALL_FONT.16P 통째
static unsigned       g_hanRecs = 0;
static short*         g_index = NULL; // 코드 - HAN_LO -> 레코드 번호, 없으면 -1
static unsigned char  g_ank[ANK_N * GF_ANK_H];
static int            g_ankOk = 0;
static int            g_tried = 0;

// 실행 파일이 있는 폴더의 파일을 통째로 읽는다. 부르는 쪽이 HeapFree 한다.
static unsigned char* ReadGameFile(const char* name, unsigned* outLen)
{
    char exe[MAX_PATH], path[MAX_PATH];
    HANDLE f;
    DWORD size, got = 0;
    unsigned char* buf;
    int i, cut = -1;

    *outLen = 0;
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return NULL;
    for (i = 0; exe[i]; i++) if (exe[i] == '\\' || exe[i] == '/') cut = i;
    if (cut < 0) return NULL;
    exe[cut] = 0;
    wsprintfA(path, "%s\\%s", exe, name);

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 0x400000) { CloseHandle(f); return NULL; }
    buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buf) { CloseHandle(f); return NULL; }
    if (!ReadFile(f, buf, size, &got, NULL) || got != size) {
        HeapFree(GetProcessHeap(), 0, buf); CloseHandle(f); return NULL;
    }
    CloseHandle(f);
    *outLen = size;
    return buf;
}

int GameFont_Ready(void) { return g_han != NULL && g_index != NULL; }

int GameFont_Load(void)
{
    unsigned len = 0, i;
    unsigned char* ank;

    if (g_tried) return GameFont_Ready();
    g_tried = 1;

    g_han = ReadGameFile("ALL_FONT.16P", &len);
    if (!g_han) return 0;
    g_hanRecs = len / HAN_REC;

    g_index = (short*)HeapAlloc(GetProcessHeap(), 0, HAN_SPAN * sizeof(short));
    if (!g_index) { HeapFree(GetProcessHeap(), 0, g_han); g_han = NULL; return 0; }
    for (i = 0; i < HAN_SPAN; i++) g_index[i] = -1;
    for (i = 0; i < g_hanRecs; i++) {
        unsigned code = (unsigned)g_han[i * HAN_REC] | ((unsigned)g_han[i * HAN_REC + 1] << 8);
        if (code >= HAN_LO && code <= HAN_HI) g_index[code - HAN_LO] = (short)i;
    }

    // ANK 는 없어도 한글은 찍힌다 — 없으면 영문·숫자만 못 쓴다.
    ank = ReadGameFile("ANKFONT.DAT", &len);
    if (ank) {
        if (len >= sizeof(g_ank)) { CopyMemory(g_ank, ank, sizeof(g_ank)); g_ankOk = 1; }
        HeapFree(GetProcessHeap(), 0, ank);
    }
    return 1;
}

void GameFont_Free(void)
{
    if (g_index) { HeapFree(GetProcessHeap(), 0, g_index); g_index = NULL; }
    if (g_han)   { HeapFree(GetProcessHeap(), 0, g_han);   g_han = NULL; }
    g_hanRecs = 0; g_ankOk = 0; g_tried = 0;
}

int GameFont_Glyph(wchar_t ch, unsigned char* mask, int* w, int* h)
{
    char mb[8];
    int n, r, c;

    if (!GameFont_Ready()) return 0;

    n = WideCharToMultiByte(949, 0, &ch, 1, mb, sizeof(mb), NULL, NULL);
    if (n <= 0) return 0;

    if (n >= 2) {
        unsigned code = ((unsigned char)mb[0] << 8) | (unsigned char)mb[1];
        short rec;
        const unsigned char* b;
        if (code < HAN_LO || code > HAN_HI) return 0;
        rec = g_index[code - HAN_LO];
        if (rec < 0) return 0;
        b = g_han + (unsigned)rec * HAN_REC + 2;
        for (r = 0; r < GF_HAN_H; r++) {
            unsigned bits = ((unsigned)b[r * 2] << 8) | b[r * 2 + 1];
            for (c = 0; c < GF_HAN_W; c++)
                mask[r * GF_MAX_W + c] = (unsigned char)((bits >> (15 - c)) & 1);
        }
        *w = GF_HAN_W; *h = GF_HAN_H;
        return 1;
    }

    // 한 바이트 — ASCII
    {
        int k = (unsigned char)mb[0];
        if (!g_ankOk || k < ANK_LO || k >= ANK_LO + ANK_N) return 0;
        for (r = 0; r < GF_ANK_H; r++) {
            unsigned bits = g_ank[(k - ANK_LO) * GF_ANK_H + r];
            for (c = 0; c < GF_ANK_W; c++)
                mask[r * GF_MAX_W + c] = (unsigned char)((bits >> (7 - c)) & 1);
        }
        *w = GF_ANK_W; *h = GF_ANK_H;
        return 1;
    }
}

int GameFont_TextWidth(const wchar_t* s)
{
    unsigned char mask[GF_MAX_W * GF_MAX_H];
    int total = 0, w, h;
    if (!s) return 0;
    for (; *s; s++)
        if (GameFont_Glyph(*s, mask, &w, &h)) total += w;
    return total;
}
