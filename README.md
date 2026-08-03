# Desktop Switch

A minimal Windows 11 tray icon application that switches virtual desktops via global hotkeys, moves windows between desktops, and properly restores focus on the target desktop.

## How it works

Registers global hotkeys for desktop switching and window moving. Delegates virtual desktop operations to [VirtualDesktopAccessor.dll](https://github.com/Ciantic/VirtualDesktopAccessor) which wraps undocumented Windows 11 `IVirtualDesktopManagerInternal` COM interfaces.

Before switching desktops, the app calls `AllowSetForegroundWindow(ASFW_ANY)` to relinquish its foreground activation rights. This allows the OS to restore focus to the previously active window on the target desktop — the same behavior as the native Ctrl+Win+Arrow shortcuts.

## Prerequisites

- Windows 11
- Visual Studio 2022 (MSVC C++ toolchain)
- [Rust toolchain](https://rustup.rs/) (for building VirtualDesktopAccessor.dll)
- CMake 3.20+

## Building

### CMake (recommended)

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Output: `build/Release/DesktopSwitch.exe` + `VirtualDesktopAccessor.dll`

For a debug build:

```
cmake --build build --config Debug
```

## Usage

Run `DesktopSwitch.exe`. It sits in the system tray and responds to:

| Hotkey | Action |
|--------|--------|
| Alt+1..9 | Switch to desktop N |
| Alt+` | Switch to most recent desktop |
| Ctrl+Alt+1..9 | Move the active window to desktop N and follow |

Right-click the tray icon to exit.

## Notes

- Only one instance can run at a time.
- If a hotkey fails to register (another app is using it), a warning is shown at startup.
- The target desktop must already exist — the app does not create new desktops.
- Ctrl+Alt hotkeys are only registered if the DLL exports `MoveWindowToDesktopNumber`.
