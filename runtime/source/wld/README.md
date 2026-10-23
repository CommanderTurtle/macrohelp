# Wayland overlay client

Build: `bash runtime/source/wld/build.sh` from the repository root, then enable
the Macrohelp overlay through Omarchy. The overlay owns a localhost control API;
the execution daemon remains separate and is never launched by it. Unavailable backend requests are silent;
the HUD and local tools still load. Theme/rendering come from the existing shell.

Configuration and server startup: [Quick start](../../../quick-start.md).
Input integration and source review: [Omarchy input](../../../docs/omarchy-input.md).
HTTP/MCP control: [Overlay control](../../../docs/overlay-control.md).

Left-click the bar widget to open its panel; right-click stops macros. The workflow
editor is inside the popup, not a permanent bottom-screen textbox. Omarchy owns bar
placement, popup geometry, theme, fonts and scaling. UI QML reloads on save; the
kept service needs `omarchy-restart-shell`. Rust client changes need a rebuild and
shell restart. This is not a separate Quickshell window or a system-tray process.

```sh
cargo test --locked --manifest-path runtime/source/wld/Cargo.toml
bun test runtime/source/wld/tests
```
