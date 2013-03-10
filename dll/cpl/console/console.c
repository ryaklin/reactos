/*
 * PROJECT:         ReactOS Console Configuration DLL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            dll/win32/console/console.c
 * PURPOSE:         initialization of DLL
 * PROGRAMMERS:     Johannes Anderwald (johannes.anderwald@student.tugraz.at)
 */

#include "console.h"

#define NUM_APPLETS 1

LONG APIENTRY InitApplet(HWND hwnd, UINT uMsg, LPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK OptionsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FontProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK LayoutProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ColorsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

HINSTANCE hApplet = 0;

/* Applets */
APPLET Applets[NUM_APPLETS] =
{
    {IDC_CPLICON, IDS_CPLNAME, IDS_CPLDESCRIPTION, InitApplet}
};

/*
 * Default 16-color palette for foreground and background
 * (corresponding flags in comments).
 */
const COLORREF s_Colors[16] =
{
    RGB(0, 0, 0),       // (Black)
    RGB(0, 0, 128),     // BLUE
    RGB(0, 128, 0),     // GREEN
    RGB(0, 128, 128),   // BLUE  | GREEN
    RGB(128, 0, 0),     // RED
    RGB(128, 0, 128),   // BLUE  | RED
    RGB(128, 128, 0),   // GREEN | RED
    RGB(192, 192, 192), // BLUE  | GREEN | RED

    RGB(128, 128, 128), // (Grey)  INTENSITY
    RGB(0, 0, 255),     // BLUE  | INTENSITY
    RGB(0, 255, 0),     // GREEN | INTENSITY
    RGB(0, 255, 255),   // BLUE  | GREEN | INTENSITY
    RGB(255, 0, 0),     // RED   | INTENSITY
    RGB(255, 0, 255),   // BLUE  | RED   | INTENSITY
    RGB(255, 255, 0),   // GREEN | RED   | INTENSITY
    RGB(255, 255, 255)  // BLUE  | GREEN | RED | INTENSITY
};
/* Default attributes */
#define DEFAULT_SCREEN_ATTRIB   (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED)
#define DEFAULT_POPUP_ATTRIB    (FOREGROUND_BLUE | FOREGROUND_RED | \
                                 BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED | BACKGROUND_INTENSITY)
/* Cursor size */
#define CSR_DEFAULT_CURSOR_SIZE 25

static VOID
InitPropSheetPage(PROPSHEETPAGE *psp,
                  WORD idDlg,
                  DLGPROC DlgProc,
                  LPARAM lParam)
{
    ZeroMemory(psp, sizeof(PROPSHEETPAGE));
    psp->dwSize = sizeof(PROPSHEETPAGE);
    psp->dwFlags = PSP_DEFAULT;
    psp->hInstance = hApplet;
    psp->pszTemplate = MAKEINTRESOURCE(idDlg);
    psp->pfnDlgProc = DlgProc;
    psp->lParam = lParam;
}

PCONSOLE_PROPS
AllocConsoleInfo()
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CONSOLE_PROPS));
}

VOID
InitConsoleDefaults(PCONSOLE_PROPS pConInfo)
{
    /* Initialize the default properties */
    pConInfo->ci.HistoryBufferSize = 50;
    pConInfo->ci.NumberOfHistoryBuffers = 4;
    pConInfo->ci.HistoryNoDup = FALSE;
    pConInfo->ci.FullScreen = FALSE;
    pConInfo->ci.QuickEdit = FALSE;
    pConInfo->ci.InsertMode = TRUE;
    // pConInfo->ci.InputBufferSize;
    pConInfo->ci.ScreenBufferSize = (COORD){80, 300};
    pConInfo->ci.ConsoleSize = (COORD){80, 25};
    pConInfo->ci.CursorBlinkOn = TRUE;
    pConInfo->ci.ForceCursorOff = FALSE;
    pConInfo->ci.CursorSize = CSR_DEFAULT_CURSOR_SIZE;
    pConInfo->ci.ScreenAttrib = DEFAULT_SCREEN_ATTRIB;
    pConInfo->ci.PopupAttrib  = DEFAULT_POPUP_ATTRIB;
    pConInfo->ci.CodePage = 0;
    pConInfo->ci.ConsoleTitle[0] = L'\0';

    pConInfo->ci.u.GuiInfo.FaceName[0] = L'\0';
    pConInfo->ci.u.GuiInfo.FontFamily = FF_DONTCARE;
    pConInfo->ci.u.GuiInfo.FontSize = 0;
    pConInfo->ci.u.GuiInfo.FontWeight = FW_DONTCARE;
    pConInfo->ci.u.GuiInfo.UseRasterFonts = TRUE;

    pConInfo->ci.u.GuiInfo.AutoPosition = TRUE;
    pConInfo->ci.u.GuiInfo.WindowOrigin = (POINT){0, 0};

    memcpy(pConInfo->ci.Colors, s_Colors, sizeof(s_Colors));
}

INT_PTR
CALLBACK
ApplyProc(HWND hwndDlg,
          UINT uMsg,
          WPARAM wParam,
          LPARAM lParam)
{
    HWND hDlgCtrl;

    UNREFERENCED_PARAMETER(lParam);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            hDlgCtrl = GetDlgItem(hwndDlg, IDC_RADIO_APPLY_CURRENT);
            SendMessage(hDlgCtrl, BM_SETCHECK, BST_CHECKED, 0);
            return TRUE;
        }
        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDOK)
            {
                hDlgCtrl = GetDlgItem(hwndDlg, IDC_RADIO_APPLY_CURRENT);
                if (SendMessage(hDlgCtrl, BM_GETCHECK, 0, 0) == BST_CHECKED)
                    EndDialog(hwndDlg, IDC_RADIO_APPLY_CURRENT);
                else
                    EndDialog(hwndDlg, IDC_RADIO_APPLY_ALL);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDlg, IDCANCEL);
            }
            break;
        }
        default:
            break;
    }

    return FALSE;
}

BOOL
ApplyConsoleInfo(HWND hwndDlg,
                 PCONSOLE_PROPS pConInfo)
{
    INT_PTR res = 0;

    res = DialogBox(hApplet, MAKEINTRESOURCE(IDD_APPLYOPTIONS), hwndDlg, ApplyProc);
    if (res == IDCANCEL)
    {
        /* Don't destroy when user presses cancel */
        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
        return TRUE;
    }
    else if (res == IDC_RADIO_APPLY_ALL || res == IDC_RADIO_APPLY_CURRENT)
    {
        HANDLE hSection;
        PCONSOLE_PROPS pSharedInfo;

        /* Create a memory section to share with the server, and map it */
        hSection = CreateFileMapping(INVALID_HANDLE_VALUE,
                                     NULL,
                                     PAGE_READWRITE,
                                     0,
                                     sizeof(CONSOLE_PROPS),
                                     NULL);
        if (!hSection)
        {
            // DPRINT1("Error when creating file mapping, error = %d\n", GetLastError());
            return FALSE;
        }

        pSharedInfo = MapViewOfFile(hSection, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!pSharedInfo)
        {
            // DPRINT1("Error when mapping view of file, error = %d\n", GetLastError());
            CloseHandle(hSection);
            return FALSE;
        }

        /* We are applying the chosen configuration */
        pConInfo->AppliedConfig = TRUE;

        /* Copy the console info into the section */
        RtlCopyMemory(pSharedInfo, pConInfo, sizeof(CONSOLE_PROPS));

        /* Unmap it */
        UnmapViewOfFile(pSharedInfo);

        /* Signal to the console server that it can apply the new configuration */
        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
        SendMessage(pConInfo->hConsoleWindow,
                    PM_APPLY_CONSOLE_INFO,
                    (WPARAM)hSection,
                    (LPARAM)(res == IDC_RADIO_APPLY_ALL));

        /* Close the section and return */
        CloseHandle(hSection);
        return TRUE;
    }

    return TRUE;
}

/* First Applet */
LONG APIENTRY
InitApplet(HWND hWnd, UINT uMsg, LPARAM wParam, LPARAM lParam)
{
    HANDLE hSection = (HANDLE)wParam;
    PCONSOLE_PROPS pSharedInfo;
    PCONSOLE_PROPS pConInfo;
    WCHAR szTitle[MAX_PATH + 1];
    PROPSHEETPAGE psp[4];
    PROPSHEETHEADER psh;
    INT i = 0;

    UNREFERENCED_PARAMETER(uMsg);

    /*
     * CONSOLE.DLL shares information with CONSRV with wParam:
     * wParam is a handle to a shared section holding a CONSOLE_PROPS struct.
     *
     * NOTE: lParam is not used.
     */

    /* Allocate a local buffer to hold console information */
    pConInfo = AllocConsoleInfo();
    if (!pConInfo) return 0;

    /* Map the shared section */
    pSharedInfo = MapViewOfFile(hSection, FILE_MAP_READ, 0, 0, 0);
    if (pSharedInfo == NULL)
    {
        HeapFree(GetProcessHeap(), 0, pConInfo);
        return 0;
    }

    // if (IsBadReadPtr((PVOID)pSharedInfo, sizeof(CONSOLE_PROPS)))
    // {
    // }

    /* Find the console window and whether we must use default parameters */
    pConInfo->hConsoleWindow    = pSharedInfo->hConsoleWindow;
    pConInfo->ShowDefaultParams = pSharedInfo->ShowDefaultParams;

    if (pConInfo->ShowDefaultParams)
    {
        /* Use defaults */
        InitConsoleDefaults(pConInfo);
    }
    else
    {
        /* Copy the shared data into our allocated buffer */
        RtlCopyMemory(pConInfo, pSharedInfo, sizeof(CONSOLE_PROPS));
    }

    /* Close the section */
    UnmapViewOfFile(pSharedInfo);
    CloseHandle(hSection);

    /* Initialize the property sheet structure */
    ZeroMemory(&psh, sizeof(PROPSHEETHEADER));
    psh.dwSize = sizeof(PROPSHEETHEADER);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_PROPTITLE | /* PSH_USEHICON */ PSH_USEICONID | PSH_NOAPPLYNOW;

    if (pConInfo->ci.ConsoleTitle[0] != L'\0')
    {
        wcsncpy(szTitle, L"\"", sizeof(szTitle) / sizeof(szTitle[0]));
        wcsncat(szTitle, pConInfo->ci.ConsoleTitle, sizeof(szTitle) / sizeof(szTitle[0]));
        wcsncat(szTitle, L"\"", sizeof(szTitle) / sizeof(szTitle[0]));
    }
    else
    {
        wcscpy(szTitle, L"ReactOS Console");
    }
    psh.pszCaption = szTitle;

    psh.hwndParent = pConInfo->hConsoleWindow;
    psh.hInstance = hApplet;
    // psh.hIcon = LoadIcon(hApplet, MAKEINTRESOURCE(IDC_CPLICON));
    psh.pszIcon = MAKEINTRESOURCE(IDC_CPLICON);
    psh.nPages = 4;
    psh.nStartPage = 0;
    psh.ppsp = psp;

    InitPropSheetPage(&psp[i++], IDD_PROPPAGEOPTIONS, (DLGPROC) OptionsProc, (LPARAM)pConInfo);
    InitPropSheetPage(&psp[i++], IDD_PROPPAGEFONT, (DLGPROC) FontProc, (LPARAM)pConInfo);
    InitPropSheetPage(&psp[i++], IDD_PROPPAGELAYOUT, (DLGPROC) LayoutProc, (LPARAM)pConInfo);
    InitPropSheetPage(&psp[i++], IDD_PROPPAGECOLORS, (DLGPROC) ColorsProc, (LPARAM)pConInfo);

    return (PropertySheet(&psh) != -1);
}

/* Control Panel Callback */
LONG CALLBACK
CPlApplet(HWND hwndCPl,
          UINT uMsg,
          LPARAM lParam1,
          LPARAM lParam2)
{
    switch (uMsg)
    {
        case CPL_INIT:
            return TRUE;

        case CPL_GETCOUNT:
            return NUM_APPLETS;

        case CPL_INQUIRE:
        {
            CPLINFO *CPlInfo = (CPLINFO*)lParam2;
            CPlInfo->idIcon = Applets[0].idIcon;
            CPlInfo->idName = Applets[0].idName;
            CPlInfo->idInfo = Applets[0].idDescription;
            break;
        }

        case CPL_DBLCLK:
            InitApplet(hwndCPl, uMsg, lParam1, lParam2);
            break;
    }

    return FALSE;
}


INT
WINAPI
DllMain(HINSTANCE hinstDLL,
        DWORD     dwReason,
        LPVOID    lpvReserved)
{
    UNREFERENCED_PARAMETER(lpvReserved);

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
            hApplet = hinstDLL;
            break;
    }

    return TRUE;
}
