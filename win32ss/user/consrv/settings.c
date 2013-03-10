/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Console Server DLL
 * FILE:            win32ss/user/consrv/settings.c
 * PURPOSE:         Consoles settings management
 * PROGRAMMERS:     Hermes Belusca - Maito
 *
 * NOTE: Adapted from existing code.
 */

/* INCLUDES *******************************************************************/

#include "consrv.h"
#include "conio.h"
#include "settings.h"

#include <stdio.h> // for swprintf

#define NDEBUG
#include <debug.h>


/* GLOBALS ********************************************************************/

extern const COLORREF s_Colors[16];


/* FUNCTIONS ******************************************************************/

static VOID
TranslateConsoleName(OUT LPWSTR DestString,
                     IN LPCWSTR ConsoleName,
                     IN UINT MaxStrLen)
{
#define PATH_SEPARATOR L'\\'

    UINT wLength;

    if ( DestString   == NULL  || ConsoleName  == NULL ||
         *ConsoleName == L'\0' || MaxStrLen    == 0 )
    {
        return;
    }

    wLength = GetWindowsDirectoryW(DestString, MaxStrLen);
    if ((wLength > 0) && (_wcsnicmp(ConsoleName, DestString, wLength) == 0))
    {
        wcsncpy(DestString, L"%SystemRoot%", MaxStrLen);
        // FIXME: Fix possible buffer overflows there !!!!!
        wcsncat(DestString, ConsoleName + wLength, MaxStrLen);
    }
    else
    {
        wcsncpy(DestString, ConsoleName, MaxStrLen);
    }

    /* Replace path separators (backslashes) by underscores */
    while ((DestString = wcschr(DestString, PATH_SEPARATOR))) *DestString = L'_';
}

static BOOL
OpenUserRegistryPathPerProcessId(DWORD ProcessId,
                                 PHKEY hResult,
                                 REGSAM samDesired)
{
    BOOL bRet = TRUE;
    HANDLE hProcessToken = NULL;
    HANDLE hProcess;
    BYTE Buffer[256];
    DWORD Length = 0;
    UNICODE_STRING SidName;
    PTOKEN_USER TokUser;

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | READ_CONTROL, FALSE, ProcessId);
    if (!hProcess)
    {
        DPRINT("Error: OpenProcess failed(0x%x)\n", GetLastError());
        return FALSE;
    }

    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hProcessToken))
    {
        DPRINT("Error: OpenProcessToken failed(0x%x)\n", GetLastError());
        CloseHandle(hProcess);
        return FALSE;
    }

    if (!GetTokenInformation(hProcessToken, TokenUser, (PVOID)Buffer, sizeof(Buffer), &Length))
    {
        DPRINT("Error: GetTokenInformation failed(0x%x)\n",GetLastError());
        CloseHandle(hProcessToken);
        CloseHandle(hProcess);
        return FALSE;
    }

    TokUser = ((PTOKEN_USER)Buffer)->User.Sid;
    if (!NT_SUCCESS(RtlConvertSidToUnicodeString(&SidName, TokUser, TRUE)))
    {
        DPRINT("Error: RtlConvertSidToUnicodeString failed(0x%x)\n", GetLastError());
        CloseHandle(hProcessToken);
        CloseHandle(hProcess);
        return FALSE;
    }

    bRet = (RegOpenKeyExW(HKEY_USERS,
                          SidName.Buffer,
                          0,
                          samDesired,
                          hResult) == ERROR_SUCCESS);

    RtlFreeUnicodeString(&SidName);

    CloseHandle(hProcessToken);
    CloseHandle(hProcess);

    return bRet;
}

BOOL
ConSrvOpenUserSettings(DWORD ProcessId,
                       LPCWSTR ConsoleTitle,
                       PHKEY hSubKey,
                       REGSAM samDesired,
                       BOOL bCreate)
{
    BOOL bRet = TRUE;
    WCHAR szBuffer[MAX_PATH] = L"Console\\";
    WCHAR szBuffer2[MAX_PATH] = L"";
    HKEY hKey;

    /*
     * Console properties are stored under the HKCU\Console\* key.
     *
     * We use the original console title as the subkey name for storing
     * console properties. We need to distinguish whether we were launched
     * via the console application directly or via a shortcut.
     *
     * If the title of the console corresponds to a path (more precisely,
     * if the title is of the form: C:\ReactOS\<some_path>\<some_app.exe>),
     * then use the corresponding unexpanded path and with the backslashes
     * replaced by underscores, to make the registry happy,
     *     i.e. %SystemRoot%_<some_path>_<some_app.exe>
     */

    /* Open the registry key where we saved the console properties */
    if (!OpenUserRegistryPathPerProcessId(ProcessId, &hKey, samDesired))
    {
        DPRINT1("OpenUserRegistryPathPerProcessId failed\n");
        return FALSE;
    }

    /*
     * Try to open properties via the console title:
     * to make the registry happy, replace all the
     * backslashes by underscores.
     */
    TranslateConsoleName(szBuffer2, ConsoleTitle, MAX_PATH);

    /* Create the registry path */
    wcsncat(szBuffer, szBuffer2, MAX_PATH);

    /* Create or open the registry key */
    if (bCreate)
    {
        /* Create the key */
        bRet = (RegCreateKeyExW(hKey,
                                szBuffer,
                                0, NULL,
                                REG_OPTION_NON_VOLATILE,
                                samDesired,
                                NULL,
                                hSubKey,
                                NULL) == ERROR_SUCCESS);
    }
    else
    {
        /* Open the key */
        bRet = (RegOpenKeyExW(hKey,
                              szBuffer,
                              0,
                              samDesired,
                              hSubKey) == ERROR_SUCCESS);
    }

    /* Close the parent key and return success or not */
    RegCloseKey(hKey);
    return bRet;
}

BOOL
ConSrvReadUserSettings(IN OUT PCONSOLE_INFO ConsoleInfo,
                       IN DWORD ProcessId)
{
    HKEY  hKey;
    DWORD dwNumSubKeys = 0;
    DWORD dwIndex;
    DWORD dwValueName;
    DWORD dwValue;
    DWORD dwColorIndex = 0;
    DWORD dwType;
    WCHAR szValueName[MAX_PATH];
    WCHAR szValue[LF_FACESIZE] = L"\0";
    DWORD Value;

    if (!ConSrvOpenUserSettings(ProcessId,
                                ConsoleInfo->ConsoleTitle,
                                &hKey, KEY_READ,
                                FALSE))
    {
        DPRINT("ConSrvOpenUserSettings failed\n");
        return FALSE;
    }

    if (RegQueryInfoKey(hKey, NULL, NULL, NULL, NULL, NULL, NULL,
                        &dwNumSubKeys, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
    {
        DPRINT("ConSrvReadUserSettings: RegQueryInfoKey failed\n");
        RegCloseKey(hKey);
        return FALSE;
    }

    DPRINT("ConSrvReadUserSettings entered dwNumSubKeys %d\n", dwNumSubKeys);

    for (dwIndex = 0; dwIndex < dwNumSubKeys; dwIndex++)
    {
        dwValue = sizeof(Value);
        dwValueName = MAX_PATH;

        if (RegEnumValueW(hKey, dwIndex, szValueName, &dwValueName, NULL, &dwType, (BYTE*)&Value, &dwValue) != ERROR_SUCCESS)
        {
            if (dwType == REG_SZ)
            {
                /*
                 * Retry in case of string value
                 */
                dwValue = sizeof(szValue);
                dwValueName = LF_FACESIZE;
                if (RegEnumValueW(hKey, dwIndex, szValueName, &dwValueName, NULL, NULL, (BYTE*)szValue, &dwValue) != ERROR_SUCCESS)
                    break;
            }
            else
            {
                break;
            }
        }

        if (!wcsncmp(szValueName, L"ColorTable", wcslen(L"ColorTable")))
        {
            dwColorIndex = 0;
            swscanf(szValueName, L"ColorTable%2d", &dwColorIndex);
            if (dwColorIndex < sizeof(ConsoleInfo->Colors)/sizeof(ConsoleInfo->Colors[0]))
            {
                ConsoleInfo->Colors[dwColorIndex] = Value;
            }
        }
        else if (!wcscmp(szValueName, L"HistoryBufferSize"))
        {
            ConsoleInfo->HistoryBufferSize = Value;
        }
        else if (!wcscmp(szValueName, L"NumberOfHistoryBuffers"))
        {
            ConsoleInfo->NumberOfHistoryBuffers = Value;
        }
        else if (!wcscmp(szValueName, L"HistoryNoDup"))
        {
            ConsoleInfo->HistoryNoDup = Value;
        }
        else if (!wcscmp(szValueName, L"FullScreen"))
        {
            ConsoleInfo->FullScreen = Value;
        }
        else if (!wcscmp(szValueName, L"QuickEdit"))
        {
            ConsoleInfo->QuickEdit = Value;
        }
        else if (!wcscmp(szValueName, L"InsertMode"))
        {
            ConsoleInfo->InsertMode = Value;
        }
        else if (!wcscmp(szValueName, L"ScreenBufferSize"))
        {
            ConsoleInfo->ScreenBufferSize.X = LOWORD(Value);
            ConsoleInfo->ScreenBufferSize.Y = HIWORD(Value);
        }
        else if (!wcscmp(szValueName, L"WindowSize"))
        {
            ConsoleInfo->ConsoleSize.X = LOWORD(Value);
            ConsoleInfo->ConsoleSize.Y = HIWORD(Value);
        }
        else if (!wcscmp(szValueName, L"CursorSize"))
        {
            ConsoleInfo->CursorSize = min(max(Value, 0), 100);
        }
        else if (!wcscmp(szValueName, L"ScreenColors"))
        {
            ConsoleInfo->ScreenAttrib = Value;
        }
        else if (!wcscmp(szValueName, L"PopupColors"))
        {
            ConsoleInfo->PopupAttrib = Value;
        }
        else if (!wcscmp(szValueName, L"FaceName"))
        {
            wcsncpy(ConsoleInfo->u.GuiInfo.FaceName, szValue, LF_FACESIZE);
        }
        else if (!wcscmp(szValueName, L"FontFamily"))
        {
            ConsoleInfo->u.GuiInfo.FontFamily = Value;
        }
        else if (!wcscmp(szValueName, L"FontSize"))
        {
            ConsoleInfo->u.GuiInfo.FontSize = Value;
        }
        else if (!wcscmp(szValueName, L"FontWeight"))
        {
            ConsoleInfo->u.GuiInfo.FontWeight = Value;
        }
        else if (!wcscmp(szValueName, L"WindowPosition"))
        {
            ConsoleInfo->u.GuiInfo.AutoPosition = FALSE;
            ConsoleInfo->u.GuiInfo.WindowOrigin.x = LOWORD(Value);
            ConsoleInfo->u.GuiInfo.WindowOrigin.y = HIWORD(Value);
        }
    }

    RegCloseKey(hKey);
    return TRUE;
}

BOOL
ConSrvWriteUserSettings(IN PCONSOLE_INFO ConsoleInfo,
                        IN DWORD ProcessId)
{
    BOOL GlobalSettings = (ConsoleInfo->ConsoleTitle[0] == L'\0');
    HKEY hKey;
    DWORD Storage = 0;

#define SetConsoleSetting(SettingName, SettingType, SettingSize, Setting, DefaultValue)         \
do {                                                                                            \
    if (GlobalSettings || (!GlobalSettings && (*(Setting) != (DefaultValue))))                  \
    {                                                                                           \
        RegSetValueExW(hKey, (SettingName), 0, (SettingType), (PBYTE)(Setting), (SettingSize)); \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
        RegDeleteValue(hKey, (SettingName));                                                    \
    }                                                                                           \
} while (0)

    WCHAR szValueName[15];
    UINT i;

    if (!ConSrvOpenUserSettings(ProcessId,
                                ConsoleInfo->ConsoleTitle,
                                &hKey, KEY_WRITE,
                                TRUE))
    {
        return FALSE;
    }

    for (i = 0 ; i < sizeof(ConsoleInfo->Colors)/sizeof(ConsoleInfo->Colors[0]) ; ++i)
    {
        /*
         * Write only the new value if we are saving the global settings
         * or we are saving settings for a particular console, which differs
         * from the default ones.
         */
        swprintf(szValueName, L"ColorTable%02d", i);
        SetConsoleSetting(szValueName, REG_DWORD, sizeof(DWORD), &ConsoleInfo->Colors[i], s_Colors[i]);
    }

    SetConsoleSetting(L"HistoryBufferSize", REG_DWORD, sizeof(DWORD), &ConsoleInfo->HistoryBufferSize, 50);
    SetConsoleSetting(L"NumberOfHistoryBuffers", REG_DWORD, sizeof(DWORD), &ConsoleInfo->NumberOfHistoryBuffers, 4);

    Storage = ConsoleInfo->HistoryNoDup;
    SetConsoleSetting(L"HistoryNoDup", REG_DWORD, sizeof(DWORD), &Storage, FALSE);

    Storage = ConsoleInfo->FullScreen;
    SetConsoleSetting(L"FullScreen", REG_DWORD, sizeof(DWORD), &Storage, FALSE);

    Storage = ConsoleInfo->QuickEdit;
    SetConsoleSetting(L"QuickEdit", REG_DWORD, sizeof(DWORD), &Storage, FALSE);

    Storage = ConsoleInfo->InsertMode;
    SetConsoleSetting(L"InsertMode", REG_DWORD, sizeof(DWORD), &Storage, TRUE);

    Storage = MAKELONG(ConsoleInfo->ScreenBufferSize.X, ConsoleInfo->ScreenBufferSize.Y);
    SetConsoleSetting(L"ScreenBufferSize", REG_DWORD, sizeof(DWORD), &Storage, MAKELONG(80, 300));

    Storage = MAKELONG(ConsoleInfo->ConsoleSize.X, ConsoleInfo->ConsoleSize.Y);
    SetConsoleSetting(L"WindowSize", REG_DWORD, sizeof(DWORD), &Storage, MAKELONG(80, 25));

    SetConsoleSetting(L"CursorSize", REG_DWORD, sizeof(DWORD), &ConsoleInfo->CursorSize, CSR_DEFAULT_CURSOR_SIZE);

    Storage = ConsoleInfo->ScreenAttrib;
    SetConsoleSetting(L"ScreenColors", REG_DWORD, sizeof(DWORD), &Storage, DEFAULT_SCREEN_ATTRIB);

    Storage = ConsoleInfo->PopupAttrib;
    SetConsoleSetting(L"PopupColors", REG_DWORD, sizeof(DWORD), &Storage, DEFAULT_POPUP_ATTRIB);

    SetConsoleSetting(L"FaceName", REG_SZ, (wcslen(ConsoleInfo->u.GuiInfo.FaceName) + 1) * sizeof(WCHAR), ConsoleInfo->u.GuiInfo.FaceName, L'\0');
    SetConsoleSetting(L"FontFamily", REG_DWORD, sizeof(DWORD), &ConsoleInfo->u.GuiInfo.FontFamily, FF_DONTCARE);
    SetConsoleSetting(L"FontSize", REG_DWORD, sizeof(DWORD), &ConsoleInfo->u.GuiInfo.FontSize, 0);
    SetConsoleSetting(L"FontWeight", REG_DWORD, sizeof(DWORD), &ConsoleInfo->u.GuiInfo.FontWeight, FW_DONTCARE);

    if (ConsoleInfo->u.GuiInfo.AutoPosition == FALSE)
    {
        Storage = MAKELONG(ConsoleInfo->u.GuiInfo.WindowOrigin.x, ConsoleInfo->u.GuiInfo.WindowOrigin.y);
        RegSetValueExW(hKey, L"WindowPosition", 0, REG_DWORD, (PBYTE)&ConsoleInfo->u.GuiInfo.WindowOrigin, sizeof(DWORD));
    }
    else
    {
        RegDeleteValue(hKey, L"WindowPosition");
    }

    RegCloseKey(hKey);
    return TRUE;
}

VOID
ConSrvGetDefaultSettings(IN OUT PCONSOLE_INFO ConsoleInfo,
                         IN DWORD ProcessId)
{
    if (ConsoleInfo == NULL) return;

/// HKCU,"Console","LoadConIme",0x00010003,1

    /*
     * 1. Load the default values
     */
// #define DEFAULT_HISTORY_COMMANDS_NUMBER 50
// #define DEFAULT_HISTORY_BUFFERS_NUMBER 4
    ConsoleInfo->HistoryBufferSize = 50;
    ConsoleInfo->NumberOfHistoryBuffers = 4;
    ConsoleInfo->HistoryNoDup = FALSE;

    ConsoleInfo->FullScreen = FALSE;
    ConsoleInfo->QuickEdit  = FALSE;
    ConsoleInfo->InsertMode = TRUE;
    // ConsoleInfo->InputBufferSize;
    ConsoleInfo->ScreenBufferSize = (COORD){80, 300};
    ConsoleInfo->ConsoleSize      = (COORD){80, 25 };

    ConsoleInfo->CursorBlinkOn;
    ConsoleInfo->ForceCursorOff;
    ConsoleInfo->CursorSize = CSR_DEFAULT_CURSOR_SIZE; // #define SMALL_SIZE 25

    ConsoleInfo->ScreenAttrib = DEFAULT_SCREEN_ATTRIB;
    ConsoleInfo->PopupAttrib  = DEFAULT_POPUP_ATTRIB;

    memcpy(ConsoleInfo->Colors, s_Colors, sizeof(s_Colors));

    // ConsoleInfo->CodePage;

    ConsoleInfo->ConsoleTitle[0] = L'\0';

    // wcsncpy(ConsoleInfo->u.GuiInfo.FaceName, L"DejaVu Sans Mono", LF_FACESIZE);
    // ConsoleInfo->u.GuiInfo.FontSize = MAKELONG(12, 8); // 0x0008000C; // font is 8x12
    // ConsoleInfo->u.GuiInfo.FontSize = MAKELONG(16, 16); // font is 16x16
    // ConsoleInfo->u.GuiInfo.FontWeight = FW_NORMAL;

    wcsncpy(ConsoleInfo->u.GuiInfo.FaceName, L"Fixedsys", LF_FACESIZE); // HACK: !!
    // ConsoleInfo->u.GuiInfo.FaceName[0] = L'\0';
    ConsoleInfo->u.GuiInfo.FontFamily = FF_DONTCARE;
    ConsoleInfo->u.GuiInfo.FontSize = 0;
    ConsoleInfo->u.GuiInfo.FontWeight = FW_DONTCARE;
    ConsoleInfo->u.GuiInfo.UseRasterFonts = TRUE;

    ConsoleInfo->u.GuiInfo.ShowWindow   = SW_SHOWNORMAL;
    ConsoleInfo->u.GuiInfo.AutoPosition = TRUE;
    ConsoleInfo->u.GuiInfo.WindowOrigin = (POINT){0, 0};

    /*
     * 2. Overwrite them with the ones stored in HKCU\Console.
     *    If the HKCU\Console key doesn't exist, create it
     *    and store the default values inside.
     */
    if (!ConSrvReadUserSettings(ConsoleInfo, ProcessId))
    {
        ConSrvWriteUserSettings(ConsoleInfo, ProcessId);
    }
}

/* EOF */
