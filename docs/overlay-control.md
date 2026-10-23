# Overlay control

The Windows overlay and Omarchy overlay own the same authenticated localhost API.
They drive their existing menus and compiler; the separate Tasket/Omarsket HTTP
daemon still performs execution. Start both components as usual. MCP is a client,
not another executor or server.

## Connection

`.env-example` includes `MACROHELP_CONTROL_ENABLED`, `MACROHELP_CONTROL_PORT`
(default `7778`) and `MACROHELP_CONTROL_KEY`. Windows reads `.env` in its working
directory; Omarchy uses the plugin's existing env-file setting. Process environment
values override the file. Set `MACROHELP_CONTROL_ENABLED=false` to disable access.

A blank key generates a private `control-key`: Windows uses
`%LOCALAPPDATA%\Macrohelp\control-key`; Linux uses
`${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp/control-key` (or
`MACROHELP_STATE_DIR`). Explicit keys need 24–256 printable, non-space ASCII
characters. All routes require `Authorization: Bearer KEY`, including health.
The listener binds only `127.0.0.1`; it does not enable cross-origin browser access.

| Method | Route | Result |
| --- | --- | --- |
| GET | `/v1/health` | Overlay readiness and backend connection |
| GET | `/v1/schema` | Request/config schema and menu inventory |
| GET | `/v1/state` | Config, native dialog, active/last run, busy/error state |
| GET | `/v1/coords` | Current displayed cursor coordinates: `{"x":123,"y":456}` |
| GET | `/v1/help` | Native Registry Hub Help: `platform`, `topic`, `text` |
| POST | `/v1/control` | Apply supplied fields and operate a menu |
| POST | `/v1/stop` | Existing stop-all path; available while busy |

Success is `{"ok":true,"result":...}`. Errors include HTTP status and
`{"ok":false,"error":{"status":409,"message":"..."}}`. An unavailable execution
backend does not prevent reading/editing local state. It cannot execute a macro.
Live key-display history is not exposed by this API.

## Requests

Menu numbers retain the native numbering:
`0` Save Cursor, `1` Paste Buffers, `2` Registry Hub, `3` Circle Placer,
`4/5/6` left/right/middle click, `7` Stop All, `8` Display, `9` Record Keys.
Canonical string names and button actions are listed by `/v1/schema`.

Each `inputs` string submits one dialog field **including Enter**. Workflow text
is literal; it is not split into commands.

Buffer/variable editors and `{type ...}` / `{set ...}` payloads retain actual
line breaks. In quick dialogs, Shift+Enter inserts a line; Enter submits. Literal
backslash-n stays literal (JSON's own string escaping still applies).

For example, save this as `request.json`:

```json
{
  "menu": 2,
  "inputs": ["Z", "{zone Z,X}", "P"],
  "config": {"zones": {"Z": [100, 100, 500, 400]}},
  "no_replace": true
}
```

```powershell
$key = (Get-Content "$env:LOCALAPPDATA\Macrohelp\control-key" -Raw).Trim()
curl.exe http://127.0.0.1:7778/v1/control -H "Authorization: Bearer $key" -H "Content-Type: application/json" --data-binary "@request.json"
```

```sh
key=$(cat "${MACROHELP_STATE_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/macrohelp}/control-key")
curl http://127.0.0.1:7778/v1/control -H "Authorization: Bearer $key" -H 'Content-Type: application/json' --data-binary @request.json
```

The body may also be a JSON-encoded CSV string:
`"2,Z,\"{type hello, world}\",P,no-replace"`. Quote comma-containing fields;
double embedded quotes. Only an unquoted final `no-replace` is a flag.
JSON objects/arrays are preferable for large workflows.

Config patches accept `script`, `registry`, `vars` (A/S/D/F), `buffers` and
`zones` (Z/X/C/V), `points` (numbered keys 1–16), `shells` (X/C/G/P/T),
`paths` (import/export) and `view`. A null point/zone clears that entry.
Shell commands and workflow grammar remain native to each platform.

`no_replace:true` holds temporary overrides through completion, then restores
the prior environment without saving overrides. Otherwise, supplied changes are
saved. Omitted fields are retained in either mode. This is not a dry run: requested
macros, exports and schedule creation still perform their normal effects.

A partial dialog returns `run`. Continue with
`{"run":"RETURNED-ID","inputs":[...]}`; omitted `no_replace` inherits that run's
setting. Asynchronous work is inspected through `/v1/state`; its result remains in
`last_run`, including temporary captures. Other operations return `409` while a
run is active. Use an input action `{"action":"cancel"}` or `/v1/stop` to release
an unfinished dialog. Stop cannot retract an already performed external action.

Requests are capped at 1 MiB/256 inputs. Use one executable flow per run.
Native validation/errors may leave a field awaiting correction; inspect dialog
state. After a timeout, inspect state before retrying an action.

Registry Hub Play dispatches a sequence: every explicit `{wait 200}` starts the
next generated native task, with that wait as its first action. Each task is built
from current points/variables after its predecessor finishes. Values are fixed
once that task is dispatched, including during its wait. Helper-generated waits
stay inside their existing task; standalone saved schedules remain snapshots.
State exposes `sequence` progress; task names/logs identify `1/n`, `2/n`, etc.
Stop All cancels the pending sequence as well as backend execution.

## MCP

Configure an MCP client's stdio server with one of these commands; the overlay
must already be running with the same control key/port:

- Windows: `runtime/source/build-vs2026/bin/Release/macrohelp-mcp.exe --env ABSOLUTE_ENV_PATH`
- Linux: `~/.local/share/macrohelp/overlay/bin/macrohelpctl mcp --env ABSOLUTE_ENV_PATH`

Tools: `macrohelp_control`, `macrohelp_state`, `macrohelp_coords`, `macrohelp_stop`,
`macrohelp_schema`, `macrohelp_health`, `macrohelp_help`. They forward to the HTTP API with the
same arguments and return structured results. Tokens never go to MCP stdout.

[The skillbook](../skills/macrohelp/SKILL.md) covers discovery, native menu flows,
temporary configuration and recovery. It is also embedded as the MCP resource
`macrohelp://guide`; no separate skill installation is needed to read it.
`macrohelp://registry-hub` reads the running overlay's own Help page and
`macrohelp://schema` reads its HTTP contract. Tool-only clients can use
`macrohelp_help` and `macrohelp_schema` instead. Both native clients share one
tool/resource catalog; help grammar remains specific to the running platform.

Invalid tool arguments return `isError:true` with `error.status`, `message` and
`next` guidance; malformed JSON-RPC returns a protocol error with recovery data.
Nothing automatically rewrites or retries an action. Inspect state before retrying
after a native error, timeout or disconnect because effects may already have begun.

Read coordinates with `curl http://127.0.0.1:7778/v1/coords -H "Authorization: Bearer $key"`.
This uses the overlay's existing displayed coordinate source; it does not move the pointer.
On Linux, dismissing a blinked quick dialog returns the pointer to its frozen entry
position using a memory-only `/temp-task` with `cleanup:true`. Escape and the
existing 60px dismissal use this; running a macro retains its own pointer actions.

## Checks

`ctest --test-dir runtime/source/build-vs2026 -C Release --output-on-failure`
checks native state/restore, UI-thread dispatch and real loopback HTTP without
desktop input. `cargo test --offline --manifest-path runtime/source/wld/Cargo.toml`
and `bun test runtime/source/wld/tests` cover the Rust transport and QML models.
Set `MACROHELP_CPP_CONTROL_TEST` to the built `control-native-tests.exe` and
`MACROHELP_CPP_MCP` to `macrohelp-mcp.exe` when running Cargo tests to additionally
compare both schemas/normalizers and exercise both MCP clients against HTTP.
