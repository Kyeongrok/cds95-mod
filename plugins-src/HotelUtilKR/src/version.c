#include "version.h"

DWORD GetGameVersion(void)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH))
        return 0;

    DWORD unused = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &unused);
    if (size == 0)
        return 0;

    BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buffer)
        return 0;

    DWORD version = 0;
    if (GetFileVersionInfoW(path, 0, size, buffer))
    {
        VS_FIXEDFILEINFO* info = NULL;
        UINT infoSize = 0;
        if (VerQueryValueW(buffer, L"\\", (LPVOID*)&info, &infoSize) && info)
        {
            WORD major    = HIWORD(info->dwFileVersionMS);
            WORD minor    = LOWORD(info->dwFileVersionMS);
            WORD build    = HIWORD(info->dwFileVersionLS);
            WORD revision = LOWORD(info->dwFileVersionLS);
            version = major * 1000u + minor * 100u + build * 10u + revision;
        }
    }

    HeapFree(GetProcessHeap(), 0, buffer);
    return version;
}
