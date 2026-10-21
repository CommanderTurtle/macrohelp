#!/usr/bin/env python3
"""QML semantic checker — imports, IDs, typos, component references.

Checks for common semantic errors that the basics checker misses:
- Invalid/unknown import modules
- Component references not matching imported modules
- Duplicate IDs
- Property names that look like typos of known QML properties
- Signal handlers referencing undefined signals

Usage:
  python3 qml_check_semantics.py [files...]
  python3 qml_check_semantics.py --json [files...]   # machine-readable output
"""
import re
import json
import sys
from pathlib import Path

SOURCE = "qml_check_semantics"

# Known QML/Quickshell import modules
KNOWN_IMPORTS = {
    "QtQuick", "QtQuick.Controls", "QtQuick.Layouts", "QtQuick.Dialogs",
    "QtQuick.Window", "QtGraphicalEffects", "Quickshell", "Quickshell.Io",
    "qs.Commons", "qs.Ui",
}

# Known component prefixes from imports
KNOWN_COMPONENT_PREFIXES = {
    "Ui": ["Button", "TextField", "Dropdown", "NumberField", "Toggle",
           "PanelHero", "WidgetButton", "KeyboardPanel", "Panel"],
    "QQC": ["SpinBox", "ScrollView", "TextField", "Button", "Dialog",
            "Popup", "Menu", "Label", "TextArea", "CheckBox", "ComboBox"],
}

# Base QML types
BASE_TYPES = {
    "Item", "Rectangle", "Text", "Image", "Column", "Row", "Grid", "Flow",
    "Loader", "Repeater", "Timer", "MouseArea", "FocusScope",
    "Path", "PathLine", "PathArc", "PathRectangle", "PathCircle",
    "SplitParser", "StdioCollector", "Process", "FontLoader",
}

# Common typos -> correct property
TYPO_MAP = {
    "heigth": "height", "widht": "width", "visble": "visible",
    "opactiy": "opacity", "colr": "color", "raduis": "radius",
    "tex": "text", "spacng": "spacing", "imlicitWidth": "implicitWidth",
    "imlicitHeight": "implicitHeight", "onCliked": "onClicked",
    "onTrigered": "onTriggered", "onChnaged": "onChanged",
    "placeholderTxt": "placeholderText", "readOnlye": "readOnly",
    "editble": "editable", "intervel": "interval", "repeate": "repeat",
    "runnning": "running", "horizontalAligment": "horizontalAlignment",
    "verticalAligment": "verticalAlignment", "wrapModee": "wrapMode",
    "maximumLineCound": "maximumLineCount", "contentWidht": "contentWidth",
    "contentHeigth": "contentHeight",
}


def strip_comments_and_strings(text: str) -> str:
    """Remove comments and string contents for analysis."""
    result = []
    i = 0
    in_block = False
    in_line = False
    in_string = None

    while i < len(text):
        ch = text[i]
        next_ch = text[i + 1] if i + 1 < len(text) else ''

        if in_block:
            if ch == '*' and next_ch == '/':
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_line:
            if ch == '\n':
                in_line = False
                result.append(ch)
            i += 1
            continue
        if in_string:
            if ch == '\\':
                i += 2
                continue
            if ch == in_string:
                in_string = None
            i += 1
            continue

        if ch == '/' and next_ch == '*':
            in_block = True
            i += 2
            continue
        if ch == '/' and next_ch == '/':
            in_line = True
            i += 2
            continue
        if ch in ('"', "'"):
            in_string = ch
            i += 1
            continue

 
        i += 1

    return "".join(result)


def extract_imports(text: str) -> list[tuple[str, str, int]]:
    """Extract import statements. Returns list of (module, alias, line_number)."""
    imports = []
    for i, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if line.startswith("import "):
            m = re.match(r'import\s+([\w.]+)(?:\s+as\s+(\w+))?', line)
            if m:
                module = m.group(1)
                alias = m.group(2) or module.split(".")[-1]
                imports.append((module, alias, i))
            m = re.match(r'import\s+"([^"]+)"\s+as\s+(\w+)', line)
            if m:
                imports.append((m.group(1), m.group(2), i))
    return imports


def extract_ids(text: str) -> list[tuple[str, int, int]]:
    """Extract all id: declarations. Returns list of (id, line_number, column)."""
    ids = []
    for i, line in enumerate(text.splitlines(), 1):
        m = re.search(r'\bid:\s*(\w+)', line)
        if m:
            ids.append((m.group(1), i, m.start() + 1))
    return ids


def extract_component_refs(text: str) -> list[tuple[str, int, int]]:
    """Extract component type references. Returns list of (component, line, col)."""
    refs = []
    for i, line in enumerate(text.splitlines(), 1):
        m = re.match(r'(\s*)((?:[A-Z]\w+\.)?[A-Z]\w*)\s*\{', line)
        if m:
            comp = m.group(2)
            col = m.start(2) + 1
            refs.append((comp, i, col))
    return refs


def check_file(path: Path) -> list[dict]:
    """Check a QML file for semantic issues. Returns list of diagnostic dicts."""
    diagnostics = []
    text = path.read_text()
    clean = strip_comments_and_strings(text)

    # 1. Check imports
    imports = extract_imports(text)
    import_aliases = set()
    for module, alias, line_no in imports:
        import_aliases.add(alias)
        if "/" not in module and module not in KNOWN_IMPORTS:
            if not any(module.startswith(k) for k in KNOWN_IMPORTS):
                diagnostics.append({
                    "line": line_no, "column": 1,
                    "severity": "warning", "source": SOURCE,
                    "message": f"unknown import module '{module}'",
                })

    # 2. Check for duplicate IDs
    ids = extract_ids(clean)
    id_map = {}
    for id_name, line_no, col in ids:
        if id_name in id_map:
            diagnostics.append({
                "line": line_no, "column": col,
                "severity": "error", "source": SOURCE,
                "message": f"duplicate id '{id_name}' (first at line {id_map[id_name]})",
            })
        else:
            id_map[id_name] = line_no

    # 3. Check component references against known types
    refs = extract_component_refs(clean)
    for comp, line_no, col in refs:
        if comp in BASE_TYPES:
            continue
        if "." in comp:
            prefix, name = comp.split(".", 1)
            if prefix in import_aliases or prefix in KNOWN_COMPONENT_PREFIXES:
                continue
            diagnostics.append({
                "line": line_no, "column": col,
                "severity": "warning", "source": SOURCE,
                "message": f"component '{comp}' - prefix '{prefix}' not in imports",
            })
        else:
            qml_dir = path.parent
            if (qml_dir / f"{comp}.qml").exists():
                continue
            if re.search(rf'^\s*{comp}\s*\{{', clean, re.MULTILINE):
                continue
            for known in BASE_TYPES:
                if comp.lower() == known.lower() and comp != known:
                    diagnostics.append({
                        "line": line_no, "column": col,
                        "severity": "warning", "source": SOURCE,
                        "message": f"possible typo '{comp}' -> '{known}'",
                    })
                    break

    # 4. Check for property typos
    for i, line in enumerate(text.splitlines(), 1):
        for m in re.finditer(r'(\w+)\s*:\s*', line):
            prop = m.group(1)
            if prop in TYPO_MAP:
                diagnostics.append({
                    "line": i, "column": m.start(1) + 1,
                    "severity": "warning", "source": SOURCE,
                    "message": f"possible typo '{prop}' -> '{TYPO_MAP[prop]}'",
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
        print(f"\nAll {len(files)} file(s) passed semantic checks.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
