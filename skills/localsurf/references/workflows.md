# Translated workflows

Use `localsurf_workflow {"name":"…"}` rather than building Registry Hub tokens.
The MCP appends its current recipe catalog below this document. A recipe's
precondition matters: `visual_range` is not the same operation from every mode.
Optional `keys` replaces its opening mapping for a user-specific configuration.

## Observe -> select -> act

1. `hints` opens the live labels. `scrsht` opens Macrohelp's native capture pane.
   Complete the capture with the inherited Macrohelp controls; locate and view
   the saved file using the model's existing filesystem/image tools.
2. `follow_hint` with the observed `keys` sends just that label, not another `f`.
3. Read Macrohelp completion and inspect the resulting page before another action.

This same pattern serves `input_hints`, `editor_hints`, `visual`, `regional`,
`tabs` and context menus. Do not reuse a screenshot's labels after a redraw.
For mouse-only controls, use raw `macrohelp_control` with native circle/point/
zone/click commands from `macrohelp_help`. No Localsurf DOM bridge is involved.

## Navigate, find or execute a registered command

`navigate` opens URLs in the current tab, `navigate_new` in a new tab. `find`,
`search` and `command` open their appropriate input panes. Supply `text`:

```json
{"name":"navigate","text":"https://example.com/"}
```

These compound recipes wait `settle_ms` (default 300) between opening and pasting.
It is an editable UI-settle delay, not a page-load guarantee or background poller.
For a slow/new frontend or uncertain focus, use separate opening/typing calls and
observe before typing. `localsurf_type` accepts optional `before`/`after` keys;
no implicit Enter is added to ordinary typing.

The Surfingkeys command pane accepts registered commands, not arbitrary JS or
OS shell commands. It may stay open after execution. Search alias `og` is the
upstream Google default; pass the user's configured alias via `keys`/bindings.

## Edit multiline text

Select a real editable with `input_hints`, or use `editor_hints` for ACE. Observe
the selected target and mode. In Vim Normal, type `i`; then use `localsurf_type`
with the complete literal text. To replace an ACE buffer, use its own selection/
editing commands first, then paste into the intended edit. Confirm the resulting
editor mode, Escape to Normal if needed, then use
`editor_save_close`. Page single-line inputs and URL fields differ from textareas;
see `modes` before sending Enter.

To replace an ordinary input, use its actual select-all binding, then paste.
Do not send Ctrl+a as a universal line-start operation. `line_start` uses Home
unless overridden (the supplied local profile uses Ctrl+q); `line_end` uses Ctrl+e.

## Passthrough without losing track of focus

`pass_enter` -> existing Macrohelp/native keys -> restore browser focus ->
`pass_exit`. The compact `pass_keys` adds Alt+i and Escape around supplied keys.
It is suitable only when no intermediate pane needs observation. It does not
detect the current mode or promise a finally-block after a timeout. After an
uncertain dispatch, inspect before sending another Escape or replaying input.

## Private browser and workspace

First inspect/reuse the intended private window on the current workspace using
the model's available context/tools. Localsurf does not maintain a window registry
or monitor user browsing. If none exists, call `private_browser` with the existing
native launch binding. Omarchy's standard is `<Meta-Shift-Alt-b>`; it invokes the
native launcher which handles the default browser family. On Windows use a
configured private launcher or the known browser's private-window shortcut after
focusing it (Chromium Ctrl+Shift+n, Firefox Ctrl+Shift+p). Don't assume those
shortcuts identify or launch a closed default browser.

Navigate a private new-tab page using native address-bar controls if Surfingkeys
cannot run there, then focus content before sending Normal mappings. Incognito
does not prevent explicit downloads/bookmark/settings changes. The extension and
user scripts are already configured here; do not repeat setup or alter them.

## Bindings, preview and native completion

`--bindings PATH.json` accepts workflow-name -> key-notation overrides. This is
only recipe input, not another Surfingkeys configuration. For example:

```json
{"line_start":"<Ctrl-q>","line_end":"<Ctrl-e>","private_browser":"<Meta-Shift-Alt-b>"}
```

On every action, `preview:true` returns the exact request without sending it.
Otherwise the request uses menu 2, the generated script and native Play with
`no_replace:true`. Literal text lives in temporary buffer Z, outside the script,
so unbalanced braces and text resembling commands cannot escape into the parser.
Native credentials, request limits, errors, task sequencing and stop-all remain
Macrohelp's. No arbitrary tool throughput, repeat or tab cap is added here.

After execution, use the returned ID with `macrohelp_state`. Wait for matching
`last_run`/sequence completion, not just task admission. Continue a partial native
dialog with its ID. Busy conflicts don't authorize stopping unrelated work.
Errors and timeouts never cause automatic replay. Unsupported key names/layouts
are native backend errors; correct the key/recipe or use inherited native tools.

The harness emits existing `{press …}`, `{paste Z}`, `{wait …}` and `{screen}`
tokens only. Surfingkeys itself still supplies the UI, modes, help and behavior.
