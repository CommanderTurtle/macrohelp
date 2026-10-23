import copy
import json
import re
import sys
import unittest

from workflows import ENTITIES, RECIPES, TEXT_RECIPES, compile_tool, key_script, validate_bindings
import server


class TranslationTests(unittest.TestCase):
    def test_sequential_case_and_chords(self):
        self.assertEqual(key_script("ggG<Ctrl-i><Ctrl-D>"),
                         "{press G}{press G}{press SHIFT_LEFT G}{press CONTROL_LEFT I}{press CONTROL_LEFT SHIFT_LEFT D}")
        self.assertEqual(key_script("<Ctrl-Alt-Meta-Shift-F7>"),
                         "{press CONTROL_LEFT ALT_LEFT WINDOWS SHIFT_LEFT F7}")

    def test_angle_brackets_and_punctuation_cannot_change_registry_grammar(self):
        self.assertEqual(key_script("<<>>{}"),
                         "{press &lt;}{press &lt;}{press &gt;}{press &gt;}{press &lbrace;}{press &rbrace;}")
        self.assertEqual(key_script("<Ctrl->><Ctrl--><lt><gt>"),
                         "{press CONTROL_LEFT &gt;}{press CONTROL_LEFT -}{press &lt;}{press &gt;}")
        for char, entity in ENTITIES.items():
            with self.subTest(char=char):
                self.assertEqual(key_script(char), "{press &" + entity + ";}")
        self.assertEqual(key_script(" \n\t"), "{press SPACEBAR}{press ENTER}{press TAB}")

    def test_invalid_notation_has_no_permissive_registry_fallback(self):
        for keys in (None, 7, "", "<Ctrl-", "<NotAKey>", "é", "\0", "\r"):
            with self.subTest(keys=keys), self.assertRaises(ValueError):
                key_script(keys)

    def test_arbitrary_text_is_outside_script_and_never_expanded(self):
        text = "a,b\n{unbalanced\n{screen}{type $Z}\n$REGISTRY &amp; \\n 😅"
        value = compile_tool("localsurf_type", {"text": text, "before": "<Ctrl-a>", "after": "<Tab>"})
        self.assertEqual(value["config"]["buffers"], {"Z": text})
        self.assertEqual(value["config"]["script"], "{press CONTROL_LEFT A}{paste Z}{press TAB}")
        self.assertTrue(value["no_replace"])
        self.assertEqual(value["inputs"], [{"action": "play"}])
        self.assertEqual(compile_tool("localsurf_type", {"text": ""})["config"]["buffers"]["Z"], "")

    def test_validations_reject_null_data_and_unrecognized_fields(self):
        for name, args in [
            ("localsurf_type", {"text": None}), ("localsurf_type", {"text": "\0"}),
            ("localsurf_type", {"text": "ok", "before": 0}),
            ("localsurf_keys", {"keys": "j", "no_replace": False}),
            ("localsurf_keys", {"keys": "j", "preview": "true"}),
            ("localsurf_workflow", {"name": "find", "text": None}),
            ("localsurf_workflow", {"name": "find", "text": "ok", "settle_ms": -1}),
            ("localsurf_workflow", {"name": "find", "text": "ok", "settle_ms": True}),
            ("localsurf_workflow", {"name": "help", "text": "not used"}),
        ]:
            with self.subTest(name=name, args=args), self.assertRaises((ValueError, TypeError)):
                compile_tool(name, args)

    def test_every_named_recipe_translates_and_uses_temporary_native_play(self):
        for name in RECIPES:
            args = {"name": name}
            if name in TEXT_RECIPES:
                args["text"] = "some query"
            if RECIPES[name][0] is None and name != "scrsht":
                args["keys"] = "af" if name == "follow_hint" else "<Meta-Shift-Alt-b>"
            with self.subTest(name=name):
                value = compile_tool("localsurf_workflow", args)
                self.assertEqual(set(value), {"menu", "inputs", "config", "no_replace"})
                self.assertEqual(value["menu"], 2)
                self.assertTrue(value["no_replace"])
                self.assertTrue(value["config"]["script"])

    def test_no_implicit_escape_no_repeat_cap_and_no_click(self):
        value = compile_tool("localsurf_keys", {"keys": "100j"})
        self.assertEqual(value["config"]["script"], "{press 1}{press 0}{press 0}{press J}")
        self.assertNotIn("ESCAPE", value["config"]["script"])
        self.assertNotIn("click", value["config"]["script"])

    def test_passthrough_is_explicit_and_cross_window_steps_can_be_separate(self):
        value = compile_tool("localsurf_workflow", {"name": "pass_keys", "keys": "<Ctrl-a>"})
        self.assertEqual(value["config"]["script"], "{press ALT_LEFT I}{press CONTROL_LEFT A}{press ESCAPE}")
        enter = compile_tool("localsurf_workflow", {"name": "pass_enter"})
        self.assertEqual(enter["config"]["script"], "{press ALT_LEFT I}")
        self.assertNotIn("ESCAPE", enter["config"]["script"])

    def test_capture_is_existing_pane_only(self):
        value = compile_tool("localsurf_workflow", {"name": "scrsht"})
        self.assertEqual(value["config"], {"script": "{screen}"})
        with self.assertRaises(ValueError):
            compile_tool("localsurf_workflow", {"name": "scrsht", "text": "ignored?"})
        with self.assertRaises(ValueError):
            validate_bindings({"scrsht": "yg"})

    def test_native_size_limit_applies_to_expanded_request_not_only_input(self):
        with self.assertRaisesRegex(ValueError, "native 1 MiB"):
            compile_tool("localsurf_keys", {"keys": "g" * 120000})

    def test_passthrough_wrapper_uses_configured_mode_bindings(self):
        value = compile_tool("localsurf_workflow", {"name": "pass_keys", "keys": "j"},
                             {"pass_enter": "<Alt-j>", "pass_exit": "<Ctrl-[>"})
        self.assertEqual(value["config"]["script"], "{press ALT_LEFT J}{press J}{press CONTROL_LEFT &lbrack;}")

    def test_live_bindings_override_defaults_without_changing_user_config(self):
        original = {"line_start": "<Ctrl-q>", "line_end": "<Ctrl-e>"}
        value = compile_tool("localsurf_workflow", {"name": "line_start"}, original)
        self.assertEqual(value["config"]["script"], "{press CONTROL_LEFT Q}")
        self.assertEqual(original, {"line_start": "<Ctrl-q>", "line_end": "<Ctrl-e>"})
        self.assertNotIn("bindings", value["config"])
        value = compile_tool("localsurf_workflow", {"name": "line_start", "keys": "<Home>"}, original)
        self.assertEqual(value["config"]["script"], "{press HOME}")

    def test_native_private_browser_binding_is_not_guessed(self):
        with self.assertRaisesRegex(ValueError, "native launch binding"):
            compile_tool("localsurf_workflow", {"name": "private_browser"})
        value = compile_tool("localsurf_workflow", {"name": "private_browser"},
                             {"private_browser": "<Meta-Shift-Alt-b>"})
        self.assertEqual(value["config"]["script"], "{press WINDOWS SHIFT_LEFT ALT_LEFT B}")


class FakePeer:
    def __init__(self):
        self.calls = []
        self.native_result = {"content": [{"type": "text", "text": "native"}],
                              "structuredContent": {"run": "same-id"}, "isError": False}

    def call(self, msg):
        self.calls.append(copy.deepcopy(msg))
        if "id" not in msg:
            return None
        method = msg["method"]
        value = {"instructions": "native rules", "serverInfo": {"name": "macrohelp"}} if method == "initialize" else (
            {"tools": [{"name": "macrohelp_control", "inputSchema": {"native": True}}]} if method == "tools/list" else (
                {"resources": [{"uri": "macrohelp://guide"}]} if method == "resources/list" else copy.deepcopy(self.native_result)))
        return {"jsonrpc": "2.0", "id": msg["id"], "result": value}


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.peer = FakePeer()
        self.session = server.Session(self.peer, {"line_start": "<Ctrl-q>"})
        self.session.dispatch({"jsonrpc": "2.0", "id": 1, "method": "initialize"})

    def call(self, name, args, id=2):
        return self.session.dispatch({"jsonrpc": "2.0", "id": id, "method": "tools/call",
                                      "params": {"name": name, "arguments": args}})

    def test_catalogs_inherit_native_items_unchanged(self):
        reply = self.session.dispatch({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        self.assertEqual(reply["result"]["tools"][0], {"name": "macrohelp_control", "inputSchema": {"native": True}})
        self.assertEqual(len(reply["result"]["tools"]), 5)
        reply = self.session.dispatch({"jsonrpc": "2.0", "id": 3, "method": "resources/list"})
        self.assertEqual(reply["result"]["resources"][0], {"uri": "macrohelp://guide"})

    def test_translated_actions_forward_exact_contract_and_result(self):
        reply = self.call("localsurf_workflow", {"name": "line_start"}, id="client-id")
        self.assertEqual(reply["id"], "client-id")
        self.assertEqual(reply["result"], self.peer.native_result)
        self.assertEqual(self.peer.calls[-1]["params"], {"name": "macrohelp_control", "arguments": {
            "menu": 2, "inputs": [{"action": "play"}], "config": {"script": "{press CONTROL_LEFT Q}"}, "no_replace": True}})

    def test_preview_and_invalid_calls_never_reach_executor(self):
        count = len(self.peer.calls)
        reply = self.call("localsurf_keys", {"keys": "gg", "preview": True})
        self.assertFalse(reply["result"]["structuredContent"]["executed"])
        for args in ({"keys": "<Nope>"}, {"keys": None}, ["j"]):
            self.assertTrue(self.call("localsurf_keys", args)["result"]["isError"])
        self.assertEqual(len(self.peer.calls), count)

    def test_native_error_and_raw_controls_are_not_replayed_or_reinterpreted(self):
        self.peer.native_result = {"isError": True, "content": [{"type": "text", "text": "409 native busy"}]}
        count = len(self.peer.calls)
        reply = self.call("localsurf_keys", {"keys": "f"})
        self.assertEqual(reply["result"], self.peer.native_result)
        self.assertEqual(len(self.peer.calls), count + 1)
        raw = {"run": "owned", "inputs": [{"action": "cancel"}]}
        self.call("macrohelp_control", raw)
        self.assertEqual(self.peer.calls[-1]["params"]["arguments"], raw)

    def test_invalid_rpc_cannot_desynchronize_child(self):
        count = len(self.peer.calls)
        for request in ([], {"method": "ping"}, {"jsonrpc": "2.0", "method": "ping", "id": []}):
            self.assertEqual(self.session.dispatch(request)["error"]["code"], -32600)
        self.assertEqual(len(self.peer.calls), count)
        with self.assertRaises(ValueError):
            server.parse_json('{"value":NaN}')

    def test_notification_and_early_calls_keep_native_lifecycle(self):
        self.assertIsNone(self.session.dispatch({"jsonrpc": "2.0", "method": "notifications/initialized"}))
        self.assertNotIn("id", self.peer.calls[-1])
        early = server.Session(self.peer)
        early.dispatch({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
                        "params": {"name": "localsurf_keys", "arguments": {"keys": "j"}}})
        self.assertEqual(self.peer.calls[-1]["params"]["name"], "localsurf_keys")

    def test_resources_are_local_and_complete_and_lookup_does_not_read_browser(self):
        count = len(self.peer.calls)
        for item in server.resources():
            reply = self.session.dispatch({"jsonrpc": "2.0", "id": 3, "method": "resources/read", "params": {"uri": item["uri"]}})
            self.assertTrue(reply["result"]["contents"][0]["text"])
        reply = self.call("localsurf_reference", {"topic": "workflows", "query": "pass_keys"})
        self.assertIn("pass_keys", reply["result"]["structuredContent"]["text"])
        self.assertEqual(len(self.peer.calls), count)

    def test_bundled_skill_has_valid_plain_frontmatter_and_resolvable_local_references(self):
        # This skill has two plain scalar fields; no YAML dependency is needed
        # to validate this deliberately small subset plus its actual links.
        guide = server.REFERENCES["guide"].read_text(encoding="utf-8")
        match = re.match(r"\A---\nname: ([a-z0-9]+(?:-[a-z0-9]+)*)\ndescription: ([^\r\n]+)\n---\n", guide)
        self.assertIsNotNone(match)
        self.assertLessEqual(len(match[1]), 64)
        self.assertLessEqual(len(match[2]), 1024)
        self.assertNotRegex(match[2], r"[<>]")
        for file in server.REFERENCES.values():
            text = file.read_text(encoding="utf-8")
            for link in re.findall(r"\]\(([^)]+)\)", text):
                if not re.match(r"\w+://|#", link):
                    self.assertTrue((file.parent / link).is_file(), link)

    def test_stdio_peer_preserves_utf8_ids_and_notifications(self):
        # A real child pipe, not a browser, overlay, server or desktop test.
        code = "import sys,json\nfor line in sys.stdin.buffer:\n m=json.loads(line)\n if 'id' in m: print(json.dumps({'jsonrpc':'2.0','id':m['id'],'result':m.get('params',{})}),flush=True)"
        peer = server.Peer([sys.executable, "-u", "-c", code])
        try:
            self.assertIsNone(peer.call({"jsonrpc": "2.0", "method": "notifications/initialized"}))
            value = {"jsonrpc": "2.0", "id": "a,b", "method": "echo", "params": {"text": "x\n😅"}}
            self.assertEqual(peer.call(value)["result"], value["params"])
        finally:
            peer.close()

    def test_bad_child_reply_never_claims_that_nothing_executed(self):
        code = "import sys\nsys.stdin.buffer.readline()\nprint('invalid-json',flush=True)"
        peer = server.Peer([sys.executable, "-u", "-c", code])
        try:
            with self.assertRaisesRegex(OSError, "execution status is unknown"):
                peer.call({"jsonrpc": "2.0", "id": 2, "method": "tools/call"})
        finally:
            peer.close()


if __name__ == "__main__":
    unittest.main()
