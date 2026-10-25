# Macrohelp Package Manifest Info

See the [HTTP daemon and overlay parity tables](docs/feature-parity.md) for exact
Windows/Wayland counterparts and outstanding menu differences.
Overlay-owned HTTP/MCP controls: [Control API](docs/overlay-control.md).

### Desktop shortcut refresh (Wayland)

Keys and Zones show the active screenshot, OCR, single-line OCR, copy and terminal
chords, including explicit plugin overrides and the existing clipboard fallback.
They are derived by the same resolver used by workflow compilation. Missing
contracts say **Not configured**; a failed compositor read is shown as an error,
not an empty binding list.

The display refreshes at controller startup, after config reload/layout events,
on plugin-setting changes, or with **Refresh hotkeys**. Refresh is read-only: no
key injection, binding registration, compositor reload, or draft/environment
changes. Event bursts are debounced and overlapping reads are coalesced. The
existing execution path still reads fresh bindings when preparing a workflow.
Switching tray pages resets their scroll position.

This uses [Quickshell's existing event stream](https://quickshell.org/docs/v0.1.0/types/Quickshell.Hyprland/Hyprland/)
and [Hyprland's config/layout notifications](https://wiki.hypr.land/IPC/), not a
new in-compositor input observer. Config-owned HUD hooks are unchanged.

This package intentionally has two runtime folders:

- `runtime` - Macrohelp / CursorOverlay source
- `http-daemon` - Tasket++ HTTP daemon source

<details>
  <summary>Show quick build</summary>

## Quick Build (in powershell)

Initial clone:

```
cd ~/Documents
git clone CommanderTurtle/macrohelp macrohelp
cd macrohelp
```

Build httpd:

```powershell
cd http-daemon
.\build.ps1 -DryRun
# Then..
.\build.ps1
```

Build overlay:

```
cd runtime/source
cmake -S . -B build-vs2026 -G "Visual Studio 18 2026" -A x64
cmake --build build-vs2026 --config Release
```

</details>

<details>
  <summary>Show quick start / how-to-close</summary>
  
## Quick Start

The expected flow is:

1. Start Tasket++ normally.
2. Start `http-daemon`.
3. Start `runtime`.
4. Use Macrohelp hotkeys. Macrohelp writes temporary Tasket schedules through the daemon; Tasket performs the real mouse, keyboard, paste, snip, zone, and schedule work.

## Open two PowerShell tabs:

Tab 1 (httpd):

```powershell
cd http-daemon/bin
.\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "$env:APPDATA\Tasket++\saved_tasks" --default-delay 1
```

Tab 2 (overlay):

```powershell
cd runtime/bin
.\CursorOverlay.exe
```

## Stop Either:

```powershell
Get-Process CursorOverlay -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process tasket-httpd -ErrorAction SilentlyContinue | Stop-Process -Force
```

Ctrl+C should also stop the httpd if it's running in an interactive terminal window.

---

## Adjust Scale manually 

(if needed, 4k / 1080p already work out-of-box with defaults)

```powershell
$env:MACROHELP_UI_SCALE = "2.25"
Start-Process "C:\..path\to\CursorOverlay.exe\
```

For persistance:

```powershell
[Environment]::SetEnvironmentVariable("MACROHELP_UI_SCALE", "2.25", "User")
# Then restart. To fully remove it later:
[Environment]::SetEnvironmentVariable("MACROHELP_UI_SCALE", $null, "User")
```

</details>

---

Macrohelp also exposes `Shift+Alt+7` as the emergency stop hotkey for active Tasket tasks through the daemon.
- (7/2/26) patched to Tasket 1.9+ behavior

---

<details>
<summary>Wayland / Omarchy</summary>

The HTTP server and overlay are separate builds. The server requires an initialized
[Omarsket](https://github.com/CommanderTurtle/omarsket) backend with local socket
support; the optional overlay runs inside Omarchy Quattro's existing shell.

```sh
sudo pacman -S --needed rust gcc zenity
git clone https://github.com/CommanderTurtle/macrohelp.git
cd macrohelp
bash http-daemon/wld/build.sh --omarsket /absolute/path/to/omarsket
```

This builds only `macrohelp-httpd`, creates a private `.env` if absent, and prints
the manual launch command. `--env PATH` selects another configuration. The example
is `.env-example`; defaults are loopback `127.0.0.1:7777` with a generated API key.
It never clones, initializes or starts Omarsket, and never starts an HTTP process.
With the existing Omarsket backend running, start the server yourself:

```sh
"${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/http-daemon/bin/macrohelp-httpd"
```

The saved `.env` path is reused automatically. Keep it private (`chmod 600`).
Requests use `X-Tasket-Key`. The Qt contract remains: `/health`, `/tasks`, GET/POST
`/run`, `/temp-task`, `/check` (`/task`), `/status`, POST `/stop`, `/entrypoint`,
`/entrypoints`, and GET/POST `/grid`. Authenticated `/` lists parameters/examples.
Task IDs, delays, loops and cancellation come from Omarsket. `cleanup:true` temp
tasks stay in memory; saved tasks use `.omscht` and never overwrite existing names.
Ctrl+C stops HTTP-owned runs; it does not stop independent backend jobs.

Optional overlay plugin:

```sh
omarchy plugin add https://github.com/CommanderTurtle/macrohelp.git
# Leave disabled until its client is built:
cd ~/.config/omarchy/plugins/io.github.commanderturtle.macrohelp
bash runtime/source/wld/build.sh
omarchy plugin enable io.github.commanderturtle.macrohelp
```

The overlay build includes its own control API. It never builds, starts or supervises
the HTTP server or Omarsket. HUD/local tools remain available without a server;
unanswered HTTP requests are silent. It uses the shell's live theme and rendering,
not a separate Quickshell instance. The `envFile` plugin setting can override the
saved `.env` path when necessary. The key remains in the native client, not QML.

The bar panel provides workflows, 16 points, pointer paths, circle/vector math,
timed keys and clicks, buffers, zones, task status and view controls. The HUD is
click-through and follows Hyprland's monitor geometry. To opt into shortcuts,
copy/adapt `runtime/source/wld/bindings.lua` in `~/.config/hypr/bindings.lua`
(or `dofile` it from the installed plugin path). These use quiet shell IPC:
Shift+Alt+0 saves a point; 1 buffers; 2 workflow; 3 pointer tools; 4/5/6 clicks;
7 stop through HTTP; 8 view; 9 focused key recorder.

Zone slots and methods are independent: `{zone Z,X}` uses zone Z with OCR;
`{zonebuf Z,X,V}` pastes that result into buffer V. Methods Z/X/C/V use the desktop's
screenshot/OCR/custom-OCR/copy actions. Macrohelp sends shortcut and drag macros
through HTTP; it does not capture images or run an OCR engine. Your existing OCR
binding, including PP-OCR, stays in charge. Clipboard buffers use the native editor.
Registry helpers remain unchanged and require `rgd` on PATH (`wl-paste` for append,
already supplied by Omarchy). Import/export transfers environments, including Windows
Registry Hub text exports; adjust Windows paths and shell aliases before use.
Workflow conditions evaluate on Play/Save. Clipboard/OCR/registry workflow steps
use Play; Export environment preserves those workflows. Input-only workflows may
also be saved as native schedules. Shell aliases open the desktop terminal shortcut
before typing; they do not open Windows shells or enable arbitrary system execution.
Key recording is focused-only; the separate PromptFont HUD shows live native
keypresses with transient history, not saved keystroke logs. No raw-device access,
telemetry or remote listener is added by the overlay.

Desktop shortcuts are read from active Hyprland bindings, including remaps in
`~/.config/hypr/bindings.lua`; Macrohelp never edits those bindings. Optional
`shortcuts` settings on the plugin entry in `~/.config/omarchy/shell.json` override
`screenshot`, `ocr`, `singleLine`, `copy`, `terminal` (key chords), and `captureWaitMs`.
C reuses normal OCR unless your tool has a separate single-line binding; Macrohelp
does not reformat the result. Unrecognized/keycode-only bindings need an explicit
shortcut override, rather than silently invoking a different desktop action.

Workflow's terminal shortcut field overrides discovery for that environment
(normally Super+Enter). `{terminal}` opens it without entering another shell.
`{bash}` uses X; `{cmd}` uses C; `{python}` / `{regedited}` use G;
`{pwsh}` / `{powershell}` use P (default `pwsh`). Each command is editable:
terminal chord → wait → type the command → Enter. A blank command retains the
terminal's default shell. Install any desired shell yourself; Macrohelp does not.

The two binaries live under `$XDG_DATA_HOME/macrohelp/http-daemon/bin` and
`$XDG_DATA_HOME/macrohelp/overlay/bin`; build caches are similarly separated under
`$XDG_CACHE_HOME/macrohelp`. Standard home defaults apply. Overlay state is
`$XDG_DATA_HOME/macrohelp/overlay.json`. Rebuild each component after its updates;
restart the Omarchy shell after changing a kept-loaded overlay service.

### Native input HUD and heatmap

The input observer must be loaded by **Hyprland configuration**, not by the
overlay over IPC. After building, add the exact Lua line printed by
`runtime/source/wld/build.sh` once to your personal `~/.config/hypr/autostart.lua`
(or the corresponding file under `$XDG_CONFIG_HOME`). It points to this checkout's
`runtime/source/wld/input.lua`; it does not replace your personal `hypr/input.lua`.
Then run `hyprctl reload` in that session. This is ordinary config-load code, not
an `hl.on("hyprland.start", ...)` or exec-once callback: it also loads after leaving
safe mode and on subsequent config reloads.

This is a one-time migration for installations that previously bootstrapped the
feed automatically. Rebuild the client too, and restart the Omarchy shell/client
**before the final `hyprctl reload`**, so an old still-running client cannot
reinject its embedded v2 feed over the new observer. Without the include, the View tab
shows the setup requirement; Macrohelp deliberately does not inject a replacement
feed. Keyboard HUD, click HUD, dialog shortcuts and heatmap need the observer;
the other tools and execution backend remain independent.

Observers register once per config lifetime. Sampling, hiding the HUD, locking,
stopping the client, or lease expiry only clear/disable transient collection;
none registers or removes mouse bindings. All three mouse buttons retain both
edges and compositor-interpreted touchpad clicks. No evdev access, elevated input
permissions, compositor plugin, or automatic config editing is required.

The printed include skips a missing checkout. Remove it when uninstalling, then
reload config to discard the observers. If the compositor itself restarts, restart
the Omarchy shell/client in the new session as well; Macrohelp never guesses a
different instance from another login or nested compositor.

Remove using `omarchy plugin remove io.github.commanderturtle.macrohelp`, stop the
foreground HTTP process separately and remove any sourced bindings. State/binaries
and build caches remain under the paths above for optional manual removal.

</details>
