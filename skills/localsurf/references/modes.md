# Modes, selection and editors

Source baseline: Surfingkeys 1.19.3,
[662b8c6](https://github.com/brookhong/Surfingkeys/tree/662b8c6faff942d7dafad2658a2682ab65d866a0).
These are defaults, not an introspection of the user's active configuration.
`localsurf_reference` topic `bindings` shows only Localsurf recipe overrides.

## Normal, focus and live help

Normal handles browsing shortcuts. Inputs, contenteditable, hints, the frontend
and browser chrome have their own focus/modes. A native address bar is not the
Surfingkeys omnibar. `?` displays the current frame's generated help; scroll and
`/` search work within help. Pressing `?` again there closes it. Current source
maps `u` to half-page-up, despite the README's outdated quick-help claim.

`w` selects/switches frames; `;w` focuses the top content frame. `cs` changes the
scroll target, `cS` resets it, `;fs` hints scrollable elements. A non-scrolling
page may simply have the wrong frame/scroll target or focus in browser chrome.
Restore focus using native controls or a Macrohelp click when necessary.

Counts are part of the sequence (`3j`, `12T`), not repeated HTTP requests.
The native `repeatThreshold` opens a confirmation above its threshold; it is not
a cap. Inspect its displayed choices rather than bypassing confirmation. Prefix
rich hints appear after the user's configured delay. Escape can cancel a pending
prefix/count before it exits any mode.

## Hints and regional hints

`f` opens click hints; `af` opens in an active tab; `gf` in a background tab;
`cf` keeps selecting links. `q` limits targets to images/buttons. `O` detects
URLs in text. Use a fresh screenshot for labels. A sole target may activate
immediately unless `hintExplicit` is enabled; do not type a guessed label then.

Shift flips overlapping labels; holding Space hides hints until release.
Backspace removes a label prefix. Numeric hints also accept a text filter.
Scrolling and multi-hit actions rebuild labels. A non-hint key may cancel hints.
Hints account for visible open shadow roots and native page dialogs, but cannot
reach every browser-protected surface. Use native keys/Macrohelp for those.

`L` selects a large element, then displays its own contextual menu: `ct` copies
text, `ch` HTML, `p` selects parent, `d` removes the element from the page, `l`
opens the optional LLM pane for that element. Read the actual menu before acting.

## Insert and multiline text

`i` hints editables; `gi` focuses the first input with Tab navigation among masks;
`I` selects a field for the Vim editor. Clicking an input also enters Insert.
`<Ctrl-i>` opens its editor from page Insert. Neovim is separately optional at
`<Ctrl-Alt-i>` and requires the user's existing native host; do not install one.

Use `localsurf_type` for text. Paste preserves arbitrary braces/newlines without
letting them become commands. It does not force a textarea to accept Enter or
submit a form: the target application still decides. In ACE Vim enter Insert
(`i`, `a`, `o`, etc.) before pasting. In ordinary single-line inputs newlines are
not meaningful; choose a textarea/contenteditable/editor for multiline material.

Insert defaults: Ctrl+e end, Ctrl+a beginning on Linux/Mac (Ctrl+f on Windows),
Ctrl+u delete before cursor, Alt+b/f word movement, Alt+w/d word deletion,
Ctrl+' quotes. These can be remapped. For a profile with Ctrl+q beginning,
Ctrl+e end and Ctrl+a select-all, preserve that exactly: set Localsurf's
`line_start` override to `<Ctrl-q>`, never overwrite Surfingkeys' settings.
Localsurf's unconfigured `line_start` uses native Home, not Ctrl+a.

Escape blurs a page editable and leaves Insert. IME/completion popups introduce
their own state. Emoji completion, when enabled, uses `:` and the user's threshold;
Tab/Shift+Tab choose candidates, Enter accepts, Escape closes the candidate pane.
Use actual paste for Unicode rather than treating Unicode as physical key names.

## Visual Caret, Range and find

From Normal, `v` first presents text hints. Choose a label to enter Caret.
`v` from Caret enters Range; `v` from Range exits. `V` in Normal restores the
cursor; `zv` selects the whole hinted text element.

| Keys in Visual | Effect |
| --- | --- |
| h/l, j/k | character/line movement; Range extends selection |
| w/e, b | word forward/backward |
| 0/$, gg/G | line or document boundaries |
| (/), {/} | sentence/paragraph boundaries; browser support varies |
| o | exchange selection ends |
| Vw/Vl/Vs/Vp | select a word/line/sentence/paragraph |
| p | expand selection to parent element |
| yw/yl/ys/yp in Caret | copy a chosen text unit |
| y in Range | copy the selection |
| f/F then character | seek forward/backward |
| ; / , | repeat seek / opposite seek |
| zt/zz/zb | position cursor top/center/bottom |
| Enter / Shift+Enter | activate the node under the cursor |
| * | find the current word |

Escape during a pending seek cancels that seek. From Range it collapses to Caret;
from Caret it exits. Do not assume one Escape is Normal. `modeAfterYank` can leave
the result in Range, Caret or Normal, so observe after copying.

`/` opens Find; input is a native search pattern. Enter commits to Caret when
matched; Ctrl+Enter adds whole-word boundaries. `n/N` walks results. Escape in
Find cancels search and clears highlights. If a literal query contains regex
characters, escape them before passing `find` text. Search in help addresses the
help document, not the underlying page.

## ACE Vim and Emacs

The editor is a separate frontend mode. It opens in Vim Normal by default, not
Insert. Enter `i` before pasting text. Use `ggVG` then paste to replace its buffer;
Escape afterward returns to Vim Normal. This Escape does not yet close it.

- `:w` writes back without closing; `:wq`/`:x` writes and closes; `:q` discards.
- In single-line input/URL Insert, Enter saves/closes. In multiline Insert,
  Ctrl+Enter saves/closes; ordinary Enter inserts a line.
- A select editor is read-only: choose the option's line and Enter.
- Tab opens completion; Tab/Shift+Tab navigate, Space accepts. Escape closes
  completion before leaving editor Insert/Normal.
- Escape in clean Vim Normal closes; in dirty Normal it prompts to discard.
  `:q` is the explicit discard path. Do not accidentally confirm that prompt.
- `:bn`/`:bp` switches the edited field; `focusOnSaved` can restore page Insert.
- Emacs mode uses Ctrl+x Ctrl+s for save/close. Do not send Vim commands into it.

## Passthrough, Lurk and Disabled

`Alt+i` from Normal enters indefinite PassThrough; Escape leaves it. This is the
appropriate bracket around page-native shortcuts or Macrohelp keyboard flows
that Surfingkeys would otherwise intercept. `pass_keys` is the compact version
only when the whole sequence returns to browser content without observation.
With another focused window/dialog, split into `pass_enter`, the native flow,
and `pass_exit` after restoring browser focus.

`p` is ephemeral passthrough with a one-second inactivity timeout. It is not a
stable bracket for long flows. In Lurk, Alt+i/p instead activate Normal and
Escape/timeout returns to Lurk. Alt+s changes the persistent site blocklist;
it is not a substitute for temporary passthrough. Do not toggle it as a reset.

Sources: [mode stack](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/common/mode.js),
[hints](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/common/hints.js),
[visual](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/common/visual.js),
[frontend/editor](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/ui/frontend.js).
