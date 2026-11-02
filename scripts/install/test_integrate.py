import importlib.util
import json
import os
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("integrate", Path(__file__).with_name("integrate.py"))
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)


class IntegrationTests(unittest.TestCase):
    def test_localsurf_is_separate_and_copies_references_without_overwriting_macrohelp(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            file = root / "mcp.json"
            original = {"command": "existing-macrohelp", "args": []}
            file.write_text(json.dumps({"mcpServers": {"macrohelp": original}}))
            server = {"command": sys.executable, "args": ["server.py", "--mcp", "macrohelpctl"]}
            integration.omp(root, server, "install", False, "localsurf")
            self.assertEqual(json.loads(file.read_text())["mcpServers"]["macrohelp"], original)
            self.assertTrue((root / "skills/localsurf/references/modes.md").is_file())
            guide = root / "skills/localsurf/SKILL.md"
            guide.write_text("user modified")
            integration.omp(root, server, "uninstall", False, "localsurf")
            self.assertEqual(guide.read_text(), "user modified")
            self.assertFalse((root / "skills/localsurf/references/modes.md").exists())
            self.assertEqual(json.loads(file.read_text())["mcpServers"], {"macrohelp": original})

    def test_localsurf_cli_preserves_paths_and_validates_bindings_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binary = root / "path with spaces/macrohelpctl"
            binary.parent.mkdir()
            binary.write_bytes(b"not executed")
            bindings = root / "bindings.json"
            bindings.write_text(json.dumps({"line_start": "<Ctrl-q>"}))
            with patch.dict(os.environ, {"OMP_HOME": str(root / "agent")}):
                argv = ["--target", "omp", "--server", "localsurf", "--mcp", str(binary), "--bindings", str(bindings)]
                integration.main(argv)
                entry = json.loads((root / "agent/mcp.json").read_text())["mcpServers"]["localsurf"]
                self.assertEqual(entry["command"], sys.executable)
                self.assertEqual(entry["args"][-2:], ["--bindings", str(bindings.resolve())])
                self.assertEqual(entry["args"][1:3], ["--mcp", str(binary.resolve())])
                before = (root / "agent/mcp.json").read_bytes()
                bindings.write_text('{"unknown": "j"}')
                with self.assertRaises(SystemExit):
                    integration.main(argv)
                self.assertEqual((root / "agent/mcp.json").read_bytes(), before)

    def test_omp_preserves_other_servers_is_repeatable_and_uninstalls(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            file = root / "mcp.json"
            file.write_text(json.dumps({"mcpServers": {"existing": {"command": "keep"}}, "disabledServers": ["macrohelp", "other"]}))
            server = {"command": "path with spaces/macrohelpctl", "args": ["mcp", "--env", "private.env"]}
            integration.omp(root, server, "install", False)
            value = json.loads(file.read_text())
            self.assertEqual(value["mcpServers"]["existing"], {"command": "keep"})
            self.assertEqual(value["disabledServers"], ["other"])
            self.assertEqual(value["mcpServers"]["macrohelp"]["args"], server["args"])
            self.assertEqual((root / "skills/macrohelp/SKILL.md").read_text(), integration.SKILL.read_text())
            before = {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}
            integration.omp(root, server, "install", False)
            self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()})
            integration.omp(root, server, "uninstall", False)
            self.assertEqual(json.loads(file.read_text())["mcpServers"], {"existing": {"command": "keep"}})
            self.assertFalse((root / "skills/macrohelp/SKILL.md").exists())

    def test_dry_run_and_invalid_json_never_write(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            integration.omp(root / "absent", {"command": "unused", "args": []}, "install", True)
            self.assertEqual(list(root.iterdir()), [])
            (root / "mcp.json").write_text("{broken")
            with self.assertRaises(ValueError):
                integration.omp(root, {}, "install", False)
            self.assertEqual((root / "mcp.json").read_text(), "{broken")

    def test_modified_entries_and_skills_are_retained_on_uninstall(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            server = {"command": "original", "args": []}
            integration.omp(root, server, "install", False)
            file = root / "mcp.json"
            value = json.loads(file.read_text())
            value["mcpServers"]["macrohelp"]["command"] = "custom"
            file.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError, "modified"):
                integration.omp(root, server, "uninstall", False)
            guide = root / "skills/macrohelp/SKILL.md"
            guide.write_text("custom guide")
            integration.skill(root, "uninstall", False)
            self.assertEqual(guide.read_text(), "custom guide")

    def test_hermes_uses_native_config_and_does_not_enable_parallel_mutations(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(integration.shutil, "which", return_value="hermes"), patch.object(integration.subprocess, "run") as run:
            root = Path(folder)
            server = {"command": "macrohelpctl", "args": ["mcp", "--env", "private.env"]}
            integration.hermes(root, "test", server, "install", False)
            args = run.call_args.args[0]
            self.assertEqual(args[:3], ["hermes", "--profile", "test"])
            self.assertEqual(args[3:7], ["config", "set", "--force", "mcp_servers.macrohelp"])
            self.assertFalse(json.loads(args[7])["supports_parallel_tool_calls"])
            self.assertEqual(run.call_args.kwargs["env"]["HERMES_HOME"], str(root))
            integration.hermes(root, "test", server, "uninstall", False)
            self.assertEqual(run.call_args.args[0][-3:], ["config", "unset", "mcp_servers.macrohelp"])

    def test_cli_selects_only_the_requested_profile_and_preserves_argument_boundaries(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binary = root / "path with spaces/macrohelpctl"
            binary.parent.mkdir()
            binary.write_bytes(b"fixture")
            with patch.dict(os.environ, {"OMP_HOME": str(root / ".omp/agent")}):
                integration.main(["--target", "omp", "--profile", "chosen", "--mcp", str(binary), "--env", str(root / "private file.env")])
            self.assertFalse((root / ".omp/agent").exists())
            file = root / ".omp/profiles/chosen/agent/mcp.json"
            entry = json.loads(file.read_text())["mcpServers"]["macrohelp"]
            self.assertEqual(entry["command"], str(binary.resolve()))
            self.assertEqual(entry["args"], ["mcp", "--env", str((root / "private file.env").resolve())])
            self.assertNotIn("env", entry)


if __name__ == "__main__":
    unittest.main()
