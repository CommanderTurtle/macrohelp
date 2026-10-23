#!/usr/bin/env python3
"""Localsurf stdio harness; all desktop actions use the existing Macrohelp MCP."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from workflows import RECIPES, UPSTREAM, compile_tool, validate_bindings

ROOT = Path(__file__).resolve().parents[4]
GUIDE = ROOT / "skills/localsurf"
LIMIT = 1024 * 1024 + 16384  # same incoming envelope bound as native MCP
REFERENCES = {
    "guide": GUIDE / "SKILL.md",
    "modes": GUIDE / "references/modes.md",
    "workflows": GUIDE / "references/workflows.md",
    "reference": GUIDE / "references/reference.md",
}


def tools():
    def tool(name, description, properties, required, read=False):
        return {"name": name, "description": description,
                "inputSchema": {"type": "object", "properties": properties,
                                "required": required, "additionalProperties": False},
                "annotations": {"readOnlyHint": read, "destructiveHint": not read,
                                "idempotentHint": read, "openWorldHint": not read}}
    string = {"type": "string"}
    preview = {"type": "boolean", "description": "Return the compiled Macrohelp request only; do not execute."}
    return [
        tool("localsurf_keys", "Send sequential Surfingkeys notation (gg, G, <Ctrl-i>, <Esc>), preserving case/chords. No automatic Escape/reset or mode assumption. Returns the native Macrohelp run; inspect macrohelp_state for completion. preview only compiles.",
             {"keys": {"type": "string", "minLength": 1}, "preview": preview}, ["keys"]),
        tool("localsurf_type", "Paste arbitrary literal multiline/Unicode text into the currently focused editable using temporary Macrohelp buffer Z. Braces, $variables, commas and literal \\n stay data. Optional before/after are key notation; no automatic Enter. Does not choose a field or editor mode for you.",
             {"text": string, "before": string, "after": string, "preview": preview}, ["text"]),
        tool("localsurf_workflow", "Translate a named Surfingkeys workflow into existing Macrohelp syntax and run it with temporary config. Read localsurf_reference topic=workflows for current-mode requirements. keys overrides the default mapping; text is required by find/navigate/navigate_new/command/search. scrsht opens the existing native capture pane ONLY; the model locates/views the saved file with its own tools.",
             {"name": {"type": "string", "enum": list(RECIPES)}, "keys": string,
              "text": string, "settle_ms": {"type": "integer", "minimum": 0, "maximum": 86400000},
              "preview": preview}, ["name"]),
        tool("localsurf_reference", "Read source-derived Surfingkeys guidance, translated workflow catalog, or configured recipe bindings. Optional query returns matching sections. This is not live browser introspection; ? and visible menus reflect the actual current mappings.",
             {"topic": {"type": "string", "enum": list(REFERENCES) + ["bindings"]},
              "query": string}, [] , True),
    ]


def resources():
    return [{"uri": f"localsurf://{name}", "name": f"Localsurf {name}",
             "mimeType": "text/markdown"} for name in REFERENCES] + [
        {"uri": "localsurf://bindings", "name": "Localsurf configured recipe bindings", "mimeType": "application/json"}]


def result(value, error=False):
    return {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}],
            "structuredContent": value, "isError": error}


def parse_json(data):
    def reject(value):
        raise ValueError("Non-finite JSON number: " + value)
    return json.loads(data, parse_constant=reject)


class Peer:
    """One existing native adapter, no HTTP client or desktop executor here."""
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def call(self, message):
        data = json.dumps(message, ensure_ascii=False).encode("utf-8") + b"\n"
        if len(data) > LIMIT:
            raise ValueError("MCP request exceeds the native 1 MiB envelope limit")
        self.process.stdin.write(data)
        self.process.stdin.flush()
        if "id" not in message:
            return None
        data = self.process.stdout.readline(4 * LIMIT + 1)
        if not data:
            raise OSError("Macrohelp MCP closed; inspect macrohelp_state after reconnecting before retrying")
        if len(data) > 4 * LIMIT:
            raise OSError("Macrohelp MCP response exceeds its native response bound")
        try:
            reply = parse_json(data)
        except (ValueError, UnicodeError) as error:
            raise OSError("Invalid Macrohelp MCP reply; execution status is unknown. Inspect state before retrying") from error
        if not isinstance(reply, dict) or reply.get("id") != message["id"]:
            raise OSError("Unexpected Macrohelp MCP response; do not replay an uncertain action")
        return reply

    def close(self):
        try:
            self.process.stdin.close()
        except OSError:
            pass
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            # Ends only this adapter. It does NOT stop the overlay/backend or undo
            # schedules already accepted there.
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        finally:
            self.process.stdout.close()


class Session:
    def __init__(self, peer, bindings=None):
        self.peer, self.bindings, self.initialized = peer, bindings or {}, False

    def reference(self, topic, query=""):
        if topic == "bindings":
            text = json.dumps({"overrides": self.bindings, "upstream": UPSTREAM,
                               "note": "Recipe overrides only; not a readback of live Surfingkeys configuration."}, indent=2)
        elif topic in REFERENCES:
            text = REFERENCES[topic].read_text(encoding="utf-8")
            if topic == "workflows":
                text += "\n## Available translated workflows\n\n" + "\n".join(
                    f"- `{name}` — `{self.bindings.get(name, keys) or '(supply keys)'}` — {help_text}"
                    for name, (keys, help_text) in RECIPES.items())
        else:
            raise ValueError("Unknown topic; use guide, modes, workflows, reference or bindings")
        if not isinstance(query, str):
            raise ValueError("query must be a string")
        if query:
            sections = text.split("\n\n")
            matches = [s for s in sections if query.casefold() in s.casefold()]
            text = "\n\n".join(matches) if matches else "No matching section; read the topic without query."
        return text

    def dispatch(self, message):
        # Invalid envelopes may produce a response even without an id. Handle
        # them here rather than leaving an unread reply in the child pipe.
        if (not isinstance(message, dict) or message.get("jsonrpc") != "2.0"
                or not isinstance(message.get("method"), str)
                or ("id" in message and message["id"] is not None
                    and type(message["id"]) not in (str, int, float))):
            return {"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid JSON-RPC request"}}
        if "id" not in message:
            return self.peer.call(message)
        params = message.get("params", {})
        if not isinstance(params, dict) or not self.initialized:
            reply = self.peer.call(message)
        elif message["method"] == "tools/call" and isinstance(params.get("name"), str) and params["name"].startswith("localsurf_"):
            try:
                name, args = params["name"], params.get("arguments", {})
                if name == "localsurf_reference":
                    if not isinstance(args, dict) or args.keys() - {"topic", "query"}:
                        raise ValueError("Use only topic and optional query")
                    value = result({"text": self.reference(args.get("topic", "guide"), args.get("query", ""))})
                else:
                    request = compile_tool(name, args, self.bindings)
                    if args.get("preview", False):
                        value = result({"request": request, "executed": False})
                    else:
                        # Preserve the native result/run/errors exactly; no auto retry,
                        # polling loop, synthesized success, or extra permission gate.
                        return self.peer.call({**message, "params": {"name": "macrohelp_control", "arguments": request}})
                return {"jsonrpc": "2.0", "id": message["id"], "result": value}
            except (ValueError, TypeError, KeyError) as error:
                return {"jsonrpc": "2.0", "id": message["id"], "result": result({"error": {
                    "message": str(error), "next": "Read localsurf_reference and correct this request. Nothing was executed; use raw macrohelp_* for native controls."}}, True)}
        elif message["method"] == "resources/read" and str(params.get("uri", "")).startswith("localsurf://"):
            topic = params["uri"].removeprefix("localsurf://")
            if topic not in REFERENCES and topic != "bindings":
                return {"jsonrpc": "2.0", "id": message["id"], "error": {"code": -32602, "message": "Unknown Localsurf resource; use resources/list"}}
            return {"jsonrpc": "2.0", "id": message["id"], "result": {"contents": [{
                "uri": params["uri"], "mimeType": "application/json" if topic == "bindings" else "text/markdown",
                "text": self.reference(topic)}]}}
        else:
            reply = self.peer.call(message)
        if reply and "result" in reply:
            value = reply["result"]
            if message["method"] == "initialize":
                self.initialized = True
                value["serverInfo"] = {"name": "localsurf", "version": "0.1.0"}
                value["instructions"] += "\nRead localsurf://guide. Use localsurf_keys/type/workflow for translated Surfingkeys controls; raw macrohelp_* remain unchanged. Live help/mode/context wins over defaults. Capture uses the existing pane; locate and view its saved file with your own tools."
                if self.bindings:
                    value["instructions"] += "\nConfigured recipe overrides: " + json.dumps(self.bindings, ensure_ascii=False)
            elif message["method"] == "tools/list":
                value["tools"].extend(tools())
            elif message["method"] == "resources/list":
                value["resources"].extend(resources())
        return reply


def backend_command(binary=None, env=None):
    candidates = ([ROOT / "runtime/source/cmake-build-control/bin/Release/macrohelp-mcp.exe",
                   ROOT / "runtime/source/build-vs2026/bin/Release/macrohelp-mcp.exe"] if os.name == "nt" else [
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "macrohelp/overlay/bin/macrohelpctl",
        ROOT / "runtime/source/wld/target/release/macrohelpctl"])
    if binary is None:
        binary = shutil.which("macrohelp-mcp" if os.name == "nt" else "macrohelpctl") or next((p for p in candidates if p.is_file()), candidates[0])
    binary = Path(binary).expanduser().resolve()
    if not binary.is_file() or binary.stem.lower() not in {"macrohelpctl", "macrohelp-mcp"}:
        raise ValueError("Pass --mcp PATH to the existing macrohelpctl or macrohelp-mcp.exe. Localsurf builds/starts no overlay.")
    return [str(binary)] + (["mcp"] if binary.stem.lower() == "macrohelpctl" else []) + (["--env", str(Path(env).expanduser().resolve())] if env else [])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mcp", type=Path, help="existing native Macrohelp MCP binary")
    parser.add_argument("--env", type=Path, help="existing overlay env file; passed unchanged to its MCP")
    parser.add_argument("--bindings", type=Path, help="optional workflow-name -> key-notation JSON overrides")
    options = parser.parse_args()
    bindings = validate_bindings(parse_json(options.bindings.read_text(encoding="utf-8-sig")) if options.bindings else {})
    peer = Peer(backend_command(options.mcp, options.env))
    session = Session(peer, bindings)
    try:
        while True:
            line = sys.stdin.buffer.readline(LIMIT + 1)
            if not line:
                break
            if len(line) > LIMIT:
                raise ValueError("MCP message exceeds the native 1 MiB envelope limit")
            try:
                message = parse_json(line)
            except (ValueError, UnicodeError):
                reply = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error; send one JSON-RPC object per line"}}
            else:
                try:
                    reply = session.dispatch(message)
                except OSError:
                    reply = None if isinstance(message, dict) and "id" not in message else {
                        "jsonrpc": "2.0", "id": message.get("id") if isinstance(message, dict) else None,
                        "error": {"code": -32603, "message": "Macrohelp MCP connection failed. Execution status is unknown; reconnect and inspect macrohelp_state before retrying."}}
            if reply is not None:
                sys.stdout.buffer.write(json.dumps(reply, ensure_ascii=False).encode("utf-8") + b"\n")
                sys.stdout.buffer.flush()
    finally:
        peer.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print(f"Localsurf: {error}", file=sys.stderr)
        sys.exit(1)
