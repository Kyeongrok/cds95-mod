#include <windows.h>

// SphinxQuizKR — 스핑크스의 물음에서 맞는 선택지에 "(정답)" 을 붙인다.
//
// 스핑크스 함수는 0x47BFE0 하나다. 물음이 넷이고, 넷 다 같은 선택 메뉴(0x4878A0)로 답을 받는다.
//   · 수수께끼("아침에는 4개의 다리 …")   call @0x47C048   맞는 칸 = 1 ("자신")
//   · 다리 세기 세 문제                   call @0x47C194   맞는 칸 = esi - 1
// 다리 세기는 문제를 낼 때 게임이 수 셋을 뽑는다 —
//   esi = 다리 넷인 괴물(2~9) · edi = 다리 둘인 괴물(2~9) · ebx = 다리가 셋이 된 마리 수
// 문제 글에는 이 셋으로 셈한 합계만 찍히고, 답 판정은 `esi - 고른 칸 == 1` 이다.
// 그러니 메뉴를 부르는 순간의 esi 가 곧 답이라 방정식을 풀 것도 없다.
//
// 그 두 call 을 우리 함수로 돌려, 선택지 사본에서 맞는 칸만 글을 바꿔 원래 메뉴에 넘긴다.
// 고르는 것은 여전히 사람이다 — 판정 코드도 문제도 건드리지 않는다.
// 끄려면 "모드 > 플러그인 관리" 에서 이 플러그인을 끈다.

#define RVA_MENU     0x000878A0u   // 0x4878A0  int menu(const char** items, int, int, int, int)
#define RVA_SITE_Q0  0x0007C048u   // 0x47C048  수수께끼의 call
#define RVA_SITE_QN  0x0007C194u   // 0x47C194  다리 세기의 call
#define RIDDLE_ANSWER 1            // "자신"
#define ITEM_MAX     16

typedef int (__cdecl *MenuFn)(const char** items, int a, int b, int c, int d);

static MenuFn g_menu = NULL;
static int    g_legsAnswer = -1;   // 다리 세기에서 맞는 칸. 스텁이 esi 에서 떠 둔다

// " (정답)" 의 cp949 바이트. 소스는 UTF-8 로 빌드되므로 글자로 적으면 깨진다.
static const char kMark[] = " (\xC1\xA4\xB4\xE4)";

static int MarkedMenu(int answer, const char** items, int a, int b, int c, int d)
{
    static char mark[64];
    const char* copy[ITEM_MAX + 1];
    int n = 0;

    while (n < ITEM_MAX && items[n]) { copy[n] = items[n]; n++; }
    // 끝을 못 봤거나 답이 목록 밖이면 손대지 않고 그대로 넘긴다.
    if (items[n] || answer < 0 || answer >= n ||
        lstrlenA(items[answer]) + (int)sizeof(kMark) > (int)sizeof(mark))
        return g_menu(items, a, b, c, d);

    copy[n] = NULL;
    lstrcpyA(mark, items[answer]);
    lstrcatA(mark, kMark);
    copy[answer] = mark;
    return g_menu(copy, a, b, c, d);
}

static int __cdecl RiddleMenu(const char** items, int a, int b, int c, int d)
{
    return MarkedMenu(RIDDLE_ANSWER, items, a, b, c, d);
}

static int __cdecl LegsMenu(const char** items, int a, int b, int c, int d)
{
    return MarkedMenu(g_legsAnswer, items, a, b, c, d);
}

// esi 를 떠 두고 LegsMenu 로 건너뛴다. 스택은 그대로라 LegsMenu 가 게임으로 바로 돌아간다.
// eax 는 게임이 call 뒤에 결과로 덮어쓰는 자리라 써도 된다.
__declspec(naked) static void LegsMenuStub(void)
{
    __asm {
        mov eax, esi
        dec eax
        mov g_legsAnswer, eax
        jmp LegsMenu
    }
}

// call rel32 한 자리를 우리 함수로 돌린다. 그 자리가 메뉴를 부르는 call 이 아니면 안 건드린다.
static int Redirect(BYTE* base, DWORD siteRva, void* to)
{
    BYTE* site = base + siteRva;
    DWORD old = 0;
    if (site[0] != 0xE8) return 0;
    if (site + 5 + *(int*)(site + 1) != base + RVA_MENU) return 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int*)(site + 1) = (int)((BYTE*)to - (site + 5));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        BYTE* base = (BYTE*)GetModuleHandleW(NULL);
        int ok;
        DisableThreadLibraryCalls(hModule);
        if (!base) return TRUE;
        g_menu = (MenuFn)(base + RVA_MENU);
        ok  = Redirect(base, RVA_SITE_Q0, (void*)RiddleMenu);
        ok += Redirect(base, RVA_SITE_QN, (void*)LegsMenuStub);
        OutputDebugStringW(ok == 2 ? L"[SphinxQuizKR] 스핑크스 선택 메뉴 두 자리를 돌렸다."
                                   : L"[SphinxQuizKR] call 자리가 안 맞는다 — 다른 빌드로 보인다.");
    }
    return TRUE;
}
