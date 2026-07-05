// AudioFixKR — 대항해시대III 한국어판의 "CD 에러" 원천 차단 플러그인.
//
// [원인 — 동적 분석으로 확정]
//   이 게임은 CD-ROM이 없으면 CD음악을 _inmm.dll 이 bgm\*.mp3 로 에뮬해 재생한다.
//   시작 시 koeicda!CDAudioOpen 이 _inmm 을 통해 MCI cdaudio 장치를 여는데,
//   MCI/오디오 세션이 비정상 종료 등으로 드라이버를 못 여는 상태면 MCI_OPEN 이
//   MCIERR_CANNOT_LOAD_DRIVER(266) 로 실패 → CDAudioOpen 이 -1 반환 →
//   사운드 초기화(cds_95!0x4222e0)가 0 반환 → 게이트(0x40d4c0)가 0 →
//   게임이 "CD 에러" 대화상자로 막힌다. (데이터CD/DirectSound/드라이브와는 무관)
//
// [해결] CDAudioOpen 이 실패(음수)를 반환하면 성공값(0)으로 바꿔 준다.
//   → 사운드 초기화가 정상 완료되어 게임이 그대로 진행한다. (그 상태에선 BGM만 무음)
//   MCI 가 정상일 때는 원래 핸들을 그대로 통과시키므로 음악도 정상 재생된다.
//   스탯/데이터/그래픽 등 게임 동작은 일절 바뀌지 않는다.

#include <windows.h>
#include <MinHook.h>
#include <stdio.h>
#include <stdarg.h>

static void LogLine(const char* fmt, ...)
{
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH - 24) return;
    lstrcatA(path, "cds_audiofix.log");
    FILE* f = fopen(path, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// koeicda!CDAudioOpen (cdecl, 1 arg) → 핸들(<0 이면 실패)
typedef int (__cdecl *CDOpen_t)(int);
static CDOpen_t o_CDOpen = NULL;
static int __cdecl h_CDOpen(int track)
{
    int r = o_CDOpen(track);
    if (r < 0) {
        LogLine("CDAudioOpen 실패(%d) → 0 으로 우회 (게임 진행, BGM 무음)", r);
        return 0;   // 음수(실패)를 성공(0)으로 → 게임이 CD에러 없이 진행
    }
    return r;       // 정상일 땐 원래 핸들 그대로 → 음악 정상
}

static void Install(void)
{
    HMODULE kc = GetModuleHandleA("koeicda.dll");
    if (!kc) kc = LoadLibraryA("koeicda.dll");
    if (!kc) { LogLine("koeicda.dll 없음 — 훅 미설치"); return; }

    void* p = (void*)GetProcAddress(kc, "CDAudioOpen");
    if (!p) { LogLine("CDAudioOpen export 없음"); return; }

    if (MH_CreateHook(p, &h_CDOpen, (void**)&o_CDOpen) == MH_OK &&
        MH_EnableHook(p) == MH_OK) {
        LogLine("---- AudioFixKR: CDAudioOpen 우회 훅 설치 완료 (pid=%lu) ----", GetCurrentProcessId());
    } else {
        LogLine("CDAudioOpen 훅 설치 실패");
    }
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        if (MH_Initialize() == MH_OK)
            Install();
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
