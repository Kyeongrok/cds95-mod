// GameApiKR — 게임 함수 제네릭 호출 API 워커 (w12)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_ARGS 12

// 제네릭 호출기: conv(0=cdecl,1=thiscall,2=stdcall), fn, ecx값, args[], nargs.
// 인자 우측→좌측 push, thiscall이면 ecx 세팅, call, cdecl이면 스택 정리. 반환 eax.
static int __declspec(noinline) CallFunc(unsigned conv, void* fn, unsigned ecxVal, const unsigned* args, int n)
{
    int rv = 0;
    __asm {
        pushad
        mov  esi, args
        mov  edi, n
    pushloop:
        test edi, edi
        jz   doneargs
        dec  edi
        mov  eax, [esi + edi*4]
        push eax
        jmp  pushloop
    doneargs:
        mov  eax, conv
        cmp  eax, 1
        jne  notthis
        mov  ecx, ecxVal
    notthis:
        call fn
        mov  rv, eax
        mov  eax, conv        ; cdecl(0) 이면 호출자가 스택 정리
        test eax, eax
        jnz  nocleanup
        mov  edx, n
        shl  edx, 2
        add  esp, edx
    nocleanup:
        popad
    }
    return rv;
}

static void WriteAck(const wchar_t* p, const char* s)
{
    DWORD wr; HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, s, lstrlenA(s), &wr, NULL); CloseHandle(f); }
}

static DWORD WINAPI Worker(LPVOID param)
{
    wchar_t tmp[MAX_PATH], cmdPath[MAX_PATH], ackPath[MAX_PATH];
    (void)param;
    GetTempPathW(MAX_PATH, tmp);
    wsprintfW(cmdPath, L"%scds_api_cmd.txt", tmp);
    wsprintfW(ackPath, L"%scds_api_ack.txt", tmp);
    OutputDebugStringW(L"[GameApiKR] worker started.");
    for (;;)
    {
        HANDLE f; char buf[512]; DWORD rd = 0; char ack[128];
        char convc; unsigned addr = 0, ecxv = 0, args[MAX_ARGS]; int nargs = 0, conv, i, got;
        char* tok;
        Sleep(120);
        f = CreateFileW(cmdPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL); CloseHandle(f);
        DeleteFileW(cmdPath);
        if (rd == 0) continue;
        buf[rd] = 0;

        // 파싱: conv addr [ecx] [args...]   (모두 hex, conv는 c/t/s)
        tok = strtok(buf, " \t\r\n");
        if (!tok) { WriteAck(ackPath, "ERR empty"); continue; }
        convc = tok[0];
        conv = (convc == 't' || convc == 'T') ? 1 : (convc == 's' || convc == 'S') ? 2 : 0;
        tok = strtok(NULL, " \t\r\n");
        if (!tok || sscanf(tok, "%x", &addr) != 1 || addr < 0x401000 || addr >= 0x600000) { WriteAck(ackPath, "ERR bad addr"); continue; }
        // thiscall이면 다음 토큰 = ecx(this)
        if (conv == 1)
        {
            tok = strtok(NULL, " \t\r\n");
            if (tok) sscanf(tok, "%x", &ecxv);
        }
        // 나머지 = args
        while ((tok = strtok(NULL, " \t\r\n")) != NULL && nargs < MAX_ARGS)
        {
            if (sscanf(tok, "%x", &args[nargs]) == 1) nargs++;
        }

        {
            int r;
            __try {
                r = CallFunc((unsigned)conv, (void*)(uintptr_t)addr, ecxv, args, nargs);
                wsprintfA(ack, "OK %X", (unsigned)r);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                wsprintfA(ack, "ERR exception @%X", addr);
            }
        }
        (void)got; (void)i;
        WriteAck(ackPath, ack);
    }
    return 0;
}

void GameApi_Start(void)
{
    HANDLE t = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    if (t) CloseHandle(t);
}
