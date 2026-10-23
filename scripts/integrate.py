#!/usr/bin/env python3
"""Register the existing overlay MCP and guide; never build or start the overlay."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SKILL = ROOT / "skills/macrohelp/SKILL.md"


def read_config(file):
    value = json.loads(file.read_text(encoding="utf-8-sig")) if file.exists() else {}
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {file}")
    if "mcpServers" in value and not isinstance(value["mcpServers"], dict):
        raise ValueError(f"Expected an mcpServers object: {file}")
    return value


def backup(file):
    if file.exists():
        fd, name = tempfile.mkstemp(prefix=file.name + ".macrohelp-backup-", dir=file.parent)
        with os.fdopen(fd, "wb") as output:
            output.write(file.read_bytes())
        return Path(name)
    return None


def write(file, text, dry_run):
    if file.exists() and file.read_text(encoding="utf-8-sig") == text:
        return
    print(f"{'[dry-run] ' if dry_run else ''}update {file}")
    if dry_run:
        return
    file.parent.mkdir(parents=True, exist_ok=True)
    backup(file)
    fd, name = tempfile.mkstemp(prefix=file.name + ".macrohelp-", dir=file.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as output:
            output.write(text)
        os.replace(name, file)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def skill(directory, action, dry_run):
    file = directory / "skills/macrohelp/SKILL.md"
    text = SKILL.read_text(encoding="utf-8")
    if action == "install":
        write(file, text, dry_run)
    elif file.exists():
        if file.read_text(encoding="utf-8") != text:
            print(f"retain modified skill {file}")
        else:
            print(f"{'[dry-run] ' if dry_run else ''}remove {file}")
            if not dry_run:
                file.unlink()


def omp(directory, server, action, dry_run):
    file = directory / "mcp.json"
    config = read_config(file)
    servers = config.setdefault("mcpServers", {})
    if action == "install":
        servers["macrohelp"] = {"type": "stdio", **server, "enabled": True, "timeout": 0}
        if isinstance(config.get("disabledServers"), list):
            config["disabledServers"] = [name for name in config["disabledServers"] if name != "macrohelp"]
    elif "macrohelp" in servers:
        if not isinstance(servers["macrohelp"], dict) or servers["macrohelp"].get("command") != server["command"] or servers["macrohelp"].get("args") != server["args"]:
            raise ValueError("Macrohelp MCP entry was modified; inspect it before removing")
        del servers["macrohelp"]
    else:
        skill(directory, action, dry_run)
        return
    write(file, json.dumps(config, ensure_ascii=False, indent=2) + "\n", dry_run)
    skill(directory, action, dry_run)


def hermes(directory, profile, server, action, dry_run):
    cli = shutil.which("hermes")
    if not cli and not dry_run:
        raise ValueError("Hermes is not on PATH; no configuration was changed")
    args = [cli or "hermes"] + ([] if profile == "default" else ["--profile", profile])
    if action == "install":
        entry = {**server, "enabled": True, "timeout": 30, "connect_timeout": 10,
                 "supports_parallel_tool_calls": False, "tools": {"resources": True, "prompts": False}}
        args += ["config", "set", "--force", "mcp_servers.macrohelp", json.dumps(entry)]
    else:
        args += ["config", "unset", "mcp_servers.macrohelp"]
    print(f"{'[dry-run] ' if dry_run else ''}{action} Hermes MCP ({profile})")
    target = directory if profile == "default" else directory / "profiles" / profile
    if not dry_run:
        saved = backup(target / "config.yaml")
        try:
            subprocess.run(args, check=True, env={**os.environ, "HERMES_HOME": str(directory)})
        except (OSError, subprocess.CalledProcessError):
            if saved:
                print(f"Previous config preserved at {saved}", file=sys.stderr)
            raise
    skill(target, action, dry_run)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", choices=["install", "uninstall"], default="install")
    parser.add_argument("--target", action="append", choices=["omp", "hermes", "all"], required=True)
    parser.add_argument("--profile", default="default", help="one named profile; default is the client's default profile")
    parser.add_argument("--mcp", type=Path, help="existing macrohelp-mcp.exe or macrohelpctl binary")
    parser.add_argument("--env", type=Path, default=ROOT / ".env", help="overlay's env file; keys are never copied into client config")
    parser.add_argument("--dry-run", action="store_true")
    options = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_-]+", options.profile):
        parser.error("Invalid profile name")
    home = Path.home()
    omp_dir = Path(os.environ.get("OMP_HOME", home / ".omp/agent")).expanduser().resolve()
    hermes_dir = Path(os.environ.get("HERMES_HOME", home / ".hermes")).expanduser().resolve()
    if options.profile != "default":
        omp_root = omp_dir.parent if omp_dir.name == "agent" else Path(os.environ.get("OMP_ROOT", home / ".omp"))
        omp_dir = omp_root / "profiles" / options.profile / "agent"
    candidates = ([ROOT / "runtime/source/build-vs2026/bin/Release/macrohelp-mcp.exe"] if os.name == "nt" else [
        Path(os.environ.get("XDG_DATA_HOME", home / ".local/share")) / "macrohelp/overlay/bin/macrohelpctl",
        ROOT / "runtime/source/wld/target/release/macrohelpctl"])
    binary = (options.mcp or next((p for p in candidates if p.is_file()), candidates[0])).expanduser().resolve()
    name = binary.stem.lower()
    if name not in ("macrohelpctl", "macrohelp-mcp"):
        parser.error("--mcp must name macrohelpctl or macrohelp-mcp.exe")
    if options.action == "install" and not binary.is_file():
        parser.error(f"Build the overlay MCP first, or pass --mcp PATH: {binary}")
    server = {"command": str(binary), "args": (["mcp"] if name == "macrohelpctl" else []) + ["--env", str(options.env.expanduser().resolve())]}
    targets = list(dict.fromkeys(["omp", "hermes"] if "all" in options.target else options.target))
    # Parse/check every selected target before the first write.
    if "omp" in targets:
        read_config(omp_dir / "mcp.json")
    if "hermes" in targets and not options.dry_run and not shutil.which("hermes"):
        parser.error("Hermes is not on PATH; no configuration was changed")
    for target in targets:
        if target == "omp":
            omp(omp_dir, server, options.action, options.dry_run)
        else:
            hermes(hermes_dir, options.profile, server, options.action, options.dry_run)
    print("Restart the selected client to reload MCP configuration. No overlay or execution backend was started.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Integration failed: {error}", file=sys.stderr)
        sys.exit(1)
