# Macrohelp Package Manifest Info

This package intentionally has two runtime folders:

- `macrohelp-runtime` - Macrohelp / CursorOverlay source
- `tasket-http-daemon` - Tasket++ HTTP daemon source

The expected flow is:

1. Start Tasket++ normally.
2. Start `tasket-http-daemon`.
3. Start `macrohelp-runtime`.
4. Use Macrohelp hotkeys. Macrohelp writes temporary Tasket schedules through the daemon; Tasket performs the real mouse, keyboard, paste, snip, zone, and schedule work.

## Quick Start

Open two PowerShell tabs.

Tab 1:

```powershell
cd tasket-http-daemon/bin
.\tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir "$env:APPDATA\Tasket++\saved_tasks" --default-delay 1
```

Tab 2:

```powershell
cd macrohelp-runtime/bin
.\CursorOverlay.exe
```

## Stop Commands

```powershell
Invoke-RestMethod -Method Post -Uri "http://127.0.0.1:7777/stop" -Body "{}" -ContentType "application/json"
Get-Process CursorOverlay -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process tasket-httpd -ErrorAction SilentlyContinue | Stop-Process -Force
```

## Adjust DPI manually (if needed, 4k defaults to 2+)

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

Macrohelp also exposes `Shift+Alt+7` as the emergency stop hotkey for active Tasket tasks through the daemon.
