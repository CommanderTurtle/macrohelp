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
    from qml_lex import mask
    return list(enumerate(mask(text).splitlines(), 1))


def check_file(path: Path) -> list[dict]:
    """Check a QML file. Returns list of diagnostic dicts."""
    diagnostics = []
    text = path.read_text(encoding="utf-8")
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
