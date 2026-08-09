#include "hkjson.h"

// hotkeys.json 은 우리가 쓰는 파일이라 모양이 정해져 있다. 그래서 온전한 JSON 파서 대신
// "따옴표 문자열"을 차례로 집는 스캐너로 읽는다(이스케이프는 안 쓴다).
//
//   { "Enabled": true, "Keys": { "정보": "I", "스폰서": "P", ... } }

// 플러그인이 CDS95Util\plugins\<만든이>\ 에 있으면 데이터는 그 위 CDS95Util 에 있다
// (charstate.c · questjson.c 와 같은 관용구 — 만든이별로 폴더를 나눠도 자료는 한 자리다).
static void UpToDataDir(wchar_t* dir)
{
    wchar_t tmp[MAX_PATH];
    int n, i, cut2 = -1, cut1 = -1;
    lstrcpynW(tmp, dir, MAX_PATH);
    n = lstrlenW(tmp);
    if (n && tmp[n-1] == L'\\') tmp[--n] = 0;
    for (i = n - 1; i >= 0; i--) {
        if (tmp[i] != L'\\') continue;
        if (cut2 < 0) cut2 = i;
        else { cut1 = i; break; }
    }
    if (cut1 < 0 || cut2 <= cut1) return;
    tmp[cut2] = 0;
    if (lstrcmpiW(tmp + cut1 + 1, L"plugins") != 0) return;
    tmp[cut1 + 1] = 0;
    lstrcpyW(dir, tmp);
}

void HkJson_Path(HINSTANCE hinst, wchar_t* out, int cch)
{
    wchar_t* q;
    wchar_t* slash = out;
    GetModuleFileNameW(hinst, out, cch);
    for (q = out; *q; q++) if (*q == L'\\' || *q == L'/') slash = q;
    slash[1] = 0;
    UpToDataDir(out);
    lstrcatW(out, L"hotkeys.json");
}

wchar_t* HkJson_Read(HINSTANCE hinst)
{
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD sz, got = 0;
    char* raw; wchar_t* w; int n; const char* p;

    HkJson_Path(hinst, path, MAX_PATH);
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 256u * 1024) { CloseHandle(h); return NULL; }
    raw = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
    if (!raw) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, raw, sz, &got, NULL)) { HeapFree(GetProcessHeap(), 0, raw); CloseHandle(h); return NULL; }
    raw[got] = 0;
    CloseHandle(h);
    p = raw;
    if (got >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;                                   // UTF-8 BOM
    n = MultiByteToWideChar(CP_UTF8, 0, p, -1, NULL, 0);
    w = n > 0 ? (wchar_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(wchar_t)) : NULL;
    if (w) MultiByteToWideChar(CP_UTF8, 0, p, -1, w, n);
    HeapFree(GetProcessHeap(), 0, raw);
    return w;
}

void HkJson_Free(wchar_t* buf)
{
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
}

// p 다음의 "따옴표 문자열"을 out 에 담고 닫는 따옴표 다음을 돌려준다. 더 없으면 NULL.
// 여는 { } 를 만나면 거기서 끊는다 — "Keys" 묶음의 끝을 알아채려는 것이다.
static const wchar_t* NextString(const wchar_t* p, wchar_t* out, int cch)
{
    int i = 0;
    while (*p && *p != L'"') { if (*p == L'}') return NULL; p++; }
    if (!*p) return NULL;
    p++;
    while (*p && *p != L'"') { if (i < cch - 1) out[i++] = *p; p++; }
    out[i] = 0;
    return *p ? p + 1 : NULL;
}

// 이름이 name 인 열쇠를 찾아 그 값이 시작되는 자리를 돌려준다(콜론과 공백은 지나서). 없으면 NULL.
static const wchar_t* FindKey(const wchar_t* buf, const wchar_t* name)
{
    const wchar_t* p;
    for (p = buf; *p; p++) {
        wchar_t k[32]; int j = 0; const wchar_t* q;
        if (*p != L'"') continue;
        q = p + 1;
        while (*q && *q != L'"' && j < 31) k[j++] = *q++;
        k[j] = 0;
        if (lstrcmpiW(k, name)) continue;
        while (*q && *q != L':') q++;
        while (*q == L':' || *q == L' ' || *q == L'\t' || *q == L'\r' || *q == L'\n') q++;
        return q;
    }
    return NULL;
}

int HkJson_Enabled(const wchar_t* buf)
{
    const wchar_t* v = buf ? FindKey(buf, L"Enabled") : NULL;
    if (!v) return 1;
    return (*v == L'f' || *v == L'F' || *v == L'0') ? 0 : 1;
}

int HkJson_KeyOf(const wchar_t* buf, const wchar_t* action, wchar_t* out, int cch)
{
    const wchar_t* p;
    wchar_t name[64], val[16];

    out[0] = 0;
    if (!buf) return 0;
    p = FindKey(buf, L"Keys");
    if (!p) return 0;
    while (*p && *p != L'{') p++;
    if (*p) p++;
    for (;;) {
        p = NextString(p, name, 64);
        if (!p) break;
        p = NextString(p, val, 16);
        if (!p) break;
        if (lstrcmpW(name, action)) continue;
        lstrcpynW(out, val, cch);                 // 빈 값이면 일부러 떼 놓은 자리다
        return 1;
    }
    return 0;
}
