# Macrohelp — a developer's best friend

### A lightweight overlay runtime. (~300kb harness) for Tasket++ 

Focused on QoL, coordinate capture, buffers, zone math, and translating intent into native Tasket `.scht` work through `tasket-httpd` daemon.

The daemon is a natively built sidecar for a running Tasket instance, with full GET support for schedules and availability of a local tasket session.

Macrohelp bundles machine code into readable instances that any automation friend would like to have on their machine.

Not only is the harness capable of spawning macros and workflow stacks chained with if-then sequencing, but macrohelp natively visualizes the unit circle, and performs dot product translations and Unit-Circle visuals for moving the mouse off relative location. The final feature is a Dance-Dance revolution style keyboard history stylized with open-source [prompt-font](https://shinmera.com/docs/promptfont/)

Want to get into .ani cursors? Check out the bilbata rainbow fork for windows [here](https://github.com/CommanderTurtle/Bibata_Cursor_Rainbow_2.0) — (includes info on .ani and the windows .inf cursor-style for animating)

#### See the Quick Start [here](./quick-start.md)

Neat features worth noting:

- Native Circle Placement tool (uses either i+j coordinate offsets in circular grid / or radian/degrees)
- Paste library (great for quickly writing reused code snippets)
- Keyboard history + Coodinates right beside the mouse cursor
- Zone creation with the circle tool
- And of course rusable zones and workflows (looks almost like N8N)
- Program can build entire dynamic workflows easily with oneliner scripts. Example: discord automation that checks for incoming messages from a user, and a zone immediately grabs from the library of copypastas for that particular user.

Combine with a secondary framework! like:

- [Surfingkeys hotkeys](https://www.youtube.com/watch?v=QZO80CZB9Lw) (rip elements and navigate like vim in the browser)
- [Powershell here strings](https://docs.shel.sh/xml-project) (paste a long multiline delimited with @' and '@ then see if it prints true or false)
- [regedited](https://docs.shel.sh/projects/regedited) (rust) or another database. Regedited is another project of mine that allows indexed code segments in a markdown file, allowing one to append the clipboard instantly with a snippet segment (more scale than SimpleSnippet or Scripter for Command Palette)

---

For the httpd daemon, see the [quick start](./quick-start.md) since automated powershell is likely the best route for targetable latest Qt upstream! (Temporary cache dir nuked, as well, full Qt/VS opt-outs for telemetry)

## Runtime Assumptions

- Tasket++ is installed and working.
- `tasket-httpd.exe` is running on `http://127.0.0.1:7777`.
- The canonical Tasket saved task folder is usually:

```powershell
$env:APPDATA\Tasket++\saved_tasks
```

If you want to use the script playground:

- Assumed Win+Alt+Space = Powertoys Command-Palette hotkey
- Assumed favorite terminal (like Terminal Preview or Powershell Preview) is TOP-pinned command-palette

If you want to use the text-extractor zones:

- Assumed Win+Shift+Y = Powertoys Text-Extractor hotkey

If you want bash and python shells to work:

- Make sure they're on path
- `winget install --source winget --id Git.Git`
- `winget install --source winget --id Python.Python.3.12`
- Win+R `sysdm.cpl` > Advanced > Environment Variables > System variables PATH + `Edit` :
- C:\Users\myuser\AppData\Local\Programs\Git\bin\

Else, manually script them after `{powershell}`, example:

```plain
Assumed store of `uv venv .... bunch of stuff`, then `uv run python` == variable Z (buffered script)
{powershell}{Z}{enter}
```

```powershell
$env:APPDATA\Tasket++\saved_tasks
```

---

# Current Hotkeys

```text
Shift+Alt+1  Paste and zone buffers.
Shift+Alt+2  Assembly playground -> Further, 'Q' is views
Shift+Alt+3  Circle and zone placement.
Shift+Alt+4  Left-click helper.
Shift+Alt+5  Right-click helper.
Shift+Alt+6  Middle-click helper.
Shift+Alt+7  Stop all Tasket tasks through the daemon.
Shift+Alt+8  Toggle Top-Right Panel
Shift+Alt+9  Save JSONs for MouseMove.
Shift+Alt+0  Save JSONs for KeySequence.
```

More info on applicable screens. Everything is pretty intuitive. Backend has no native hooks to mouse or keyboard control without Tasket.

---

## Notes

- Circle movement and zone actions use Tasket schedules through the daemon, not direct WinUI cursor mutation.
- Paste buffers and zone buffers are deliberately simple: Macrohelp stores values, then emits Tasket JSON when asked.
- Registry Hub is the command surface for composing these primitives. It is not meant to be a native automation engine.
- View [live workflow editor](https://app.shel.sh/macro) runtime environment when 'script too hard, need UI' <-- a 300kb ComfyUI app mimicing workflow module style for emitting/importing machine code. (Plain rendering to text files, all local js) - [(Source Code)](https://github.com/CommanderTurtle/orc/tree/main/app/macrohard)

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

The live key display uses [PromptFont](https://shinmera.com/docs/promptfont/) (by Shinmera, SIL OFL) for beautiful keyboard icons:

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

## License

- CursorOverlay is open source [AGPLv3](./LICENSE). No data ever collected, solely passion project. 
- PromptFont by Shinmera (Yukari Hafner) is used under the SIL Open Font License 1.1.
