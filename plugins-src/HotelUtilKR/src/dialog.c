#include "dialog.h"

// 게임의 계산기 UI(0x482xxx)는 게임 프레임 루프에 물려 돌아가는 대형 화면 클래스라
// Rest 훅(동기 호출) 안에서 안전하게 재사용하기 어렵다. 대신 자체 메시지 루프를 가지는
// Win32 모달 다이얼로그를 리소스 없이 in-memory 템플릿으로 만들어 띄운다.

#define IDC_EDIT 100

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        // lParam = 기본 일수. 미리 채우고 전체 선택.
        SetDlgItemInt(hDlg, IDC_EDIT, (UINT)(int)lp, FALSE);
        SendDlgItemMessageW(hDlg, IDC_EDIT, EM_SETSEL, 0, (LPARAM)-1);
        SetFocus(GetDlgItem(hDlg, IDC_EDIT));
        return FALSE; // 우리가 포커스를 지정했으므로 FALSE

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDOK:
        {
            BOOL ok = FALSE;
            int v = (int)GetDlgItemInt(hDlg, IDC_EDIT, &ok, FALSE);
            if (!ok || v < 1) v = 1;
            if (v > 127) v = 127;
            EndDialog(hDlg, v);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, 0); // 취소 -> 0 (호출측이 defaultDays 로 처리)
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// --- in-memory DLGTEMPLATE 빌더 ---
static void PutW(BYTE** p, WORD w)   { *(WORD*)(*p) = w;  *p += 2; }
static void PutDW(BYTE** p, DWORD d) { *(DWORD*)(*p) = d; *p += 4; }
static void PutStr(BYTE** p, const WCHAR* s) { while (*s) PutW(p, (WORD)*s++); PutW(p, 0); }
static void AlignDW(BYTE** p, BYTE* base) { while (((SIZE_T)(*p - base)) & 3) *(*p)++ = 0; }

int HotelKR_AskDays(int defaultDays)
{
    BYTE buf[1024];
    BYTE* base = buf;
    BYTE* p = buf;

    // DLGTEMPLATE 헤더
    PutDW(&p, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    PutDW(&p, 0);                 // exStyle
    PutW(&p, 4);                  // cdit = 컨트롤 4개
    PutW(&p, 0); PutW(&p, 0);     // x, y
    PutW(&p, 190); PutW(&p, 74);  // cx, cy (dialog units)
    PutW(&p, 0);                  // menu 없음
    PutW(&p, 0);                  // 기본 다이얼로그 클래스
    PutStr(&p, L"숙박 일수 입력");// 제목
    PutW(&p, 9);                  // 폰트 크기 (DS_SETFONT)
    PutStr(&p, L"맑은 고딕");     // 폰트

    // 1) STATIC 라벨
    AlignDW(&p, base);
    PutDW(&p, WS_CHILD | WS_VISIBLE | SS_LEFT); PutDW(&p, 0);
    PutW(&p, 12); PutW(&p, 8); PutW(&p, 166); PutW(&p, 12); PutW(&p, (WORD)-1);
    PutW(&p, 0xFFFF); PutW(&p, 0x0082); // STATIC
    PutStr(&p, L"며칠 숙박하시겠습니까? (1-127)");
    PutW(&p, 0);

    // 2) EDIT (숫자 전용)
    AlignDW(&p, base);
    PutDW(&p, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL); PutDW(&p, 0);
    PutW(&p, 12); PutW(&p, 24); PutW(&p, 166); PutW(&p, 14); PutW(&p, IDC_EDIT);
    PutW(&p, 0xFFFF); PutW(&p, 0x0081); // EDIT
    PutStr(&p, L"");
    PutW(&p, 0);

    // 3) 확인 (기본 버튼)
    AlignDW(&p, base);
    PutDW(&p, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON); PutDW(&p, 0);
    PutW(&p, 40); PutW(&p, 50); PutW(&p, 50); PutW(&p, 16); PutW(&p, IDOK);
    PutW(&p, 0xFFFF); PutW(&p, 0x0080); // BUTTON
    PutStr(&p, L"확인");
    PutW(&p, 0);

    // 4) 취소
    AlignDW(&p, base);
    PutDW(&p, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON); PutDW(&p, 0);
    PutW(&p, 100); PutW(&p, 50); PutW(&p, 50); PutW(&p, 16); PutW(&p, IDCANCEL);
    PutW(&p, 0xFFFF); PutW(&p, 0x0080);
    PutStr(&p, L"취소");
    PutW(&p, 0);

    {
        HWND parent = GetActiveWindow();
        INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL),
                                            (LPCDLGTEMPLATEW)buf, parent, DlgProc,
                                            (LPARAM)defaultDays);
        if (r <= 0)
            return defaultDays; // 취소/실패 -> 원래 일수 유지
        return (int)r;
    }
}
