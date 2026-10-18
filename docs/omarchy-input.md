# Omarchy input integration

Source review: 2026-10-18. This describes the Wayland overlay's integration
boundaries, not a replacement desktop input stack. Windows behavior is unchanged.

## Ownership

Macrohelp's overlay is an Omarchy shell plugin and HTTP client. The separately
started HTTP daemon communicates with the chosen macro backend. Omarsket plays
macros through Wayland-automation; the overlay does not become another macro
engine, OCR tool, input method, or device driver.

The [Keyboard, Mouse, Trackpad manual](https://omarchy.org/manual/keyboard-mouse-trackpad/)
puts personal input overrides in `~/.config/hypr/input.lua`, accessible through
Setup > Input. Macrohelp must leave keyboard layout, Compose, repeat, pointer
sensitivity, handedness, scrolling, gestures, and touchpad haptics there. None is
an installer default or prerequisite to using the overlay.

Omarchy's [configuration entry point](https://github.com/omacom/omarchy/blob/quattro/config/hypr/hyprland.lua)
loads packaged defaults before personal overrides. Read the installed configuration
and active compositor state; do not copy a manual example over user settings.
Ordinary user-owned configuration does not need a root GUI. A privileged file
must use the system's administrator authorization path, not a user-writable
script run as root. Passwords do not belong in plugin settings or HTTP requests.

## Source coverage

The manual's technical body links were followed, including its two Hotkeys
anchors. The Fcitx homepage did not load; its official Fcitx 5 and Wayland wiki
pages were read instead. Site navigation, sponsors, and unrelated application
links are not input-integration sources.

| Source read | Consequence for Macrohelp |
| --- | --- |
| [Hyprland configuration options](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/config-options.md), especially Input, Virtualkeyboard, Binds, Cursor, and per-device options | Preserve the user's XKB, repeat, acceleration, scroll, focus, and virtual-keyboard settings. Do not change refresh, VFR, rendering, or cursor behavior to make an overlay work. |
| [Hotkeys](https://omarchy.org/manual/hotkeys/), including Quick Emojis and Quick Completions | Discover active capture/copy/terminal bindings rather than hardcoding the documented default chords. Leave Compose sequences and `.XCompose` to Omarchy. |
| [Gestures](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/gestures.md) | Native gesture actions, fingers, thresholds, and conflicts are not ordinary mouse-button macros. Do not install gesture recognizers or substitute synthetic swipes. |
| [Fcitx 5](https://fcitx-im.org/wiki/Fcitx_5), [setup](https://fcitx-im.org/wiki/Setup_Fcitx_5), and [Fcitx on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland) | Use the existing session/input-method environment and focused text controls. Physical key display is not composed-text capture. Do not set global IME environment variables or start another Fcitx process. |
| [Keyboard layouts](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/keyboard-layouts.md), [device settings](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/devices.md), and [per-device bindings](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/devices/per-device.md) | Distinguish physical codes, layout-dependent symbols, and device scope. A picture of an ANSI keyboard is not proof of a device's actual keymap. |
| [Bind API](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/_index.md), [flags](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/flags.md), [submaps](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/submaps.md), [mouse](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/devices/mouse.md), and [touchpad binds](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/binds/devices/touchpad.md) | Keep flags, device restrictions, submaps, key spelling, and press/release semantics. Do not flatten native bindings into a universal chord list. |
| [Lua events](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/advanced-configuration/events.md) and [hyprctl](https://github.com/hyprwm/hyprland-wiki/blob/main/content/configuring/core/advanced-configuration/using-hyprctl.md) | Keep compositor callbacks bounded and nonblocking; query data only when needed. `repl` returns a Lua result; `eval` only reports execution status. |

The linked input-option references were also read:
[acceleration](https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html),
[scrolling](https://wayland.freedesktop.org/libinput/doc/latest/scrolling.html),
[clickpads](https://wayland.freedesktop.org/libinput/doc/latest/clickpad-softbuttons.html),
[tapping/drag lock](https://wayland.freedesktop.org/libinput/doc/latest/tapping.html),
[middle-button emulation](https://wayland.freedesktop.org/libinput/doc/latest/middle-button-emulation.html),
and [three-finger drag](https://wayland.freedesktop.org/libinput/doc/latest/drag-3fg.html).
These belong to the compositor/libinput configuration, not Macrohelp. In particular,
touchpad scrolling is distinct from discrete wheel clicks, software button regions
depend on hardware, and an enabled drag lock can keep a logical button down after
a finger lifts. The overlay must not reinterpret those physical gestures or
silently disable them.

## Key identity, layouts, and Compose

The native `input.keyboard.key` callback supplies an XKB code and states
`0` (release), `1` (press), or `2` (repeat).
[`input.lua`](../runtime/source/wld/input.lua) subtracts the XKB offset of eight
once and ignores repeat notifications; a repeat must not become a false release
or another heatmap press. Its callback only updates bounded in-memory state.

[`KeyDisplay.js`](../runtime/source/wld/qml/model/KeyDisplay.js) maps physical
identities to the bundled PromptFont, including modifiers and distinct numpad
keys. Unknown codes retain a numeric identity rather than disappearing. This is
a physical-key HUD: it does not translate an IME's committed text or claim that
the printed US legend matches every selected layout. Firmware-only Fn actions
cannot be observed unless the device actually emits an event.

Hyprland normally resolves symbol bindings against the first configured layout;
`resolve_binds_by_sym` can change that, including per device. Physical `code:N`
bindings and symbol bindings are not interchangeable. Exact native names also
matter when unbinding. A profile editor must preserve that distinction instead
of identifying all conflicts by lowercased display text alone.

There is a documentation/source discrepancy worth preserving: the input manual's
example describes `compose:caps`, while the reviewed
[Omarchy input defaults](https://github.com/omacom/omarchy/blob/quattro/default/hypr/input.lua)
use `omarchy:shift_caps_compose`. Neither should be hardcoded by Macrohelp.
Quick completions belong to the user's `.XCompose` and the existing
`omarchy-restart-xcompose` flow. The
[session environment](https://github.com/omacom/omarchy/blob/quattro/default/hypr/envs.lua)
already establishes `XCOMPOSEFILE` and toolkit/Wayland settings.

## Capture, copy, and launch actions

[`Workflow.js`](../runtime/source/wld/qml/model/Workflow.js) discovers shortcuts
from active `binds` metadata or explicit plugin shortcut settings. It emits key
actions through the existing HTTP/backend queue, not the command behind a bind.
Changing the desktop OCR implementation does not require Macrohelp to acquire
an OCR engine. Screenshot, OCR, ordinary copy, and terminal-aware universal copy
remain distinct actions. A normal OCR shortcut is not automatically a separate
single-line OCR implementation.

Current discovery supports global, symbol-based bindings with the standard
Shift/Ctrl/Alt/Super modifiers. It does not guess from keycode-only, mouse-only,
or submapped bindings. Explicit shortcut settings are needed where discovery
cannot represent the native action. Native Lua callback IDs are not executable
command text and must not be reconstructed as such.

[`o.bind`/`o.rebind`](https://github.com/omacom/omarchy/blob/quattro/default/hypr/helpers.lua)
are the native route for user binding overrides. New profile bindings must remain
user-owned additions loaded after packaged defaults, not edits to those defaults.
The optional [Macrohelp bindings](../runtime/source/wld/bindings.lua) are examples,
not an automatic overwrite of the user's configuration. No new capture packages
or input-method packages are needed for these actions. Rgd remains independent.

## Lifecycle and shell behavior

[`Hud.qml`](../runtime/source/wld/qml/Hud.qml) uses Omarchy's theme/font/spacing
tokens and one click-through, non-focusable overlay per screen. Pointer and saved
zone coordinates use compositor logical coordinates, including negative monitor
origins. Do not convert them using a guessed DPI or force a display refresh rate.
The editor can take focus; the HUD cannot.

[`overlay.rs`](../runtime/source/wld/src/overlay.rs) gates key display on the UI
preference, checks session lock, and stops the hook on clean exit. The compositor-
side lease also expires after a consumer disappears. HUD events stay transient;
saved macro settings are separate from key history. Heatmap collection must be
independently switchable, persisting aggregate session data rather than an ordered
keystroke log.

Compositor control requests are synchronous, including direct socket requests:
avoiding the `hyprctl` subprocess does not make them free. The current HUD batches
events between samples rather than making an IPC call per key, stops display
sampling when disabled, and backs off if the native interface is unavailable.
Aggregate-only collection should not require display-rate sampling. Slow HTTP
work must not block key/pointer updates or execute in a compositor callback.

Virtual keyboard teardown is another separate concern. Hyprland exposes
`input.virtualkeyboard.release_pressed_on_close`; automatic release on an abrupt
client death must not be assumed. Normal macro sequences must balance press and
release. Stop-during-held-key behavior needs a real backend/compositor check,
not a global setting changed by the overlay to hide a backend problem.

## Verification boundaries

This review is a source and code audit, not a live Omarchy/device validation.
Model tests cover PromptFont mapping, chords, bounded history, discovery, capture
action construction, and geometry. Native lock/unlock, IME composition, layout
switching, device-specific binds, and cancellation while keys are held still need
an actual Omarchy session. No package-version branches, global input-setting
changes, hardware writes, or new dependencies are justified by this review.
