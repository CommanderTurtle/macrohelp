#!/usr/bin/env python3
"""QML basics checker — delimiter balance and structure.

Catches unbalanced braces/parens/brackets and common structural issues
that kill the entire overlay.

Usage:
  python3 qml_check_basics.py [files...]
  python3 qml_check_basics.py --json [files...]   # machine-readable output
"""
import json
import sys
from pathlib import Path

SOURCE = "qml_check_basics"


def strip_qml(text: str) -> list[tuple[int, str]]:
    """Return list of (line_number, stripped_line) with comments/strings removed."""
    lines = []
    in_block_comment = False
    for i, raw in enumerate(text.splitlines(), 1):
        line = raw
        if in_block_comment:
            end = line.find("*/")
            if end == -1:
                continue
            line = line[end + 2:]
            in_block_comment = False
        while "/*" in line:
            start = line.find("/*")
            end = line.find("*/", start + 2)
            if end == -1:
                line = line[:start]
                in_block_comment = True
                break
            line = line[:start] + line[end + 2:]
        in_string = None
        j = 0
        while j < len(line):
            ch = line[j]
            if in_string:
                if ch == "\\":
                    j += 2
                    continue
                if ch == in_string:
                    in_string = None
            else:
                if ch in ('"', "'"):
                    in_string = ch
                elif ch == "/" and j + 1 < len(line) and line[j + 1] == "/":
                    line = line[:j]
                    break
            j += 1
        in_string = None
        result = []
        for ch in line:
            if in_string:
                if ch == in_string:
                    in_string = None
                    result.append(ch)
            else:
                if ch in ('"', "'"):
                    in_string = ch
                    result.append(ch)
                else:
                    result.append(ch)
        lines.append((i, "".join(result)))
    return lines


def check_file(path: Path) -> list[dict]:
    """Check a QML file. Returns list of diagnostic dicts."""
    diagnostics = []
    text = path.read_text()
    lines = strip_qml(text)

    stack = []  # (char, line_number, column)
    pairs = {")": "(", "]": "[", "}": "{"}

    for lineno, line in lines:
        for col, ch in enumerate(line, 1):
            if ch in "({[":
                stack.append((ch, lineno, col))
            elif ch in ")}]":
                if not stack:
                    diagnostics.append({
                        "line": lineno, "column": col,
                        "severity": "error", "source": SOURCE,
                        "message": f"unexpected '{ch}' (nothing to close)",
                    })
                else:
                    top_char, top_line, top_col = stack.pop()
                    if top_char != pairs[ch]:
                        diagnostics.append({
                            "line": lineno, "column": col,
                            "severity": "error", "source": SOURCE,
                            "message": f"'{ch}' closes '{top_char}' from line {top_line} (mismatch)",
                        })

    for ch, lineno, col in stack:
        diagnostics.append({
            "line": lineno, "column": col,
            "severity": "error", "source": SOURCE,
            "message": f"unclosed '{ch}'",
        })

    if text.strip():
        stripped = text.strip()
        if not (stripped.startswith("import ") or stripped[0].isalpha() or stripped[0] == "/"):
            diagnostics.append({
                "line": 1, "column": 1,
                "severity": "warning", "source": SOURCE,
                "message": "file doesn't start with import or component name",
            })

    return diagnostics


def main():
    args = sys.argv[1:]
    json_output = False
    if "--json" in args:
        json_output = True
        args = [a for a in args if a != "--json"]

    if args:
        files = [Path(f) for f in args]
    else:
        qml_dir = Path(__file__).parent.parent / "qml"
        files = sorted(qml_dir.glob("*.qml"))

    if not files:
        print("No QML files found.")
        return 0

    all_diags = []
    for f in files:
        if not f.exists():
            if not json_output:
                print(f"  SKIP {f} (not found)")
            continue
        diags = check_file(f)
        for d in diags:
            d["file"] = str(f)
        all_diags.extend(diags)
        if not json_output:
            if diags:
                print(f"  FAIL {f.name}")
                for d in diags:
                    print(f"    {f.name}:{d['line']}:{d['column']}: {d['severity']}: {d['message']}")
            else:
                print(f"  OK   {f.name}")

    if json_output:
        print(json.dumps(all_diags, indent=2))
    else:
        errors = [d for d in all_diags if d["severity"] == "error"]
        if errors:
            print(f"\n{len(errors)} error(s), {len(all_diags) - len(errors)} warning(s) found.")
            return 1
        print(f"\nAll {len(files)} file(s) passed.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
