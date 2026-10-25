# Synthesized-key identity audit

Reviewed 2026-10-25 against the supplied 16:23–16:34 session export, including
its expanded thinking blocks, Macrohelp `b5c0c1e`, Omarsket `77b9988`,
Wayland-automation `485fe1f`, wtype v0.4 and Hyprland v0.56.2 source.

## What explains the Escape label

The report repeatedly observes different requested keys appearing as Escape in
the HUD. That observation is consistent with a concrete identity mismatch:

- [wtype v0.4][wtype] numbers requested symbols in a private map starting at
  code 1. Its keymap gives that code the requested symbol; it does not send the
  keysym value as the protocol's key argument. A one-key `q`, Print or F13
  invocation therefore uses code 1, despite representing different symbols.
- [Hyprland's virtual-keyboard handler][protocol] forwards that code. Its
  [Lua event][events] adds the XKB offset of 8. Macrohelp subtracts that offset
  and passes the resulting physical identity to `KeyDisplay.js`.
- Physical evdev code 1 means Escape. The HUD consequently prints Escape for
  these private-map events. The PromptFont value `0x242f` is a glyph, **not**
  Escape's XKB keysym. Changing the offset or glyph table would break real keys.

This proves why the HUD can misidentify the input. It does not prove that every
focused application receives Escape. Application translation uses the virtual
keyboard's supplied map. [Compositor binding matching][bindings] can instead
use its normal translation state unless the device resolves bindings by symbol,
so screenshot-shortcut behavior must be checked separately on the actual device.

The current Lua callback contains code, timestamp and state, but no originating
device or resolved symbol. The HUD cannot reconstruct an arbitrary private map
from those three values. Do not replace the label with the requested key from a
scheduled task: that would report intent, not observed input.

## Invalid conclusions in the exported investigation

- Its temporary subscriptions use `input:keyboard:key`; the supported event is
  `input.keyboard.key`, which the installed observer already uses. An unsupported
  name returns no subscription. Those probes do not establish a missing event,
  broken subscription API, or loss of persistent Lua state.
- The ordinary HUD drains the shared sample queue. An empty manual sample after
  a keypress does not establish input loss.
- `unknown request` following an empty concatenation is not evidence that pipe
  or semicolon characters are prohibited in Lua expressions. The report did not
  isolate that claim with a nonempty controlled response.
- The unusual help/version errors and press/release flags are present in stock
  wtype v0.4; they do not identify a custom or broken Omarchy build.

## Separate Omarsket heatmap connection

Omarsket's heatmap bridge is optional and independent of task input execution.
Its initial health-check failure was cached for the shell lifetime; reopening
the panel could not retry it. The companion Omarsket change adds deliberate
reconnect/retry, retains native aggregate IPC once connected, and discards stale
queued heatmap mutations after transport failure. This is not a keyboard fix.

## Remaining integration check

Before changing key emission, capture a harmless named key in a dedicated test
client, inspect its virtual-device map and active binding-resolution setting,
and compare application receipt with the HUD and desktop binding result.
Preserve the existing config-owned observer lifecycle described in
[input-feed-safety.md](input-feed-safety.md): no runtime binding registration,
sampling bootstrap, compositor reload or global setting change for this probe.

No live Linux key-receipt check was completed in this audit: the documented SSH
channel was unavailable. No keyboard emitter, input observer, or compositor
configuration was changed on the strength of the report's unsupported theory.

[wtype]: https://github.com/atx/wtype/blob/v0.4/main.c
[protocol]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/protocols/VirtualKeyboard.cpp
[events]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/lua/LuaEventHandler.cpp
[bindings]: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/managers/KeybindManager.cpp
