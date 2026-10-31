#!/usr/bin/env python3
"""Render the Registry Hub field manual as plaintext.

Sources
  --windows  RegistryHubHelpText() in runtime/source/src/main.cpp (wide literals)
  --linux    help() in runtime/source/wld/qml/model/Dialogs.js (single-line JS string)

Modes
  --tofile PATH    write the rendered plaintext to PATH (creates the file)
  --fromfile PATH  read plaintext from PATH and write it back into the source

Examples
  uv run scripts/render-registry-field-manual.py --linux --tofile /tmp/render.txt
  uv run scripts/render-registry-field-manual.py --windows --fromfile /tmp/append.txt
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS_SRC = ROOT / "runtime" / "source" / "src" / "main.cpp"
LINUX_SRC = ROOT / "runtime" / "source" / "wld" / "qml" / "model" / "Dialogs.js"

USAGE = """Usage: render-registry-field-manual.py (--linux | --windows) (--tofile PATH | --fromfile PATH)
  --windows    render RegistryHubHelpText() from src/main.cpp
  --linux      render help() from wld/qml/model/Dialogs.js
  --tofile P   write rendered plaintext to P (creates the file)
  --fromfile P read plaintext from P and write it back into the source"""


class Parser(argparse.ArgumentParser):
    def error(self, message):
        sys.stderr.write(USAGE + "\n")
        sys.stderr.write(f"error: {message}\n")
        sys.exit(2)


def _decode(literal, crlf=False):
    """Decode an escaped string literal body into real text."""
    out = []
    i = 0
    while i < len(literal):
        c = literal[i]
        if c == "\\":
            n = literal[i + 1] if i + 1 < len(literal) else ""
            if n == "r":
                out.append("\r" if crlf else "r")
                i += 2
            elif n == "n":
                out.append("\n")
                i += 2
            elif n == "t":
                out.append("\t")
                i += 2
            elif n in ("\\", '"', "'"):
                out.append(n)
                i += 2
            else:
                out.append(c + n)
                i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def render_windows():
    text = WINDOWS_SRC.read_text(encoding="utf-8")
    block = re.search(
        r"static std::wstring RegistryHubHelpText\(\)\s*\{.*?\};", text, re.S
    )
    if not block:
        sys.exit(f"error: RegistryHubHelpText() not found in {WINDOWS_SRC}")
    body = block.group(0)
    parts = re.findall(r'L"((?:[^"\\]|\\.)*)"', body)
    if not parts:
        sys.exit(f"error: no wide literals in RegistryHubHelpText()")
    return "".join(_decode(p, crlf=True) for p in parts)


def render_linux():
    text = LINUX_SRC.read_text(encoding="utf-8")
    m = re.search(r'function help\(\)\s*\{\s*return\s*"((?:[^"\\]|\\.)*)"', text)
    if not m:
        sys.exit(f"error: help() return literal not found in {LINUX_SRC}")
    return _decode(m.group(1))


def _encode_cpp(text):
    lines = []
    for line in text.split("\n"):
        esc = line.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('        L"' + esc + "\\r\\n\"")
    return "\n".join(lines)


def _encode_js(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def apply_windows(plaintext):
    text = WINDOWS_SRC.read_text(encoding="utf-8")
    block = _encode_cpp(plaintext)
    block = block[:-1] + '";'  # terminate the final literal with ';'
    pat = re.compile(
        r"(static std::wstring RegistryHubHelpText\(\)\s*\{\s*return\s*\n)(.*?)(\n\s*\};)",
        re.S,
    )
    m = pat.search(text)
    if not m:
        sys.exit("error: could not locate RegistryHubHelpText() literal block")
    WINDOWS_SRC.write_text(text[: m.start(2)] + block + text[m.end(2):], encoding="utf-8")


def apply_linux(plaintext):
    text = LINUX_SRC.read_text(encoding="utf-8")
    esc = _encode_js(plaintext)
    pat = re.compile(r'(function help\(\)\s*\{\s*return\s*")((?:[^"\\]|\\.)*)(\");)', re.S)
    m = pat.search(text)
    if not m:
        sys.exit("error: could not locate help() return literal")
    LINUX_SRC.write_text(text[: m.start(2)] + esc + text[m.end(2):], encoding="utf-8")


def main():
    p = Parser(description="Render the Registry Hub field manual as plaintext.")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--linux", action="store_true")
    src.add_argument("--windows", action="store_true")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--tofile", metavar="PATH")
    mode.add_argument("--fromfile", metavar="PATH")
    args = p.parse_args()

    if args.linux:
        render, apply, label = render_linux, apply_linux, "linux (Dialogs.js)"
    else:
        render, apply, label = render_windows, apply_windows, "windows (main.cpp)"

    if args.tofile:
        data = render()
        dest = Path(args.tofile)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(data, encoding="utf-8")
        print(f"wrote {len(data)} chars, {data.count(chr(10))} lines -> {dest}")
    else:
        data = Path(args.fromfile).read_text(encoding="utf-8")
        apply(data)
        print(f"imported {len(data)} chars from {args.fromfile} into {label} source")


if __name__ == "__main__":
    main()
