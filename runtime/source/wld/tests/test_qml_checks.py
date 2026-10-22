import tempfile
import unittest
from pathlib import Path
from qml_check_basics import check_file as basics
from qml_check_semantics import check_file as semantics
from qml_lex import mask


class Checks(unittest.TestCase):
    def check(self, text, checker):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "Check.qml"
            path.write_text(text, encoding="utf-8")
            return checker(path)

    def test_literals_preserve_offsets_and_hide_delimiters(self):
        source = 'Item {\n text:"} // ["; property var r: /[{}]\\//; /* ( */ x: 1/2\n}'
        self.assertEqual(len(mask(source)), len(source))
        self.assertEqual(mask(source).count("\n"), 2)
        self.assertEqual(self.check(source, basics), [])

    def test_delimiter_error_is_not_silently_erased(self):
        self.assertTrue(self.check('import QtQuick\nItem { width: (1+2 }', basics))

    def test_semantic_checks_inspect_code_and_ignore_literals(self):
        issues = self.check('import QtQuick\nItem { id: a; id: a; heigth: 2; text:"id: a" }', semantics)
        self.assertTrue(any("duplicate id" in issue["message"] for issue in issues))
        self.assertTrue(any("heigth" in issue["message"] for issue in issues))


if __name__ == "__main__":
    unittest.main()
