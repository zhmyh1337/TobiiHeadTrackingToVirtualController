#include "windows.h"


HWND GetConsoleHwnd(void)
// Copied and sligthly modified from https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/obtain-console-window-handle
// This is Microsofts suggested solution to replace GetConsoleWindow which is no longer working on Windows 11.
// See https://learn.microsoft.com/en-us/windows/console/getconsolewindow.
{
#define MY_BUFSIZE 1024 // Buffer size for console window titles.
    static HWND hwndFound = NULL;        // This is what is returned to the caller.
    if (hwndFound != NULL) return hwndFound;

    WCHAR pszNewWindowTitle[MY_BUFSIZE]; // Contains fabricated
    // WindowTitle.
    WCHAR pszOldWindowTitle[MY_BUFSIZE]; // Contains original
    // WindowTitle.

// Fetch current window title.

    GetConsoleTitle(pszOldWindowTitle, MY_BUFSIZE);

    // Format a "unique" NewWindowTitle.

    wsprintf(pszNewWindowTitle, L"%d/%d",
        GetTickCount(),
        GetCurrentProcessId());

    // Change current window title.

    SetConsoleTitle(pszNewWindowTitle);

    // Ensure window title has been updated.

    Sleep(40);

    // Look for NewWindowTitle.

    hwndFound = FindWindow(NULL, pszNewWindowTitle);

    // Restore original window title.

    SetConsoleTitle(pszOldWindowTitle);

    return(hwndFound);
}