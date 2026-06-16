# Macrohelp BETA v2.0

A native Windows overlay tool with cursor crosshairs, real-time coordinate tracking, and live key press visualization. Inspired by Microsoft PowerToys Mouse Pointer Crosshairs, built for macro recording with Tasket-compatible JSON output.

## What's New in v2.0

- **Live Key Display** - See every key you press rendered in real-time beside your cursor
- **PromptFont Integration** - Beautiful keyboard glyph icons (by Shinmera) for all keys
- **5-Second Fade History** - Keys fade out over 5 seconds with history numbering (-10 to current)
- **Pulsing Active Combo** - Currently held keys pulse to indicate active state
- **Per-Pixel Alpha Blending** - GDI+ rendering with `UpdateLayeredWindow` for smooth transparency
- **VS 2022/2025 Hardened** - `/permissive-` strict conformance, modern C++17

## Features

- **Full-Screen Crosshair Overlay** - Red lines + yellow center dot across all monitors
- **Real-Time Coordinates** - Live X/Y position at 60+ FPS in a dark themed panel
<p><span style="font-size:18px; color:#bbb;">^ Live, updating coordinates (refreshed at 60fps right under mouse cursor)</span></p>
- **Live Key Visualization** - Every key press shown with icon glyphs, fading over 5 seconds
- **Global Hotkeys** - `Shift+Alt+1` and `Shift+Alt+2` work over any overlay
- **Cursor Position Saving** - Save coordinates as Tasket `cursormovements` JSON
- **Key Sequence Recording** - Record keyboard sequences as Tasket `keyssequence` JSON
- **Manual Key Input** - Dropdown menus for all 80+ supported Tasket key names
- **Auto Wait Entries** - Every save automatically followed by `0.2s` wait block
- **Continuous Append** - All output appends to `clicksession.txt` in Downloads
- **Modeless Dialogs** - Crosshair and key display track while dialogs are open
- **System Tray** - Right-click menu with show/hide toggles for all panels

## Building

### Visual Studio 2022/2025 (Recommended)

Open **"x64 Native Tools Command Prompt for VS"**:
```batch
build.bat
```
Output: `build\CursorOverlay.exe`

### MinGW-w64 (via MSYS2)

```batch
build_mingw.bat
```
Output: `build_mingw\CursorOverlay.exe`

### CMake

```batch
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## Usage

### Running

Double-click `CursorOverlay.exe`. You will see:
- **Red crosshairs** centered on your cursor across all monitors
- **Coordinate panel** in the top-right showing live X/Y
- **Key display** below your cursor showing pressed keys in real-time
- **System tray icon** for quick access

### Live Key Display

The key display appears below your cursor and shows:
- **Current combo** (pulsing) - keys you're currently holding
- **Recent history** - finalized combos fading out over 5 seconds
- **History numbers** - `-1`, `-2`, ... `-10` for past entries

### Shift+Alt+1: Save Cursor Position

1. Move cursor to target position
2. Press `Shift+Alt+1`
3. Enter a name (e.g., `clickleftwindow`)
4. Click **"Add Another"** to capture additional points
5. Click **OK** to save

### Shift+Alt+2: Record Key Sequence

1. Press `Shift+Alt+2`
2. Enter a name (e.g., `COPYALL`)
3. Click **"Capture Key"** then press your key(s)
4. Click **"Add Another Key"** to build combos (e.g., Ctrl+A)
5. Click **"Add Step"** for new steps in the sequence
6. Click **"Manual Input"** to select keys from dropdowns
7. Click **OK** to save

### Tray Menu

Right-click the tray icon to:
- Show/Hide crosshairs
- Show/Hide coordinate panel
- Show/Hide key display
- Trigger save cursor / record keys
- Exit

## Output Format

Every save appends **two blocks** to `%USERPROFILE%\Downloads\clicksession.txt`:

### Cursor Movement

```json
        {
            "cursorMovsId": "clickleftwindow",
            "cursormovsmap": [
                [
                    0,
                    100,
                    1191,
                    38
                ]
            ],
            "loop": 1,
            "optionalkeysstroke": [
            ],
            "type": "cursormovements"
        },
        {
            "duration": 0.2,
            "type": "wait"
        },
```

### Key Sequence

```json
        {
            "keysSeqId": "COPYALL",
            "keysmap": {
                "0": [
                    100,
                    [
                        "CONTROL_LEFT",
                        "A"
                    ]
                ],
                "200": [
                    100,
                    [
                        "CONTROL_LEFT",
                        "C"
                    ]
                ]
            },
            "loop": 1,
            "type": "keyssequence"
        },
        {
            "duration": 0.2,
            "type": "wait"
        },
```

## Architecture

```
CursorOverlay/
├── src/
│   ├── main.cpp          (~1700 lines, full application)
│   └── resource.h        (resource definitions)
├── res/
│   ├── CursorOverlay.rc  (dialogs, icons, font binary)
│   ├── cursor.ico        (application icon)
│   ├── cursor_small.ico  (small icon)
│   ├── promptfont.ttf    (PromptFont by Shinmera)
│   ├── promptfont.h      (PromptFont glyph definitions)
│   └── LICENSE.txt       (PromptFont OFL license)
├── build.bat             (MSVC build)
├── build_mingw.bat       (MinGW build)
├── CMakeLists.txt        (CMake configuration)
└── README.md
```

### Technical Details

| Feature | Implementation |
|---------|---------------|
| Crosshair | `WS_EX_LAYERED` colorkey window, `WM_TIMER` at 16ms |
| Key display | Separate `WS_EX_LAYERED` window with `UpdateLayeredWindow` + `ULW_ALPHA` |
| Alpha rendering | GDI+ on 32bpp DIB section with per-pixel alpha |
| Key glyphs | PromptFont loaded via `AddFontMemResourceEx` from embedded RC binary |
| Key tracking | `WH_KEYBOARD_LL` hook, always active, thread-safe circular buffer |
| Fade effect | Linear opacity over 5s: `opacity = 1.0 - age / 5000ms` |
| Active combo | Sine pulse: `0.6 + 0.4 * sin(time * 2PI)` |
| Hotkeys | `RegisterHotKey` with `MOD_SHIFT \| MOD_ALT` |
| Dialogs | Modeless (`CreateDialogParamW`) with `IsDialogMessage` |
| Multi-monitor | `SM_CXVIRTUALSCREEN` / `SM_CYVIRTUALSCREEN` |
| File output | `std::ofstream` append mode, JSON with exact Tasket spacing |

### PromptFont Key Glyphs

The live key display uses PromptFont (by Shinmera, SIL OFL) for beautiful keyboard icons:

| Key | Glyph | Key | Glyph |
|-----|-------|-----|-------|
| Ctrl | `␧` | Shift | `␩` |
| Alt | `␨` | Win/Super | `␪` |
| Fn | `␦` | AltGr | `⑊` |
| Left Mouse | `➊` | Right Mouse | `➋` |
| Middle Mouse | `➌` | Tab | `␫` |
| Enter | `␮` | Escape | `␯` |
| Backspace | `␭` | Space | `␺` |
| Arrow keys | `⏴⏵⏶⏷` | Delete | `␷` |

Keys without special glyphs (F1-F12, A-Z, 0-9, numpad) render as clean text.

## License

CursorOverlay is open source. PromptFont by Shinmera (Yukari Hafner) is used under the SIL Open Font License 1.1.
