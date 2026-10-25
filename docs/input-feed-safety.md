# Input-feed lifecycle audit

Reviewed 2026-10-25 against Macrohelp `51e63b8`, Omarsket `9b3dc1f`, the exported
Hyprland investigation/session, its four crash reports, journal windows and core
disassembly, and upstream Hyprland v0.56.2 source. No private machine details or
session credentials are needed to reproduce the lifecycle findings below.

## Confirmed issues addressed

1. The old client bootstrapped the entire Lua observer over `repl` when its session
   global was missing. Display-enabled sampling could then register six `hl.bind`
   mouse observers on the compositor thread. Display toggles and lease expiry
   removed them and later sampling recreated them. A protected Lua call does not
   isolate the compositor from a native crash.
2. Cleanup was not handle-local. In v0.56.2, [`Keybind:remove()`][keybind] forwards
   modifier/key identity to [`CKeybindManager::removeKeybind`][manager], which
   erases **all matching chords**, including another matching user binding and
   both observer edges. Non-consuming/transparent creation flags do not change
   the removal scope.
3. The old tests covered PromptFont rendering and source-string invariants, but
   did not execute the Lua lifecycle. They could not establish that callbacks
   never registered or removed bindings during sampling or cleanup.

The observer is now config-owned. Load it with the exact guarded include printed
by the build, once from personal Hyprland configuration; see [migration/setup].
The native config loader owns observer creation and destroys its Lua state on
reload. A duplicate include within that state is a no-op. Sampling, stopping,
lock handling and consumer-lease expiry never register or remove observers.
Partial setup failure disables only the owned mouse handles. No raw input-device
access, privileged input group, compositor binary plugin, or new execution
backend was introduced.

Mouse events retain their compositor interpretation, press/release ordering,
modifier chords and between-poll fast taps. Heatmap-only collection counts
keyboard presses without an ordered event stream or mouse counts. When inactive,
callbacks retain no input data. An old or missing feed produces an explicit setup
diagnostic rather than reinjecting code. The client stays pinned to its inherited
compositor instance instead of guessing another login/nested instance.

## What the crash evidence does not establish

- The four exported reports share an IPC/Lua-evaluation crash path. The records
  supplied do not capture the failing request payload or identify the caller by
  socket peer credentials. Macrohelp was a credible suspect, not a proven source
  of every recorded crash. No deliberately crash-inducing live repro was run.
- The raw GDB instruction is `mov 0x68,%rax`. Its appended analysis changes that
  to `mov 0x68(%rip),%rax`; that is a different instruction/addressing mode. The
  mapped-read-only-data/race theory derived from the latter is unsupported by the
  former. A binary-relative backtrace offset is also not a function-relative
  offset. Exact native attribution needs the matching executable/debug info and
  original signal context; the crash handler's later SIGABRT is not that context.
- Bootstrap alone did not create mouse bindings: the old `start()` was already
  display-gated. Heatmap-only sampling did not register them. Restart changed the
  compositor instance signature, so a client with its old environment could fail
  before reaching any feed bootstrap. A guaranteed restart/re-crash loop was not
  demonstrated. The lock query also already gated collection; it was not absent.
- Moving creation to config load avoids this runtime mutation surface, but does
  not fix an arbitrary upstream native `hl.bind` fault. These are focused client
  lifecycle fixes, not a promise that Hyprland cannot crash.

The report's safe-mode/start-event recovery problem is distinct from input
observation. This patch does not re-run all Omarchy autostart actions, unlock a
session, adjust GPU watchdogs, change drivers, or restart a user's compositor.
The observer include is ordinary config-load code, not `hyprland.start`/exec-once,
so it does not depend on a start event that may already have fired in safe mode.

## Alternatives evaluated

- v0.56.2's [Lua event list][events] has a keyboard hook but no pointer-button
  hook. A future supported pointer event would be preferable to mouse observers.
- A passive evdev reader avoids compositor registration but is not a drop-in
  replacement: [device access][permissions] needs explicit permissions, and
  [libinput tapping][tapping]/button interpretation is above raw kernel events.
  Broad input-device access was not added to a display-only feature.
- A compiled Hyprland plugin has an in-compositor lifecycle, not process/crash
  isolation. It would add an unnecessary binary/ABI dependency for this patch.

## Verification

`tests/input-feed.test.lua` executes the actual observer against a native-API
fixture with v0.56.2-style chord-wide removal. It checks config ownership,
duplicate loads, all mouse edges and modifiers, repeat filtering, heatmap-only
privacy, stop/lock/toggle behavior, lease expiry, bounded overflow, partial setup
failure, old-feed migration and fresh config/compositor lifetimes. It fails
against the old observer. Run from the repository root:

```sh
lua runtime/source/wld/tests/input-feed.test.lua
cargo test --locked --manifest-path runtime/source/wld/Cargo.toml
bun test runtime/source/wld/tests
```

Rust tests exercise the actual sampler with fake IPC: missing feed never boots
code; locked or unknown lock state stops both consumers; stale-instance errors
do not trigger bootstrap; shutdown does not require a successful lock query.
These and the QML checkers are regression coverage, not live-compositor safety
certification. A live integration check should verify the config include, rapid
clicks, user mouse bindings, display toggles, lock/unlock and config reload on a
session where restarting the compositor is acceptable.

Omarsket's remote head has no observational `hl.bind` bootstrap. Its native
editor's REPL request compiles configuration with `load` without invoking the
compiled function; user-authorized edits are applied through config reload. Its
backend caches display scale instead of refreshing monitors before each pointer
action. No Omarsket production change was warranted by this investigation.

[keybind]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/lua/objects/LuaKeybind.cpp
[manager]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/managers/KeybindManager.cpp
[events]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/lua/LuaEventHandler.cpp
[permissions]: https://wayland.freedesktop.org/libinput/doc/latest/tools.html
[tapping]: https://wayland.freedesktop.org/libinput/doc/latest/tapping.html
[migration/setup]: ../quick-start.md#native-input-hud-and-heatmap
