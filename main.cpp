#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// --- VirtualDesktopAccessor.dll function pointers ---

typedef int (__cdecl *GoToDesktopNumberProc)(int desktopNumber);
typedef int (__cdecl *GetDesktopCountProc)();
typedef int (__cdecl *GetCurrentDesktopNumberProc)();
typedef int (__cdecl *MoveWindowToDesktopNumberProc)(HWND hwnd, int desktopNumber);
typedef int (__cdecl *RegisterPostMessageHookProc)(HWND hwnd, UINT message_offset);
typedef void (__cdecl *UnregisterPostMessageHookProc)(HWND hwnd);

static GoToDesktopNumberProc GoToDesktopNumber = nullptr;
static GetDesktopCountProc GetDesktopCount = nullptr;
static GetCurrentDesktopNumberProc GetCurrentDesktopNumber = nullptr;
static MoveWindowToDesktopNumberProc MoveWindowToDesktopNumber = nullptr;
static RegisterPostMessageHookProc RegisterPostMessageHook = nullptr;
static UnregisterPostMessageHookProc UnregisterPostMessageHook = nullptr;

// --- Globals ---

static NOTIFYICONDATAW g_nid = {};
static HMENU g_hMenu = nullptr;
static const UINT WM_TRAYICON = WM_USER + 1;
static const UINT WM_DESKTOP_CHANGED = WM_USER + 2;
static const UINT HOTKEY_BASE = 1000;
static const UINT HOTKEY_LAST = 1100;
static const UINT HOTKEY_MOVE_BASE = 1200;
static const UINT HOTKEY_SEND_BASE = 1300;
static const UINT TIMER_VALIDATE_DESKTOP = 1;
static const UINT DESKTOP_DWELL_DELAY_MS = 1200;

// Desktop history.  g_settledDesktop is the last desktop we dwelled on long
// enough to count as a real visit; g_lastDesktop is the one we dwelled on
// before that.  Intermediate desktops passed through during a fast burst of
// switches (e.g. repeated Ctrl+Win+Right) never enter the history, so Alt+`
// always returns to the desktop the burst started from.
static int g_settledDesktop = -1;
static int g_lastDesktop = -1;

// --- DLL Loading ---

static HMODULE LoadVDA() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    // Strip filename, append DLL name
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"VirtualDesktopAccessor.dll");

    HMODULE hDll = LoadLibraryW(path);
    if (!hDll) {
        MessageBoxW(nullptr,
            L"Failed to load VirtualDesktopAccessor.dll.\n"
            L"Place it next to DesktopSwitch.exe.",
            L"Desktop Switch", MB_ICONERROR);
        return nullptr;
    }

    GoToDesktopNumber = (GoToDesktopNumberProc)GetProcAddress(hDll, "GoToDesktopNumber");
    GetDesktopCount = (GetDesktopCountProc)GetProcAddress(hDll, "GetDesktopCount");
    GetCurrentDesktopNumber = (GetCurrentDesktopNumberProc)GetProcAddress(hDll, "GetCurrentDesktopNumber");
    MoveWindowToDesktopNumber = (MoveWindowToDesktopNumberProc)GetProcAddress(hDll, "MoveWindowToDesktopNumber");
    RegisterPostMessageHook = (RegisterPostMessageHookProc)GetProcAddress(hDll, "RegisterPostMessageHook");
    UnregisterPostMessageHook = (UnregisterPostMessageHookProc)GetProcAddress(hDll, "UnregisterPostMessageHook");

    if (!GoToDesktopNumber) {
        MessageBoxW(nullptr,
            L"VirtualDesktopAccessor.dll loaded but GoToDesktopNumber not found.\n"
            L"The DLL may be an incompatible version.",
            L"Desktop Switch", MB_ICONERROR);
        FreeLibrary(hDll);
        return nullptr;
    }

    return hDll;
}

// --- Desktop Switching ---

static void OnDesktopDwellTimer(HWND hwnd) {
    KillTimer(hwnd, TIMER_VALIDATE_DESKTOP);

    int current = GetCurrentDesktopNumber ? GetCurrentDesktopNumber() : -1;
    if (current < 0) return;

    // We stayed here long enough - this desktop becomes the settled one and the
    // previously settled one becomes the Alt+` target.  A burst that ends where
    // it started leaves the history untouched.
    if (current != g_settledDesktop) {
        g_lastDesktop = g_settledDesktop;
        g_settledDesktop = current;
    }
}

static void StartDesktopDwellTimer(HWND hwnd) {
    // Restart the dwell timer.  Every switch defers the commit, so only the
    // desktop we finally rest on is recorded.
    KillTimer(hwnd, TIMER_VALIDATE_DESKTOP);
    SetTimer(hwnd, TIMER_VALIDATE_DESKTOP, DESKTOP_DWELL_DELAY_MS, nullptr);
}

static bool SwitchToDesktop(HWND hwnd, int index) {
    if (!GoToDesktopNumber) return false;

    int current = GetCurrentDesktopNumber ? GetCurrentDesktopNumber() : -1;

    // Skip if already on this desktop
    if (current == index) return false;

    // Skip if desktop doesn't exist
    if (GetDesktopCount && index >= GetDesktopCount()) return false;

    // History is updated by the dwell timer, started from WM_DESKTOP_CHANGED,
    // which fires for all switches (both our shortcuts and Windows shortcuts)

    // This allows restoring the last active window on this desktop automatically
    // Without `AllowSetForegroundWindow` the window on the last desktop stays active
    // and accepts input.  This also removes the task bar application icons blinking.
    AllowSetForegroundWindow(ASFW_ANY);
    GoToDesktopNumber(index);
    return true;
}

static void SwitchToLastDesktop(HWND hwnd) {
    int current = GetCurrentDesktopNumber ? GetCurrentDesktopNumber() : -1;
    if (current < 0) return;

    // If the current desktop hasn't settled yet we're still mid-switch, so go
    // back to where the switching started.  Once settled, toggle to the desktop
    // visited before it.
    int target = (current != g_settledDesktop) ? g_settledDesktop : g_lastDesktop;
    if (target < 0) return;

    // Commit the jump immediately so a second Alt+` toggles straight back
    // instead of waiting out the dwell timer.
    if (SwitchToDesktop(hwnd, target)) {
        g_lastDesktop = current;
        g_settledDesktop = target;
    }
}

static void MoveActiveWindowToDesktop(HWND msgHwnd, int index, bool follow) {
    if (!MoveWindowToDesktopNumber) return;
    if (follow && !GoToDesktopNumber) return;

    int current = GetCurrentDesktopNumber ? GetCurrentDesktopNumber() : -1;
    if (current == index) return;
    if (GetDesktopCount && index >= GetDesktopCount()) return;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;

    if (MoveWindowToDesktopNumber(hwnd, index) < 0) return;

    // If following, SwitchToDesktop will handle the desktop switch and validation
    if (follow) {
        SwitchToDesktop(msgHwnd, index);
        SetForegroundWindow(hwnd);
    }
}

// --- Icon ---

static HICON CreateDesktopSwitchIcon() {
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcColor = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    SelectObject(hdcColor, hbmColor);

    RECT rc = {0, 0, cx, cy};

    int pad = cx / 8;
    int size = cx * 5 / 8;
    int offset = cx * 3 / 8;
    int radius = cx / 8;

    // Color bitmap: black rectangles on black background
    FillRect(hdcColor, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // Mask: white = transparent, black = opaque
    HDC hdcMask = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr);
    SelectObject(hdcMask, hbmMask);
    FillRect(hdcMask, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

    HPEN hpenNull = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdcMask, GetStockObject(BLACK_BRUSH));
    SelectObject(hdcMask, hpenNull);
    RoundRect(hdcMask, pad, pad, pad + size, pad + size, radius, radius);
    RoundRect(hdcMask, offset, offset, offset + size, offset + size, radius, radius);

    DeleteObject(hpenNull);
    DeleteDC(hdcMask);
    DeleteDC(hdcColor);
    ReleaseDC(nullptr, hdcScreen);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmMask);
    DeleteObject(hbmColor);
    return hIcon;
}

// --- Tray Icon ---

static void CreateTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = CreateDesktopSwitchIcon();
    wcscpy_s(g_nid.szTip, L"Desktop Switch (Alt, Ctrl+Alt, Shift+Ctrl+Alt)+1..9");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// --- Window Procedure ---

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam >= HOTKEY_BASE && wParam < HOTKEY_BASE + 9) {
            SwitchToDesktop(hwnd, (int)(wParam - HOTKEY_BASE));
        } else if (wParam == HOTKEY_LAST) {
            SwitchToLastDesktop(hwnd);
        } else if (wParam >= HOTKEY_MOVE_BASE && wParam < HOTKEY_MOVE_BASE + 9) {
            MoveActiveWindowToDesktop(hwnd, (int)(wParam - HOTKEY_MOVE_BASE), true);
        } else if (wParam >= HOTKEY_SEND_BASE && wParam < HOTKEY_SEND_BASE + 9) {
            MoveActiveWindowToDesktop(hwnd, (int)(wParam - HOTKEY_SEND_BASE), false);
        }
        return 0;

    case WM_DESKTOP_CHANGED:
        // Any desktop change - ours or a Windows shortcut (Ctrl+Win+Left/Right)
        // - defers the history update until we stop moving.
        StartDesktopDwellTimer(hwnd);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_VALIDATE_DESKTOP) {
            OnDesktopDwellTimer(hwnd);
        }
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            PostQuitMessage(0);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Entry Point ---

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"DesktopSwitch_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Desktop Switch is already running.", L"Desktop Switch", MB_ICONINFORMATION);
        return 0;
    }

    HMODULE hVDA = LoadVDA();
    if (!hVDA) return 1;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DesktopSwitchClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Desktop Switch",
                                0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);

    // The desktop we start on is the initial anchor for Alt+`
    g_settledDesktop = GetCurrentDesktopNumber ? GetCurrentDesktopNumber() : -1;

    // Register for desktop change notifications from VirtualDesktopAccessor.dll
    if (RegisterPostMessageHook) {
        if (RegisterPostMessageHook(hwnd, WM_DESKTOP_CHANGED) < 0) {
            MessageBoxW(nullptr,
                L"Failed to register for desktop change notifications.\n"
                L"Alt+` may not work correctly with Windows shortcuts.",
                L"Desktop Switch", MB_ICONWARNING);
        }
    }

    int registered = 0;
    for (int i = 0; i < 9; i++) {
        if (RegisterHotKey(hwnd, HOTKEY_BASE + i, MOD_ALT | MOD_NOREPEAT, '1' + i)) {
            registered++;
        }
    }
    // Alt+` (backtick/grave) to switch to last desktop
    if (RegisterHotKey(hwnd, HOTKEY_LAST, MOD_ALT | MOD_NOREPEAT, VK_OEM_3)) {
        registered++;
    }

    // Ctrl+Alt+1..9 to move active window to desktop and follow
    if (MoveWindowToDesktopNumber) {
        for (int i = 0; i < 9; i++) {
            if (RegisterHotKey(hwnd, HOTKEY_MOVE_BASE + i, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, '1' + i)) {
                registered++;
            }
        }
        // Shift+Ctrl+Alt+1..9 to move active window to desktop without switching
        for (int i = 0; i < 9; i++) {
            if (RegisterHotKey(hwnd, HOTKEY_SEND_BASE + i, MOD_SHIFT | MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, '1' + i)) {
                registered++;
            }
        }
    }

    if (registered == 0) {
        MessageBoxW(nullptr,
            L"Failed to register any hotkeys.\n"
            L"Another application may be using Alt+1..9.",
            L"Desktop Switch", MB_ICONERROR);
        FreeLibrary(hVDA);
        return 1;
    }

    g_hMenu = CreatePopupMenu();
    AppendMenuW(g_hMenu, MF_STRING, 1, L"Exit");
    CreateTrayIcon(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (int i = 0; i < 9; i++) {
        UnregisterHotKey(hwnd, HOTKEY_BASE + i);
    }
    UnregisterHotKey(hwnd, HOTKEY_LAST);
    for (int i = 0; i < 9; i++) {
        UnregisterHotKey(hwnd, HOTKEY_MOVE_BASE + i);
    }
    for (int i = 0; i < 9; i++) {
        UnregisterHotKey(hwnd, HOTKEY_SEND_BASE + i);
    }

    // Unregister desktop change notifications
    if (UnregisterPostMessageHook) {
        UnregisterPostMessageHook(hwnd);
    }

    // Kill any pending validation timer
    KillTimer(hwnd, TIMER_VALIDATE_DESKTOP);

    RemoveTrayIcon();
    DestroyMenu(g_hMenu);
    FreeLibrary(hVDA);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
