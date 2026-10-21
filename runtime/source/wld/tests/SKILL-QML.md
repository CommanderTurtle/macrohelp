 Two checkers in runtime/source/wld/tests/:

 qml_check_basics.py — delimiter balance:
 - Unbalanced {} () []
 - Mismatched bracket types
 - Files not starting with import

 qml_check_semantics.py — semantic issues:
 - Unknown import modules
 - Duplicate IDs
 - Property typos (heigth→height, visble→visible, etc.)
 - Component prefix not in imports

 Both run on all .qml files by default, or take explicit paths. Run before restarting the shell:

 ```sh
python3 tests/qml_check_basics.py && python3 tests/qml_check_semantics.py
 ```

---

Both checkers also emit json diagnostic format:

 ```json
{
  "line": 3,
  "column": 16,
  "severity": "error",
  "source": "qml_check_basics",
  "message": "')' closes '{' from line 2 (mismatch)",
  "file": "/path/to/file.qml"
}
 ```

 Usage:

 ```sh
# Human-readable (default)
python3 tests/qml_check_basics.py
python3 tests/qml_check_semantics.py

# Machine-readable (pipe to jq, CI, etc.)
python3 tests/qml_check_basics.py --json
python3 tests/qml_check_semantics.py --json
