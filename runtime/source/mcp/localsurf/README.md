# Localsurf

Surfingkeys workflows over the existing Macrohelp MCP. Python standard library
only; no native rebuild, extra server, browser bridge or capture implementation.
Both the native Windows MCP and Wayland `macrohelpctl mcp` remain supported.

Start the overlay and its execution backend as usual, then configure a stdio MCP:

```sh
python3 runtime/source/mcp/localsurf/server.py --mcp /path/to/macrohelpctl --env /path/to/.env
```

On Windows, use `python` and the existing `macrohelp-mcp.exe` path. `uv run` may
also run the script; no dependencies need installing. The command starts only the
existing MCP adapter, not the overlay, daemon or browser.

Optional `--bindings PATH.json` sets recipe overrides, leaving Surfingkeys alone:

```json
{"line_start":"<Ctrl-q>","line_end":"<Ctrl-e>","private_browser":"<Meta-Shift-Alt-b>"}
```

The private-browser binding above is Omarchy's default, not a Windows shortcut.
Without an override, `private_browser` asks for the current native `keys` recipe.
The model reuses the workspace's private window before launching another.

Optional client registration (no clients/settings are changed until run):

```sh
./integrate.sh --target omp --server localsurf --mcp /path/to/macrohelpctl --env /path/to/.env --bindings /path/to/bindings.json --dry-run
```

Remove `--dry-run` to register; add `uninstall` to remove only that registration
and unmodified bundled skill files. The existing `macrohelp` entry is untouched.
`--target hermes` and named `--profile` work through the same integration helper.

Read `localsurf://guide`. Four translated tools supplement the unchanged native
tools. `preview:true` returns generated requests without desktop actions.
`scrsht` uses the existing capture pane; the model finds/views the saved image.

Host-only checks:

```sh
python3 -m unittest discover -s runtime/source/mcp/localsurf -p 'test_*.py'
python3 -m unittest discover -s scripts -p 'test_integrate.py'
```

Run `bun test tests/localsurf.test.js` from `runtime/source/wld` for the real QML
compiler/control checks. Set `MACROHELP_CPP_CONTROL_TEST` to the existing
`control-native-tests` executable to include native C++ schema checks.
These tests check translation and MCP forwarding without pressing desktop keys;
they do not claim that a live browser interaction was tested.
