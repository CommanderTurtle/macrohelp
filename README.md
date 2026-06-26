# Macrohelp — a developer's best friend

### A lightweight overlay runtime. (~300kb harness) for Tasket++ 

Focused on QoL, coordinate capture, buffers, zone math, and translating intent into native Tasket `.scht` work through `tasket-httpd` daemon.

The daemon is a natively built sidecar for a running Tasket instance, with full GET support for schedules and availability of a local tasket session.

Macrohelp bundles machine code into readable instances that any automation friend would like to have on their machine.

Not only is the harness capable of spawning macros and workflow stacks chained with if-then sequencing, but macrohelp natively visualizes the unit circle, and performs dot product translations and Unit-Circle visuals for moving the mouse off relative location. The final feature is a Dance-Dance revolution style keyboard history stylized with open-source [prompt-font](https://shinmera.com/docs/promptfont/)

Want to get into .ani cursors? Check out the bilbata rainbow fork for windows [here](https://github.com/CommanderTurtle/Bibata_Cursor_Rainbow_2.0) — (includes info on .ani and the windows .inf cursor-style for animating)

---

## Run Existing Build

```powershell
cd macrohelp-runtime/bin
.\CursorOverlay.exe
```

## Rebuild From Source (first step requirement, ensuring latest VS build tools)

```powershell
cd macrohelp-runtime/source
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
# Start program:
.\build-vs\bin\Release\CursorOverlay.exe
```

Fast rebuild loop:

```powershell
# Kill the process (no startup hooks, very portable — can close from system tray too)
Get-Process CursorOverlay -ErrorAction SilentlyContinue | Stop-Process -Force
cd macrohelp-runtime/source
# Build again:
cmake --build build-vs --config Release
# Start program:
.\build-vs\bin\Release\CursorOverlay.exe
```

## Runtime Assumptions

- Tasket++ is installed and working.
- `tasket-httpd.exe` is running on `http://127.0.0.1:7777`.
- The canonical Tasket saved task folder is usually:

```powershell
$env:APPDATA\Tasket++\saved_tasks
```

## Current Hotkeys

```text
Shift+Alt+1  Save current cursor coordinate to Macrohelp JSON.
Shift+Alt+2  Record key/mouse sequence, including paste blocks.
Shift+Alt+3  Circle and zone placement.
Shift+Alt+4  Left-click helper.
Shift+Alt+5  Right-click helper.
Shift+Alt+6  Middle-click helper.
Shift+Alt+7  Stop all Tasket tasks through the daemon.
Shift+Alt+8  View toggles: crosshair and cursor-relative grid.
Shift+Alt+9  Registry Hub / command workflow surface.
Shift+Alt+0  Paste and zone buffers.
```

More info on applicable screens. Everything is pretty intuitive. Backend is fancy json parsing and structure mimicry, with no native hooks to mouse or keyboard control.

`{powershell}` machine code assumes 'Win+Alt+Space [Command Pallet](https://learn.microsoft.com/en-us/windows/powertoys/) has a first pinned entry of powershell preview. Set to your favorite shell. 
> This is backend for all machine code, a spawnable shell must be easily reached this way.


## Notes

- Circle movement and zone actions use Tasket schedules through the daemon, not direct WinUI cursor mutation.
- Paste buffers and zone buffers are deliberately simple: Macrohelp stores values, then emits Tasket JSON when asked.
- Registry Hub is the command surface for composing these primitives. It is not meant to be a native automation engine.
- View [macrohard](https://app.shel.sh/macro) runtime environment when 'macro too hard, give me UI' <-- a 300kb vite app mimicing comfy's style for emitting/importing machine code. [(Source Code)](https://github.com/CommanderTurtle/orc/tree/main/app/macro)
