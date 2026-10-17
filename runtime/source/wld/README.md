# Wayland overlay client

Build: `bash runtime/source/wld/build.sh` from the repository root, then enable
the Macrohelp overlay through Omarchy. This crate contains no HTTP server or macro
engine, and does not build or launch either. Unavailable HTTP requests are silent;
the HUD and local tools still load. Theme/rendering come from the existing shell.

Configuration and server startup: [Quick start](../../../quick-start.md).

```sh
cargo test --locked --manifest-path runtime/source/wld/Cargo.toml
bun test runtime/source/wld/tests/overlay.test.js
```
