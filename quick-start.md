# Macrohelp Package Manifest Info

This package intentionally has two runtime folders:

- `macrohelp-runtime` - Macrohelp / CursorOverlay source
- `tasket-http-daemon` - Tasket++ HTTP daemon source

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
cd tasket-http-daemon
.\build.ps1 -DryRun
# Then..
.\build.ps1
```

Build overlay:

```
cd macrohelp-runtime/source
cmake -S . -B build-vs2026 -G "Visual Studio 18 2026" -A x64
cmake --build build-vs2026 --config Release
```

</details>

<details>
  <summary>Show quick start / how-to-close</summary>
  
## Quick Start

The expected flow is:

1. Start Tasket++ normally.
2. Start `tasket-http-daemon`.
3. Start `macrohelp-runtime`.
4. Use Macrohelp hotkeys. Macrohelp writes temporary Tasket schedules through the daemon; Tasket performs the real mouse, keyboard, paste, snip, zone, and schedule work.

## Open two PowerShell tabs:

Tab 1 (httpd):

```powershell
cd tasket-http-daemon/bin
.\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "$env:APPDATA\Tasket++\saved_tasks" --default-delay 1
```

Tab 2 (overlay):

```powershell
cd macrohelp-runtime/bin
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
