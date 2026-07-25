# Desktop Switch

A minimal Windows 11 tray icon application that switches virtual desktops via Alt+1 through Alt+9.

## How it works

Registers global hotkeys Alt+1..Alt+9. On keypress, calls `GoToDesktopNumber()` from [VirtualDesktopAccessor.dll](https://github.com/Ciantic/VirtualDesktopAccessor) to instantly jump to the target desktop by index. A system tray icon is shown; right-click it to exit.

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
| Alt+1  | Switch to desktop 1 |
| Alt+2  | Switch to desktop 2 |
| ...    | ... |
| Alt+9  | Switch to desktop 9 |
| Alt+`  | Switch to most recent desktop |

Right-click the tray icon to exit.

## Notes

- Only one instance can run at a time.
- If a hotkey fails to register (another app is using it), a warning is shown at startup.
- The target desktop must already exist — the app does not create new desktops.
