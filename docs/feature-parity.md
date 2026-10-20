# Feature parity

Source audit: 2026-10-18. Each row compares the same feature in both implementations.
**Present** means implemented in the inspected source, not tested on a live Omarchy
desktop. **Partial**, **Absent**, and **Different** identify remaining work or an
intentional platform boundary. These are not claims of 100% menu or runtime parity.

Daemon evidence: [Qt routes](../http-daemon/src/HttpServer.cpp),
[Qt runner](../http-daemon/src/TaskRunner.cpp),
[Qt lifecycle wiring](../http-daemon/src/main.cpp),
[Rust routes](../http-daemon/wld/src/http_server.rs),
[Rust runner](../http-daemon/wld/src/task_runner.rs),
[Rust contract tests](../http-daemon/wld/tests/http_contract.rs).
Overlay evidence: [Windows implementation](../runtime/source/src/main.cpp),
[Windows dialogs](../runtime/source/res/CursorOverlay.rc),
[Wayland panel](../runtime/source/wld/qml/Panel.qml),
[workflow compiler](../runtime/source/wld/qml/model/Workflow.js),
[client service](../runtime/source/wld/qml/Service.qml),
[native client](../runtime/source/wld/src/overlay.rs).

<details>
<summary>HTTP daemon — endpoints, execution, hooks and configuration</summary>

| Feature / contract | Windows · Tasket HTTP daemon | Wayland · Macrohelp HTTP daemon |
| --- | --- | --- |
| H01 · Separate manual process | `tasket-httpd.exe`; independent of overlay | Present: `macrohelp-httpd`; never launched by the overlay |
| H02 · Execution ownership | Compiles Tasket action classes into its own `TaskExecutor` threads; does not remotely control the Tasket GUI | Different: communicates with the running Omarsket owner's socket; one shared backend queue |
| H03 · GET `/` | Name, version, address, directory, auth, default delay, tools | Present: equivalent inventory; platform-specific daemon/backend identity |
| H04 · GET `/health` | Listener status and saved-file count | Present: also requires a successful Omarsket snapshot; not merely an open HTTP socket |
| H05 · GET `/tasks` | Scans `.scht` files; name, delay, description | Present: reloads native schedules; same public name/delay/description fields |
| H06 · GET `/run` | Query `task`, optional `delay`, `loop` | Present |
| H07 · POST `/run` | JSON `task`, optional `delay`, `loop` | Present |
| H08 · Saved-task name resolution | Optional `.scht`, case-insensitive fallback | Present: `.scht` / `.omscht`, case-insensitive fallback; unsafe paths rejected |
| H09 · Default delay | CLI default 10s; per-file `delay` / `http_delay`; request override | Different: generated env default 1s, native task delay and request override; bounded to 0–86400s |
| H10 · Finite loops | Request count; default 1 | Present: 1–100000; zero rejected |
| H11 · Infinite loops | GET `inf` / `infinite`; POST `infinite` | Present: `inf`, `infinite`, or -1 through common integer normalization |
| H12 · POST `/temp-task` | `name`, Tasket `task` object, `delay`, `loop`, `run`, `cleanup` | Present: same controls; accepts supported native action forms too |
| H13 · Temp task, `run:false` | Write file only | Present: validates then saves only |
| H14 · Temp task, `cleanup:true` | Writes file, removes after completion/failure/stop; write-only removes immediately | Different: memory-only plan; no temporary schedule artifact to clean |
| H15 · Name collision on temp save | Writes to resolved task filename | Different: refuses collision with existing saved schedule; never silently overwrites it |
| H16 · Temp task response | `name`, `path`, `created`, `scheduled`, optional job/cleanup | Present: also `storage`; memory-only path is empty |
| H17 · GET `/check?id=N` | Numbered state, timestamps, description, failure detail | Present: also engine session, action index and completed-loop count |
| H18 · GET `/task?id=N` | Alias of `/check` | Present |
| H19 · GET `/status` | Scheduled/running/finished arrays and counts | Present: shared Omarsket job ledger, not a second HTTP-only scheduler |
| H20 · POST `/stop?id=N` | Cancel one daemon-owned task/timer | Present: cancel the given shared backend job |
| H21 · POST `/stop` | Stop all tasks in daemon's own executor | Different: stops the shared Omarsket queue, including independently queued jobs |
| H22 · POST `/entrypoint` | Stores `id`, string `value`; echoes optional `type` label | Present: string values; additionally accepts bounded numbers/booleans. `type` is metadata, not a new evaluator |
| H23 · GET `/entrypoints` | In-memory values and count | Present |
| H24 · POST `/grid` | Stores `id`, string `value` | Present: additionally accepts bounded numbers/booleans |
| H25 · GET `/grid` | In-memory cells and count | Present |
| H26 · Entrypoint/grid persistence | Lost when daemon exits | Present: likewise memory-only; not wired to task branching or a database |
| H27 · Response envelope | `success`, `message`, optional nonempty `data` | Present: same named fields, null data omitted; callers must not require byte-identical JSON |
| H28 · Unknown task/id/route | JSON errors with HTTP status | Present: 404; malformed arguments 400; backend unavailable 503/502 as appropriate |
| H29 · Header authentication | Optional CLI `--key`, `X-Tasket-Key` | Different: required generated/private env key; missing 401, wrong 403 |
| H30 · Listen address | CLI `--bind`; source default `0.0.0.0`, quick start explicitly uses loopback | Different: loopback-only env bind; port configurable |
| H31 · Browser/CORS access | No application-level origin policy in route handlers | Different: rejects Origin / cross-site requests; no browser automation API exposed |
| H32 · HTTP bounds | cpp-httplib transport defaults | Different: explicit 16KiB headers, 2MiB body, deadlines, four workers, duplicate-header/query rejection |
| H33 · Input action: paste | `content`, `contentId`, `loop` | Present: backend clipboard paste; named reusable data resolved by Omarsket |
| H34 · Input action: wait | `duration` seconds | Present; legacy `WaitAction` milliseconds normalized too |
| H35 · Input action: keys sequence | Timed `keysmap`, `keysSeqId`, repeat | Present: backend press/release sequence |
| H36 · Input action: cursor movements | Timed positions, reusable ID, optional held keys, repeat | Present: backend logical-coordinate movement |
| H37 · Action: system command | Tasket's system/file commands | Present with explicit permission; Linux meanings differ, see Omarsket's parity table |
| H38 · Action: run another task | Name, delay, loops; Tasket stub forwards child request | Present: native nested plan; rejects cycles/depth overflow |
| H39 · Native convenience actions | Not in Qt loader | Additional: move, click, scroll, hotkey, type |
| H40 · Invalid action type | Qt loader skips unrecognized action entries | Different: rejects before queuing; never silently drops work |
| H41 · Request hook | Qt `requestReceived` signal and console logging | Different: Rust dispatch/result handling; no equivalent Qt signal subscriber API |
| H42 · Scheduling hook | `executeRequested` → runner after QTimer | Present through Omarsket scheduler and socket requests; no Qt hook required |
| H43 · Started / finished / failed hooks | Registry signals and runner completion wiring | Present through backend job state/events; HTTP clients query `/check` or `/status` |
| H44 · Webhook callbacks / SSE / WebSocket | Not implemented | Not implemented; do not imply push subscriptions from lifecycle hooks |
| H45 · Stop/exit cleanup | Timers/executors stopped; remembered temp files removed | Present: Ctrl+C cancels only HTTP-owned jobs by session + ID; independent jobs survive daemon exit |
| H46 · GUI quit action | Tasket MainWindow stub ignores `forceQuit` in sidecar | Different: Omarsket backend owns its own quit semantics; HTTP does not supervise it |
| H47 · Backend failure | Task file/load error marks failed | Present: socket/plan error returned; backend job failures retained in job ledger |
| H48 · Runtime configuration | CLI flags; Qt build pulls Tasket source | Present via `.env`, `--env`, environment overrides; separate Rust build uses existing Omarsket install |
| H49 · Installation boundary | Separate Qt binary + DLLs | Present: separate native server binary; no input engine installed by HTTP build |
| H50 · Privacy | Local process; no telemetry API | Present: no telemetry/cloud calls; credentials stay outside QML |

</details>

<details>
<summary>Overlay — every Windows dialog, menu, workflow primitive and Linux counterpart</summary>

| Feature / control | Windows · CursorOverlay | Wayland · Omarchy overlay |
| --- | --- | --- |
| O01 · Entry point | Native executable, global hotkeys | Present as shell bar widget + opt-in native bindings; not a standalone window or separately registered tray icon |
| O02 · Main editor placement | Modeless Registry Hub dialog and command router | Different: native popup with Workflow / Pointer / Keys / Buffers / Zones / Tasks / Presets / View pages |
| O03 · Theme, fonts, scale | Windows/GDI styling; optional scale env | Present through live Omarchy Color/Style, shell scaling and panel bounds; no custom refresh-rate control |
| O04 · Multi-monitor | Physical Windows virtual-screen coordinates | Present using Hyprland logical coordinates, negative origins and per-screen nonfocusable HUDs |
| O05 · Crosshair | Toggle crosshair / full-screen line windows | Present: View → Crosshair; drawing differs from Windows fast-line overlay |
| O06 · Grid lines | View toggle | Present: View → Grid |
| O07 · Coordinate display | Cursor coordinates + separate top-right coordinate panel | Partial: cursor-following coordinates present; no separate fixed top-right panel |
| O08 · Coordinates-only view | Visibility preset | Present: View → Coordinates only |
| O09 · Live every-key PromptFont HUD | Native low-level/raw key input with transient history | Present: native Hyprland hook, bundled PromptFont, modifiers/numpad/media, fade; no raw keystroke files |
| O10 · Key HUD disable / lock / shutdown | Windows visibility and process lifecycle | Present: collection gated independently, session-lock suppression, hook lease and cleanup |
| O11 · Heatmap/Pomodoro | Not in Windows baseline | Additional: aggregate-only named sessions exposed to Omarsket; independent on/off/timers/persistence |
| O12 · Shift+Alt+0 | Save Cursor Position dialog | Present: optional binding saves current point; Pointer has capture/manual point controls |
| O13 · Save Cursor Position: named movement list | Name stage, Add Another, Save + Click, append action JSON file | Partial: 16 numbered points + timed path recording/replay; no equivalent named append-file dialog or Save + Click button |
| O14 · Shift+Alt+1 | Paste buffers / zone-buffer router | Present: Buffers; Zones is a separate page |
| O15 · Paste Buffers: edit Z/X/C/V | Four text buffers | Present: buffer selector + multiline editor |
| O16 · Paste Buffers: clipboard → slot | Store current clipboard | Present |
| O17 · Paste Buffers: paste / paste + Enter | Slot actions and sequence router | Present: individual UI actions; multi-buffer sequences through `{buf Z,X}` / `{enter Z,X}` |
| O18 · Paste Buffers: ordered sequence picker | Dedicated staged selection flow | Partial: same operation in workflow syntax; no staged sequence-picker UI |
| O19 · Paste Buffers: zone edit/play/store router | Z/X/C/V zones, method and target selection | Present on Zones page rather than single-character router |
| O20 · Shift+Alt+2 | Registry Hub | Present: Workflow page |
| O21 · Registry Hub: script editor | Script stage with Play | Present: multiline editor + Play; hides panel before execution |
| O22 · Registry Hub: Clear | Clear script-local state, retain paste/zone buffers | Absent: no dedicated Clear action with those exact reset semantics; manual editing is not equivalent |
| O23 · Registry Hub: H Help | Full Field Manual, examples, router and token reference | Partial: inline token hint only; no equivalent full help page/search |
| O24 · Registry Hub: R registry path | Stored rgd target file path | Present: Workflow → Regedited file path |
| O25 · Registry Hub: X/C/G shell fields | User command aliases | Present: Workflow shell fields; Linux C defaults to `sh`, not unavailable Windows `cmd` |
| O26 · Registry Hub: default terminal | Windows Win+Alt+Space launcher assumption | Present: active native terminal shortcut discovery; editable T shortcut overrides it, usually Super+Enter |
| O27 · `{terminal}` | No explicit token in Windows compiler | Additional: open terminal only, retaining its configured default shell |
| O28 · `{bash}` / `{shell X}` | Open visible launcher, enter configured X command | Present: terminal shortcut → wait → type X → Enter; blank X opens terminal only |
| O29 · `{cmd}` / `{shell C}` | Configured C command; default cmd | Present: same visible flow; command is user-editable, no executable installation/detection |
| O30 · `{python}`, `{py}`, `{regedited}`, `{regbin}`, `{shell G}` | All resolve to G field | Present: same shared G alias, default `uv run python` |
| O31 · `{powershell}` | Windows-specific launcher, wait, Enter | Different: `{powershell}` / `{pwsh}` use editable P command, default `pwsh`, through native terminal; user installs shells |
| O32 · Shell commands vs native processes | Visible macro flow; generic exec disabled | Present: no subprocess launch or package install for shell aliases; command must be single-line |
| O33 · Registry Hub: A/S/D/F | Four script variables | Present: Buffers → Variable, plus `{set A ...}` |
| O34 · Registry Hub: E/I path editors | Saved import/export path fields | Partial: native file picker import/export; no persistent separate E/I path-editor controls |
| O35 · Registry Hub: Export/Import environment | Plaintext full-state export/import | Present for data: native JSON export and Windows plaintext import; no Windows-format export |
| O36 · Registry Hub: Save Schts | Emits compiled native schedules | Present: Save schedule for input-only workflows; local clipboard/rgd steps require Play or environment export |
| O37 · Registry Hub: Q visibility router | Display toggles | Present: View page |
| O38 · Registry Hub: points 1–16 | Important points and references | Present: Pointer selector, manual coordinates, capture and clear |
| O39 · Registry Hub: N zones | Zone selection/editing | Present: Zones page |
| O40 · Literal keys / chords | Unknown brace token becomes Tasket chord | Present: `{CTRL+S}`, key aliases; exact Windows alias vocabulary is not exhaustively ported |
| O41 · Wait / pointer / click | `{wait}`, `{pt}` / `{point}`, `{left}` / `{right}` / `{middle}` | Present |
| O42 · Text / buffers / Enter | `{type}`, `{paste}`, `{buf}`, `{enter}`, click-on/off | Present; ordinary text uses backend paste, shell commands use backend typing |
| O43 · Counted modifier movement | `{ctrlmod}`, `{controlmod}`, `{shiftmod}` | Present |
| O44 · Counted wheel aliases | Wheelup/down, scrollup/down, mousewheel aliases and canonical mouse-wheel tokens | Partial: wheelup/down and scrollup/down explicit; remaining aliases do not have full counted-token normalization |
| O45 · References | Vars, buffers, points, registry, shells | Present for `$A/S/D/F`, `$Z/X/C/V`, `$1…16`, `$REGISTRY`, `$REGEDITED`, `$SHELLX/C/G` |
| O46 · Set statements | Vars/buffers/registry/shell/point/zone | Present for direct values; shell P/T added; point/zone shorthand-copy variants below are incomplete |
| O47 · Point copy shorthand | `{set point 4 3}` copies a point | Partial: explicit `$3` coordinates work; bare index-copy form absent |
| O48 · Zone shorthand | Coordinates, two point references, copy another zone | Partial: explicit four coordinates and expanded point coordinates; bare zone-copy/index shorthand absent |
| O49 · Conditions | Nested if/else/endif; empty/contains/string/numeric comparisons | Present; evaluated on Play/Save, not a live continuously running predicate engine |
| O50 · Daemon-backed conditions | Task state/remaining/message, saved-task exists/count | Present using task/status snapshots and remembered last task |
| O51 · Saved task chaining | `{play name[.scht]}` | Present; normalizes `.scht` / `.omscht` |
| O52 · Zone capture | `{zone slot,method}`; screenshot/OCR/single-line/copy | Present: discovered/user shortcuts and drag macros, no screenshot/OCR engine in overlay |
| O53 · Zone capture to buffer | `{zonebuf slot,method,target}` | Present: text methods only, live clipboard result resolves later references |
| O54 · rgd clip/append | Narrow rgd adapters | Present: external `rgd` required; references passed through; no database parser added |
| O55 · Generic exec/system/sys | Disabled in Registry Hub | Present: rejected; explicit backend task permissions are separate |
| O56 · Shift+Alt+3 | Circle Placer | Present: Pointer page |
| O57 · Circle Placer: radius | Radius, visible preview | Present |
| O58 · Circle Placer: radians/degrees | Angle mode | Present |
| O59 · Circle Placer: vector / scaled direction | `i+j` expression, scaled direction syntax | Partial: numeric `x,y` offset field; no equivalent scaled-direction expression parser |
| O60 · Circle Placer: move / append / next circle | Three separate actions | Partial: move target, target → point, set origin present; named JSON append / chained staged circle UI absent |
| O61 · Circle Placer: ZoneMake | Circle bounds into zone/capture flow | Present: Circle → zone, then Zones method controls |
| O62 · Shift+Alt+4/5/6 | Manual Click: left/right/middle | Present: same optional hotkeys and Keys buttons |
| O63 · Manual Click: delayed/repeated/held click | Click count and hold-duration behavior | Present: Keys → Hold ms / Repeat; timing is a queued backend action |
| O64 · Manual Click: Z hold / X release | Separate indefinite hold and release controls | Absent as paired UI controls; bounded timed hold is not equivalent |
| O65 · Shift+Alt+7 | Stop all through HTTP | Present: optional binding, Stop button, bar right-click; independent emergency client path |
| O66 · Shift+Alt+8 | Toggle top-right panel | Different: opens View; no top-right panel counterpart |
| O67 · Shift+Alt+9 | Record Keys / manual-key dialogs | Present: Keys page, but recorder/picker differences below |
| O68 · Record Keys: capture chord, timings | Capture Key / Add Step / step delay | Present: focused recording captures press/release timing; editable action JSON; not global recording |
| O69 · Record Keys: text/paste blocks | Add Text/Paste inside recorded flow | Partial: workflow text/buffer tokens exist; no mixed-flow recorder button |
| O70 · Manual Keys: categories / three-key picker | Mouse, numpad, navigation, functions, toggles, symbols | Absent: chord text field + focused recorder, not categorized picker |
| O71 · Record Keys: named append-file output | Save action JSON to named file | Partial: editable JSON displayed and replayable; no direct named action-file export button |
| O72 · Pointer path recording | Named recorded cursor-action lists | Present: timed path capture/replay; raw path not separately exported by a matching Windows dialog |
| O73 · Closing behavior | Escape; several HUDs also close on pointer movement | Different: native popup dismissal/Escape; no universal pointer-move-to-close behavior |
| O74 · Workflow cancellation | Stop daemon tasks | Present: invalidates delayed/capture/clipboard callbacks as well as stopping backend jobs |
| O75 · Offline server | HTTP execution unavailable | Present: unanswered HTTP is silent; HUD/local editing continue; local file/tool errors remain visible |
| O76 · Saved workflow presets | Not in Windows baseline | Additional: Presets exports selected code/vars/buffers/zones/points/shells/view; import/run and `{profile name}` expansion |
| O77 · External editor bridge | State/command/circle-preview files for web editor integration | Absent: Wayland uses native IPC; Windows external file protocol is not implemented |
| O78 · Task management page | Registry conditions + stop helper | Additional: Tasks lists saved tasks and recent jobs with per-job Stop |
| O79 · Shell shortcut/config changes | Windows assumptions | Present: native bindings re-read for compilation; per-environment T override and plugin shortcuts respected |
| O80 · Build / reload | CMake native executable | Present: independent Rust client build + shell QML; kept-loaded service/client rebuild requires shell reload |
| O81 · Tray right-click menu | Visibility, buffers, keys, circle, clicks, stop, view, hub, cursor, Exit | Partial: equivalent actions distributed across bar/panel pages; right-click is Stop only, not a duplicate tray menu |
| O82 · Tray Exit | Quit overlay executable | Native replacement: disable Macrohelp plugin; never exit the shared Omarchy shell |

</details>

## Remaining parity checklist

- [ ] Registry Hub full Help and Clear/reset controls (O22–23).
- [ ] Named cursor/key action-file output, Save + Click, mixed key/paste recorder,
  categorized manual picker and ordered paste selection UI (O13, O18, O69–71).
- [ ] Separate top-right coordinate panel and its hotkey (O07, O66).
- [ ] Separate indefinite mouse hold/release controls with safe cancellation (O64).
- [ ] Remaining shorthand/alias grammar and scaled vector expressions (O40, O44,
  O47–48, O59–60).
- [ ] Persistent E/I controls and Windows external-editor bridge, if retaining those
  Windows integration surfaces on Linux (O34, O77).
- [ ] Live Omarchy build/render/input checks. Source/model tests do not validate
  compositor focus, device input, IME, installed shell commands or real authorization.

The full backend comparison, including per-system-command mappings and all keyboard
layers, lives in [Omarsket's parity document](https://github.com/CommanderTurtle/omarsket/blob/main/docs/feature-parity.md).
