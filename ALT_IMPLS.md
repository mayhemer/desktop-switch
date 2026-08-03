# Alternative Focus Restoration: EnumWindows Query

If the current `AllowSetForegroundWindow(ASFW_ANY)` approach stops working (e.g. on a
future Windows build where the OS no longer internally restores per-desktop focus), here
is an alternative that queries live state after each switch instead of storing HWNDs.

## Approach

After `GoToDesktopNumber` returns, enumerate top-level windows in z-order using
`EnumWindows`. The first visible window confirmed to be on the current virtual desktop
(via `IsWindowOnCurrentVirtualDesktop` from VirtualDesktopAccessor.dll) is the one to
focus with `SetForegroundWindow`.

## Implementation

```cpp
typedef int (__cdecl *IsWindowOnCurrentVirtualDesktopProc)(HWND hwnd);
static IsWindowOnCurrentVirtualDesktopProc IsWindowOnCurrentVirtualDesktop = nullptr;

// Load in LoadVDA():
// IsWindowOnCurrentVirtualDesktop = (IsWindowOnCurrentVirtualDesktopProc)
//     GetProcAddress(hDll, "IsWindowOnCurrentVirtualDesktop");

static BOOL CALLBACK FindTopWindowOnDesktop(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;  // skip owned windows

    if (IsWindowOnCurrentVirtualDesktop && IsWindowOnCurrentVirtualDesktop(hwnd)) {
        *(HWND*)lParam = hwnd;
        return FALSE;  // stop enumeration
    }
    return TRUE;
}

// Use after GoToDesktopNumber(index):
// HWND target = nullptr;
// EnumWindows(FindTopWindowOnDesktop, (LPARAM)&target);
// if (target) SetForegroundWindow(target);
```

## Trade-offs

- No stored state, no stale handles
- Slightly more expensive per switch (walks the window list), but negligible in practice
- Requires loading one additional DLL export (`IsWindowOnCurrentVirtualDesktop`)
- Deterministic: always picks the topmost z-order window on the target desktop
