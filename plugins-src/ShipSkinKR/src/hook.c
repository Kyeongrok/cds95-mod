#include <windows.h>
#include <MinHook.h>
#include <string.h>
#include <stdlib.h>

// ShipSkinKR — 항해 배 이미지만 바꾸기 (스탯/이름/함선종류 전부 불변).
//
// 스프라이트 소스는 함선종류 필드(기함 = 0x5A4E40 = ship[0]+0x28)다. CE find-what-accesses 로
// 확인한 결과, 해상 지도의 배 스프라이트는 이 필드를 "직접" 읽지 않고 getter 0x44C6E0
// ( mov eax,[ecx+28]; ret ) 을 매 프레임 호출해 받아온다. 그 호출자 중 스프라이트 그래픽
// 데이터 포인터를 만드는 곳이 0x48A853 (call @0x48A84E). 이 한 call-site 를 우리 함수로
// 리다이렉트해서 "기함이고 스킨이 켜져 있을 때만" 선택 타입을 반환하고, 그 외(스탯/UI/전투)의
// getter 호출은 진짜 함선종류를 그대로 받게 둔다 → 화면만 바뀌고 스탯은 불변.
//   (검증: 0x48A84E 를 mov eax,7 로 임시 치환 시 해상 스프라이트만 다우로, 함선정보는 코구 유지.)
//
// 추가로 출항 함대그래픽 함수(0x46A520)에 대한 기존 swap-restore 훅도 유지한다(함대편성 등
// 다른 화면 커버). getter 리다이렉트와 독립적이라 충돌 없음.
//
// 선택 타입: %TEMP%\cds_shiptype.txt (정수 0~7, 없거나 -1이면 미적용).

// ---- 함대그래픽 함수 (RVA, base 0x400000). 시그니처로 빌드 검증 ----
#define RVA_FLEETGFX   0x0006A520u          // 0x46A520, __thiscall(this), ret(인자없음), SEH 프롤로그
#define FLAGSHIP_TYPE  0x005A4E40u          // 기함(slot0) 함선종류 (ship[0]+0x28)

// ---- getter call-site 리다이렉트 (해상 스프라이트) ----
#define RVA_GETTER     0x0004C6E0u          // 0x44C6E0  mov eax,[ecx+28]; ret  (함선종류 getter)
#define RVA_CALLSITE   0x0008A84Eu          // 0x48A84E  call 0x44C6E0  (스프라이트 그래픽 포인터 산출부)
#define RVA_SHIP0      0x001A4E18u          // 0x5A4E18  기함(slot0) 구조체 base
#define SHIP_TYPE_OFF  0x28u                // 함선종류 오프셋

// ---- 아틀라스 재배치 (8선종 고유 스프라이트, ana2-4) ----
// 해상 draw 경로 2곳을 패치해 스프라이트 아틀라스를 플러그인 8슬롯 버퍼로 옮긴다(타 화면 무손상).
//   0x48A853  mov ecx,[eax*4+0x5695D8]  →  mov ecx,eax; and ecx,7; nop nop  (type를 gid로 직접·클램프)
//   0x48A877  lea edi,[eax+0x5D68C8]     →  lea edi,[eax+BUF]  (disp32만 버퍼주소로)
// 버퍼[type] = 원본 테이블 매핑대로 각 type의 현재 스프라이트로 시드(미편집 시 동일). 이후 편집본 오버레이.
#define RVA_ATLAS         0x001D68C8u       // 0x5D68C8  스프라이트 아틀라스 base
#define RVA_TABLE_DATA    0x001695D8u       // 0x5695D8  선종→gid 테이블(데이터)
#define RVA_DRAW_TABLE    0x0008A853u       // mov ecx,[eax*4+0x5695D8]  (7바이트)
#define RVA_DRAW_LEADISP  0x0008A879u       // lea edi,[eax+0x5D68C8] 의 disp32 위치
#define SHIP_FRAME_SZ     2304u             // 48*48 팔레트인덱스
#define SHIP_SLOT_SZ      (8u * SHIP_FRAME_SZ)   // gid당 8프레임 = 18432
#define SHIP_NUM_SLOTS    8u                // 선종 0~7
#define SHIP_BUF_SZ       (SHIP_NUM_SLOTS * SHIP_SLOT_SZ)  // 147456

// ---- 아틀라스 로더 Decode (RVA, base 0x400000) ----
// 정적 xref(0x5D68C8 참조)로 특정: init 코드 0x489AB3~ 가 이 함수를 반복 호출해 아틀라스를 채운다.
//   mov ecx,0x5AA3B8; push 0x5D68C8; push 1;  call 0x463680   ; Decode(mgr, resIdx=1, dst=해상 배 아틀라스)
//   push 0x6092D0;    push 2;                 call 0x463680   ; 정박 아틀라스
//   push 0x6089D0;    push 0xB;               call 0x463680
// __thiscall Decode(this=0x5AA3B8, int resIndex, void* dst), ret 8. 프롤로그 = kSig(SEH). MinHook 가능.
// RPM 오버레이는 세이브로드 때 재디코드에 덮여 불가 판명 → 이 함수 훅이 픽셀편집(sp5-2/5/7)의 유일 경로.
// (Step1: 검증용 로깅만. resIndex/dst/호출타이밍 확인 후 Step2에서 오버레이 추가.)
#define RVA_DECODE     0x00063680u          // 0x463680

typedef int (__fastcall* FleetGfx_t)(void* thisptr, void* edx);
static FleetGfx_t g_orig = NULL;

typedef int (__fastcall* Decode_t)(void* thisptr, void* edx, int resIndex, void* dst);
static Decode_t g_origDecode = NULL;
static volatile LONG g_decodeCalls = 0;

// 시그니처: mov eax,fs:[0]; push ebp; mov ebp,esp
static const BYTE kSig[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x55, 0x8B, 0xEC };

// 선택 스프라이트 타입(0~7). 파일 %TEMP%\cds_shiptype.txt 에서 읽음.
static int ReadChosenType(void)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    HANDLE f; char buf[16]; DWORD rd = 0; int v;
    if (!GetTempPathW(MAX_PATH, tmp)) return -1;
    wsprintfW(path, L"%scds_shiptype.txt", tmp);
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(f, buf, sizeof(buf) - 1, &rd, NULL)) { CloseHandle(f); return -1; }
    CloseHandle(f);
    buf[rd] = 0;
    v = atoi(buf);
    return (v >= 0 && v <= 7) ? v : -1;
}

// getter 리다이렉트는 매 프레임 호출되므로 파일 I/O를 스로틀(200ms)해 캐시.
static volatile LONG g_skin = -1;
static DWORD g_lastCheck = 0;
static void RefreshSkin(void)
{
    DWORD now = GetTickCount();
    if ((now - g_lastCheck) < 200) return;
    g_lastCheck = now;
    g_skin = ReadChosenType();
}

// ---- getter 리다이렉트용 우리 함수 (원본과 동일한 __thiscall 시맨틱: ecx=ship, ret in eax) ----
static LPBYTE g_base = NULL;
static int __fastcall DetourGetType(void* thisptr, void* edx)
{
    int real = *(volatile int*)((BYTE*)thisptr + SHIP_TYPE_OFF);
    (void)edx;
    RefreshSkin();
    // 기함이고 스킨이 켜져 있을 때만 화면용으로 치환. 그 외엔 진짜 함선종류.
    if (g_skin >= 0 && thisptr == (void*)(g_base + RVA_SHIP0))
        return g_skin;
    return real;
}

// ---- 출항 함대그래픽 훅 (swap-restore) ----
static int __fastcall DetourFleetGfx(void* thisptr, void* edx)
{
    int chosen = ReadChosenType();
    if (chosen >= 0)
    {
        volatile int* pType = (volatile int*)FLAGSHIP_TYPE;
        int save = *pType;
        int r;
        *pType = chosen;          // 스프라이트 로드용으로 치환
        r = g_orig(thisptr, edx); // 원본: 선택 타입으로 비트맵 로드
        *pType = save;            // 스탯용 원복
        return r;
    }
    return g_orig(thisptr, edx);
}

// ---- 아틀라스 로더 Decode 훅 (Step1: 검증용 로깅) ----
// %TEMP%\cds_decode_log.txt 에 각 호출을 append (디버거 없이 확인). 폭주 방지 캡(300).
static void LogDecode(void* thisptr, int resIndex, void* dst, int r)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    HANDLE f; char line[160]; int n; DWORD wr;
    LONG cnt = InterlockedIncrement(&g_decodeCalls);
    if (cnt > 300) return;
    n = wsprintfA(line, "#%ld Decode(this=%08X, resIndex=%d, dst=%08X) ret=%d\r\n",
                  cnt, (unsigned)(DWORD_PTR)thisptr, resIndex, (unsigned)(DWORD_PTR)dst, r);
    OutputDebugStringA(line);
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    wsprintfW(path, L"%scds_decode_log.txt", tmp);
    f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, line, (DWORD)n, &wr, NULL); CloseHandle(f); }
}

// ---- Step2: 해상 배 아틀라스(resIndex==1 → dst=0x5D68C8) 오버레이 ----
// 원본 Decode가 dst를 채운 "직후"(힙 변환 前) 편집 픽셀을 덮는다. RPM으론 못 이기던 타이밍을 훅으로 확보.
// 오버레이 소스 = %TEMP%\cds_ship_atlas.bin (정확히 0x12000B = 4슬롯×18432, 48×48 팔레트인덱스).
//   파일 없으면 무변경(원본 그대로). gid0~3 = 코구/다우, 카라벨, 카락, 갤리온 (선종→gid 테이블 0x5695D8).
#define ATLAS_OVERLAY_SZ  0x12000u          // 4슬롯 (0x5D68C8 ~ 0x5E88C8 직전; 그 뒤는 타 게임데이터라 침범 금지)
static BYTE g_overlayBuf[ATLAS_OVERLAY_SZ];

// 오버레이/추출 폴더 = <게임폴더>\CDS95Util\shipskin  (배포용 영속 경로). menu.c 와 공유.
void ShipSkin_OverlayDir(wchar_t* out)
{
    wchar_t exe[MAX_PATH]; wchar_t* p;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) { out[0] = 0; return; }
    p = wcsrchr(exe, L'\\'); if (p) *p = 0;           // 게임폴더
    wsprintfW(out, L"%s\\CDS95Util\\shipskin", exe);
}
void ShipSkin_OverlayPath(wchar_t* out)
{
    wchar_t dir[MAX_PATH];
    ShipSkin_OverlayDir(dir);
    wsprintfW(out, L"%s\\ship_atlas.bin", dir);
}

// dst(0x5D68C8)에 오버레이 파일을 덮는다. 우선순위: 게임폴더 shipskin\ship_atlas.bin (메뉴 배포용),
// 없으면 %TEMP%\cds_ship_atlas.bin (cds-ship-atlas.ps1 도구용) 폴백.
static BOOL TryOverlayFrom(const wchar_t* path, void* dst)
{
    HANDLE f; DWORD rd = 0; BOOL ok = FALSE;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    if (ReadFile(f, g_overlayBuf, ATLAS_OVERLAY_SZ, &rd, NULL) && rd == ATLAS_OVERLAY_SZ)
    {
        // all-zero(빈) 오버레이 가드: 손상/빈 파일을 적용하면 배가 안 보이므로 스킵(=바닐라 유지).
        DWORD i, nz = 0;
        for (i = 0; i < ATLAS_OVERLAY_SZ; i += 64) { if (g_overlayBuf[i]) { nz++; if (nz >= 8) break; } }
        if (nz >= 8) { memcpy(dst, g_overlayBuf, ATLAS_OVERLAY_SZ); ok = TRUE; }
        else OutputDebugStringW(L"[ShipSkinKR] overlay looks empty - skipped (vanilla).");
    }
    CloseHandle(f);
    return ok;
}

static void ApplyAtlasOverlay(void* dst)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH];
    ShipSkin_OverlayPath(path);
    if (path[0] && TryOverlayFrom(path, dst))
    {
        OutputDebugStringW(L"[ShipSkinKR] atlas overlay applied (shipskin, resIndex 1).");
        return;
    }
    if (GetTempPathW(MAX_PATH, tmp))
    {
        wsprintfW(path, L"%scds_ship_atlas.bin", tmp);
        if (TryOverlayFrom(path, dst))
            OutputDebugStringW(L"[ShipSkinKR] atlas overlay applied (TEMP, resIndex 1).");
    }
}

// 최초 바닐라 아틀라스를 shipskin\vanilla.bin 으로 1회 저장 (창의 배별 "원본 복원"용).
// dst 는 원본 Decode 직후(정규화 前)라 투명영역이 키색 인덱스다 → 게임과 동일하게 정규화
// (첫 픽셀=키, 그 값을 0=투명으로)한 뒤 저장해야 창 미리보기 배경이 투명(정상)으로 나온다.
static void SaveVanillaOnce(const void* atlas)
{
    wchar_t path[MAX_PATH], dir[MAX_PATH]; HANDLE f; DWORD wr; DWORD i; BYTE key;
    ShipSkin_OverlayDir(dir);
    if (!dir[0]) return;
    CreateDirectoryW(dir, NULL);
    wsprintfW(path, L"%s\\vanilla.bin", dir);
    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, 0, NULL);  // 이미 있으면 실패=스킵
    if (f == INVALID_HANDLE_VALUE) return;
    memcpy(g_overlayBuf, atlas, ATLAS_OVERLAY_SZ);            // 재사용 버퍼로 복사 후 정규화
    key = g_overlayBuf[0];
    for (i = 0; i < ATLAS_OVERLAY_SZ; i++) if (g_overlayBuf[i] == key) g_overlayBuf[i] = 0;
    WriteFile(f, g_overlayBuf, ATLAS_OVERLAY_SZ, &wr, NULL);
    CloseHandle(f);
}

static int __fastcall DetourDecode(void* thisptr, void* edx, int resIndex, void* dst)
{
    int r = g_origDecode(thisptr, edx, resIndex, dst);   // 원본 먼저(아틀라스 채움)
    LogDecode(thisptr, resIndex, dst, r);
    if (resIndex == 1) {                                  // 해상 배 아틀라스
        SaveVanillaOnce(dst);                             // 바닐라 스냅샷(1회)
        ApplyAtlasOverlay(dst);                           // 편집 오버레이
    }
    return r;
}

// ---- call-site 리다이렉트 설치/해제 ----
static BYTE g_origCall[5];
static BOOL g_callPatched = FALSE;

static void InstallGetterRedirect(void)
{
    BYTE* site = g_base + RVA_CALLSITE;
    DWORD getter = (DWORD)(g_base + RVA_GETTER);
    DWORD oldProt; int newRel;
    if (site[0] != 0xE8)   // 검증: call rel32
    {
        OutputDebugStringW(L"[ShipSkinKR] getter call-site: not E8 - skip (unsupported build).");
        return;
    }
    {
        int rel = *(int*)(site + 1);
        DWORD target = (DWORD)(site + 5) + (DWORD)rel;
        if (target != getter)
        {
            OutputDebugStringW(L"[ShipSkinKR] getter call-site: target mismatch - skip (unsupported build).");
            return;
        }
    }
    memcpy(g_origCall, site, 5);
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return;
    newRel = (int)((BYTE*)&DetourGetType - (site + 5));
    site[0] = 0xE8;
    *(int*)(site + 1) = newRel;
    VirtualProtect(site, 5, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_callPatched = TRUE;
    OutputDebugStringW(L"[ShipSkinKR] sea-sprite getter call-site redirected.");
}

static void RemoveGetterRedirect(void)
{
    BYTE* site;
    DWORD oldProt;
    if (!g_callPatched || !g_base) return;
    site = g_base + RVA_CALLSITE;
    if (VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        memcpy(site, g_origCall, 5);
        VirtualProtect(site, 5, oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_callPatched = FALSE;
}

// ---- 아틀라스 재배치: 8슬롯 버퍼 + 해상 draw 2곳 패치 ----
static BYTE* g_atlasBuf = NULL;
static BYTE  g_origDrawTable[7];
static BYTE  g_origLeaDisp[4];
static BOOL  g_atlasPatched = FALSE;

// PoC 검증용: 버퍼 주소를 %TEMP%\cds_shipbuf.txt 에 hex로 노출(외부 RPM 테스트).
static void WriteBufAddrFile(void)
{
    wchar_t path[MAX_PATH], tmp[MAX_PATH]; char buf[32]; HANDLE f; DWORD wr;
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    wsprintfW(path, L"%scds_shipbuf.txt", tmp);
    wsprintfA(buf, "%08X", (unsigned)(DWORD_PTR)g_atlasBuf);
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, buf, lstrlenA(buf), &wr, NULL); CloseHandle(f); }
}

static void InstallAtlasPatches(void)
{
    static const BYTE sigTable[7] = { 0x8B,0x0C,0x85,0xD8,0x95,0x56,0x00 }; // mov ecx,[eax*4+0x5695D8]
    static const BYTE sigLea[4]   = { 0xC8,0x68,0x5D,0x00 };                // disp32 = 0x005D68C8
    BYTE* pTable = g_base + RVA_DRAW_TABLE;
    BYTE* pLea   = g_base + RVA_DRAW_LEADISP;
    DWORD op;
    if (memcmp(pTable, sigTable, 7) != 0) { OutputDebugStringW(L"[ShipSkinKR] draw-table sig mismatch - atlas skip."); return; }
    if (memcmp(pLea,   sigLea,   4) != 0) { OutputDebugStringW(L"[ShipSkinKR] lea-disp sig mismatch - atlas skip."); return; }
    memcpy(g_origDrawTable, pTable, 7);
    memcpy(g_origLeaDisp,   pLea,   4);
    // 1) mov ecx,[eax*4+table] -> mov ecx,eax; and ecx,7; nop; nop  (type를 gid로 직접, 0~7 클램프)
    if (VirtualProtect(pTable, 7, PAGE_EXECUTE_READWRITE, &op))
    {
        static const BYTE patch[7] = { 0x8B,0xC8, 0x83,0xE1,0x07, 0x90,0x90 };
        memcpy(pTable, patch, 7);
        VirtualProtect(pTable, 7, op, &op);
        FlushInstructionCache(GetCurrentProcess(), pTable, 7);
    }
    // 2) lea edi,[eax+0x5D68C8] 의 disp32 -> 버퍼 주소
    if (VirtualProtect(pLea, 4, PAGE_EXECUTE_READWRITE, &op))
    {
        *(DWORD*)pLea = (DWORD)(DWORD_PTR)g_atlasBuf;
        VirtualProtect(pLea, 4, op, &op);
        FlushInstructionCache(GetCurrentProcess(), pLea, 4);
    }
    g_atlasPatched = TRUE;
    OutputDebugStringW(L"[ShipSkinKR] atlas relocated to plugin 8-slot buffer.");
}

static void RemoveAtlasPatches(void)
{
    DWORD op;
    if (!g_atlasPatched || !g_base) return;
    { BYTE* pTable = g_base + RVA_DRAW_TABLE;
      if (VirtualProtect(pTable, 7, PAGE_EXECUTE_READWRITE, &op)) { memcpy(pTable, g_origDrawTable, 7); VirtualProtect(pTable, 7, op, &op); FlushInstructionCache(GetCurrentProcess(), pTable, 7); } }
    { BYTE* pLea = g_base + RVA_DRAW_LEADISP;
      if (VirtualProtect(pLea, 4, PAGE_EXECUTE_READWRITE, &op)) { memcpy(pLea, g_origLeaDisp, 4); VirtualProtect(pLea, 4, op, &op); FlushInstructionCache(GetCurrentProcess(), pLea, 4); } }
    g_atlasPatched = FALSE;
}

// 아틀라스가 로드(SHIP.CDS 디코드)될 때까지 대기 후 시드+패치. (1회)
static DWORD WINAPI AtlasWorker(LPVOID param)
{
    BYTE* atlas = g_base + RVA_ATLAS;
    const int* table = (const int*)(g_base + RVA_TABLE_DATA);
    int i, t;
    (void)param;
    // 아틀라스 채워질 때까지 대기 (gid0 영역에 nonzero)
    for (;;)
    {
        int nz = 0;
        for (i = 0; i < (int)SHIP_SLOT_SZ; i += 256) if (atlas[i]) { nz = 1; break; }
        if (nz) break;
        Sleep(200);
    }
    g_atlasBuf = (BYTE*)VirtualAlloc(NULL, SHIP_BUF_SZ, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_atlasBuf) { OutputDebugStringW(L"[ShipSkinKR] atlas buffer alloc fail."); return 0; }
    // 시드: buf[type] = atlas[ table[type] ]  (원본 매핑대로)
    for (t = 0; t < (int)SHIP_NUM_SLOTS; t++)
    {
        int gid = table[t];
        if (gid < 0 || gid > 3) gid = 0;
        memcpy(g_atlasBuf + (SIZE_T)t * SHIP_SLOT_SZ, atlas + (SIZE_T)gid * SHIP_SLOT_SZ, SHIP_SLOT_SZ);
    }
    InstallAtlasPatches();     // 시드 후 패치 (draw가 빈 버퍼 읽는 것 방지)
    WriteBufAddrFile();
    return 0;
}

void InstallShipSkinHook(void)
{
    LPBYTE base = (LPBYTE)GetModuleHandleW(NULL);
    LPBYTE fn;
    if (!base) return;
    g_base = base;

    // 1) 해상 스프라이트: getter call-site 리다이렉트 (핵심)
    InstallGetterRedirect();

    // 1c) 아틀라스 로더 Decode(0x463680) 로깅 훅 (Step1 검증) — 시그니처(kSig) 검증 후.
    //     fleet-gfx 블록의 early-return 前에 설치(시그니처 불일치로 스킵되지 않게).
    {
        LPBYTE dec = base + RVA_DECODE;
        if (memcmp(dec, kSig, sizeof(kSig)) != 0)
            OutputDebugStringW(L"[ShipSkinKR] Decode(0x463680) signature mismatch - skip.");
        else if (MH_CreateHook(dec, &DetourDecode, (LPVOID*)&g_origDecode) != MH_OK ||
                 MH_EnableHook(dec) != MH_OK)
            OutputDebugStringW(L"[ShipSkinKR] Decode hook create/enable fail.");
        else
            OutputDebugStringW(L"[ShipSkinKR] Decode(0x463680) logging hook installed.");
    }

    // 1b) 아틀라스 재배치(8선종 고유 슬롯) — 미완성/보류(ana2-4).
    //     정적 디스어셈블 추측으로 잡은 0x48A877 경로가 실제 기함 draw가 아니어서 투명 버그 발생.
    //     CE "find what accesses 0x5D68C8"(항해 중)로 진짜 draw 명령 특정 후 재활성화 예정.
    //     그때까지 워커 비활성(재배치 미적용) — sp3-1(getter 리다이렉트)만 동작.
    // { HANDLE th = CreateThread(NULL, 0, AtlasWorker, NULL, 0, NULL); if (th) CloseHandle(th); }
    (void)&AtlasWorker;   // 미사용 경고 억제(코드 보존)

    // 2) 출항 함대그래픽: swap-restore 훅 (부가 커버) — 시그니처 검증
    fn = base + RVA_FLEETGFX;
    if (memcmp(fn, kSig, sizeof(kSig)) != 0)
    {
        OutputDebugStringW(L"[ShipSkinKR] fleet-gfx signature mismatch - skip (unsupported build).");
        return;
    }
    if (MH_CreateHook(fn, &DetourFleetGfx, (LPVOID*)&g_orig) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] CreateHook fail."); return; }
    if (MH_EnableHook(fn) != MH_OK) { OutputDebugStringW(L"[ShipSkinKR] EnableHook fail."); return; }
    OutputDebugStringW(L"[ShipSkinKR] hook installed (sea-sprite redirect + fleet-gfx swap).");
}

void RemoveShipSkinHook(void)
{
    RemoveAtlasPatches();
    RemoveGetterRedirect();
    MH_DisableHook(MH_ALL_HOOKS);
}
