# Surfingkeys feature reference

Research baseline: 1.19.3 at
[662b8c6](https://github.com/brookhong/Surfingkeys/tree/662b8c6faff942d7dafad2658a2682ab65d866a0).
Read `modes` for context-sensitive meanings. Read live `?` or the visible local
menu for custom/site mappings. The harness can send any sequence, not only the
named convenience workflows. For arbitrary data use `localsurf_type`.

## Omnibar and command area

| Open from Normal | Pane |
| --- | --- |
| t / go | URLs (new/current tab) |
| b / ab | bookmarks / add bookmark |
| oh / ox / H | history / recently closed / current tab's visited URLs |
| T / ;x | tabs / close tabs matching input |
| om / W | marks / move tab to window |
| og, od, ow, etc. | configured search aliases |
| : / Q | registered Commands / configured inline dictionary |
| ;cl | Firefox container chooser, then link hints |
| A | optional existing LLM chat |

Enter selects; Shift+Enter flips new/current-tab preference; Ctrl+Enter opens
without closing in handlers supporting that operation. Tab/Shift+Tab cycle
results; Ctrl+n/p may cycle input history instead. Bottom-position omnibar
reverses the visual listing. Ctrl+./Ctrl+, page through results, Ctrl+j changes
position, Ctrl+i edits a selected URL. Ctrl+c copies the chosen URL or listed
URLs; Ctrl+d removes the focused history/bookmark item, Ctrl+Shift+d removes
listed items. These are mutations, not navigation. Ctrl+r changes history sort.

Bookmarks: comma toggles folder-only filtering, Backspace in an empty field goes
to the parent, Enter enters a folder. Ctrl+Enter on a folder opens its bookmarks.
When adding, a trailing `/` means a folder; otherwise the last component can be a
title. Ctrl+Shift+letter creates a mark from a selected URL. Current source also
has Ctrl+m followed by a mark key. Commands accept their own double-quote argument
grouping, not general shell escaping/evaluation. Inspect visible completion.

## Page navigation and scrolling

| Keys | Operation |
| --- | --- |
| j/k, h/l | vertical/horizontal scroll |
| e/u, d | half-page up/down |
| U/P | full-page up/down |
| gg/G, 0/$ | top/bottom, far left/right |
| count then % | scroll to a percentage |
| cs/cS, ;fs | cycle/reset/hint scroll target |
| w / ;w | switch frame / focus top frame |
| S/D, r | page back/forward, reload |
| gu/gU | up path / URL hierarchy root |
| g? / g# | remove query / fragment and reload |
| [[ / ]] | previous/next link (or configured URL-number rule) |
| ;u / ;U | Vim-edit URL into new/current tab |

Scrolling applies to the active scrollable element, not necessarily the page.
Smart page-boundary navigation, fallback and smoothness are user settings.

## Tabs, windows, groups, marks and sessions

`E/R` previous/next tab; `count T` selects by index. `B/F` walks activation history,
Ctrl+6 returns to last used tab, `gT/gt` first/last activated tab, `gp` playing tab.
`x/X` closes/restores; `yt/yT` duplicates active/background. Alt+p pins, Alt+m mutes.
`gxt/gxT` closes a tab left/right, `gx0/gx$` closes that side, `gxx` closes others,
`gxp` closes an audible tab. `<<`/`>>` moves tab position; `zr/zi/zo` controls zoom.

`W` moves the current tab between windows (or creates a window). `;gt` gathers
filtered tabs, `;gw` gathers windows. `;G` displays groups with generated labels
for membership/collapse/new group. `sf` is present only if the browser supports
split view. Surfingkeys itself gates it; don't claim it is universally available.

`m` then a mark key saves URL/scroll position; apostrophe then key jumps; Ctrl+'
then key opens the mark in a new tab. `''` toggles the previous scroll position.
`:listSession`, `:createSession NAME`, `:openSession NAME`, `:deleteSession NAME`
manage saved sessions. `ZZ` saves LAST and quits, `ZR` restores it, `ZQ` quits.
Quit/session/window operations can affect multiple windows; do not use as a reset.

## Clipboard, forms, hover and capture

| Keys | Operation |
| --- | --- |
| yy / count yy / yY | current URL / following tab URLs / all tab URLs |
| yh/yl/ys/yj | host/title/current source/settings |
| ya/yma | link URL(s) by hints |
| yv/ymv | text element(s) by hints |
| yc/ymc | table column(s) by hints |
| yi/yq | input value / preformatted text |
| yf/yp | form data JSON / POST-form representation |
| ;pf | select a form and fill from yf data |
| cc | open selection or clipboard URL(s) |
| Ctrl+h / Ctrl+j / ;m | mouseover / mouseout hints / mouseout last element |
| yg/yG/yS | visible/full-page/scroll-target image popup |

Yanks use the system clipboard; Localsurf does not turn them into MCP text/image
responses. Paste into an intended field, or use existing model tools when reading
is needed. Bulk `cc` may show the extension's own confirmation/URL queue.
`;cq`, `:listQueueURLs`, `:clearQueueURLs` manage that queue.
`;pp` replaces the page's HTML from clipboard; `;pj` restores extension settings.
Do not mistake either for ordinary text entry. `;di` downloads a hinted image.

`scrsht` is the Macrohelp pane workflow, not these browser captures. `yg/yG/yS`
display an image that still requires a native save/copy action. Full-page capture
scrolls/stitches the target; choose Macrohelp's pane when observing the current UI.

## Settings, browsers, PDF, Markdown, Neovim

`;e` opens Settings. Basic mappings, advanced user scripts, domain rules, search
aliases, hint alphabet/numeric hints, disabled/lurk patterns, editor mode and
themes can all change the active behavior. Preserve existing configuration.
Live help is generated from the actual mapping tries; no need invent remap APIs.

Chrome-family builds use MV3 user scripts; Firefox has container flows and a
different settings path; Safari lacks several bookmark/history/window/proxy flows.
Internal/protected pages and extension permissions are browser constraints, not
proof an input task failed. Use native browser controls there. Existing setups
need no readiness wizard or permissions rewrite.

Chrome's optional bundled PDF viewer loads Surfingkeys content integration.
`;s` toggles it; the browser's own PDF viewer cannot expose all Visual/scroll
behavior. The integration disables PDF scripting and packages its worker/fonts.
Localsurf neither embeds nor replaces that renderer.

`;pm` opens Markdown preview; inside it `sm` edits the source, `cc` copies generated
HTML, `of` opens a file, `;s` changes parser. The remote parser sends content to
GitHub if selected; leave local parsing alone for private use. README's claim
that a second `;pm` edits Markdown is stale at this source revision.

ACE is built in. Native Neovim is separate (`;v`, Ctrl+Alt+i) and requires the
user's existing native messaging host; don't create/install one for Localsurf.

## Optional upstream LLM, dictionary, speech and proxy features

`A` opens the upstream LLM pane from Normal, Visual or Regional Hints. It uses the
user's provider configuration, not Localsurf. `/` lists commands; Tab completes:
`/agents`, `/provider`, `/clearPromptHistory`, `/permissions`, `/copy`, `/clear`.
Source behavior beats stale prose: switching provider clears its conversation;
switching agent keeps it. Escape denies a pending tool prompt, otherwise stops
an active answer, otherwise closes the pane. Those are separate steps.

Its tools include page/outline/search/links, bookmarks/history/tabs/downloads,
rendered-tab reads, URL fetch, search/open/highlight/group actions. Their existing
permission UI belongs to Surfingkeys. Localsurf does not configure providers,
approve requests on the user's behalf, or clone that internal browser tool layer.
Insert Ctrl+g corrects input grammar; `;lt`/`;t` or Visual `t` translate text
through the configured LLM. Only use those when that external service is wanted.

`q` in Visual / `Q` / `cq` use the configured inline dictionary. `gr` speaks selected
or clipboard text where supported; `:listVoices`, `:testVoices`, `:stopReading`
control speech. Chrome proxy commands include `:setProxy`, `:setProxyMode`, `cp`,
`;pa/pb/pd/ps/pc`. They are optional existing features, not Localsurf setup steps.

Sources: [default mappings](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/common/default.js),
[omnibar handlers](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/ui/omnibar.js),
[registered commands](https://github.com/brookhong/Surfingkeys/blob/662b8c6faff942d7dafad2658a2682ab65d866a0/src/content_scripts/ui/command.js),
[native private-browser shortcut](https://omarchy.org/manual/hotkeys/).
