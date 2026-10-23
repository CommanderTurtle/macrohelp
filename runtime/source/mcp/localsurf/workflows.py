"""Surfingkeys intentions -> existing Macrohelp requests. No desktop APIs."""
import json
import re

UPSTREAM = "662b8c6faff942d7dafad2658a2682ab65d866a0"

# The native press parser decodes these *after* Registry Hub brace parsing.
ENTITIES = dict(zip(
    "&<>\"'{}[]/\\|;:!#$%()*+,.?@`",
    ("amp lt gt quot apos lbrace rbrace lbrack rbrack sol bsol vert semi colon "
     "excl num dollar percnt lpar rpar ast plus comma period quest commat grave").split(),
))
SPECIAL = {
    "esc": "ESCAPE", "escape": "ESCAPE", "enter": "ENTER", "cr": "ENTER",
    "return": "ENTER", "tab": "TAB", "space": "SPACEBAR", "bs": "BACKSPACE",
    "backspace": "BACKSPACE", "delete": "DELETE", "del": "DELETE",
    "insert": "INSERT", "home": "HOME", "end": "END",
    "arrowup": "UP_ARROW", "up": "UP_ARROW", "arrowdown": "DOWN_ARROW",
    "down": "DOWN_ARROW", "arrowleft": "LEFT_ARROW", "left": "LEFT_ARROW",
    "arrowright": "RIGHT_ARROW", "right": "RIGHT_ARROW", "pageup": "PAGE_UP",
    "pagedown": "PAGE_DOWN", "printscreen": "PRINT_SCREEN",
    "capslock": "CAPS_LOCK", "numlock": "NUM_LOCK", "scrolllock": "SCROLL_LOCK",
    "pause": "PAUSE", "lt": "&lt;", "gt": "&gt;",
    "shift": "SHIFT_LEFT", "ctrl": "CONTROL_LEFT", "alt": "ALT_LEFT",
    "meta": "WINDOWS", "super": "WINDOWS", "win": "WINDOWS",
}
MODIFIERS = {"ctrl": "CONTROL_LEFT", "control": "CONTROL_LEFT", "c": "CONTROL_LEFT",
             "alt": "ALT_LEFT", "a": "ALT_LEFT", "meta": "WINDOWS", "m": "WINDOWS",
             "super": "WINDOWS", "win": "WINDOWS", "shift": "SHIFT_LEFT", "s": "SHIFT_LEFT"}

# Keys are defaults, not claims about the user's current mappings. None means a
# native launch recipe must be supplied; do not invent an OS browser controller.
RECIPES = {
    "help": ("?", "Normal -> live help; scroll or / search inside the help pane."),
    "hints": ("f", "Normal -> link hints. Observe current labels before following."),
    "follow_hint": (None, "Current Hints -> chosen label. Supply keys from a fresh capture."),
    "active_hints": ("af", "Normal -> hints opening an active new tab."),
    "background_hints": ("gf", "Normal -> hints opening a background tab."),
    "multi_hints": ("cf", "Normal -> repeated link hints. Labels may be rebuilt."),
    "input_hints": ("i", "Normal -> editable-element hints."),
    "first_input": ("gi", "Normal -> first editable; Tab cycles the input masks."),
    "editor_hints": ("I", "Normal -> editable hints -> ACE editor."),
    "editor_current": ("<Ctrl-i>", "Page Insert -> editor for the focused field."),
    "neovim_current": ("<Ctrl-Alt-i>", "Page Insert -> existing optional native Neovim."),
    "visual": ("v", "Normal -> text hints; choose one to enter Caret."),
    "visual_range": ("v", "Caret -> Range; Range -> Normal. Check the current indicator."),
    "visual_restore": ("V", "Normal -> restore visual cursor."),
    "visual_element": ("zv", "Normal -> text hint -> select the whole text element."),
    "visual_word": ("Vw", "Visual -> select word."),
    "visual_line": ("Vl", "Visual -> select line."),
    "visual_sentence": ("Vs", "Visual -> select sentence (browser support varies)."),
    "visual_paragraph": ("Vp", "Visual -> select paragraph (browser support varies)."),
    "regional": ("L", "Normal -> large-element hints -> generated ct/ch/d/l/p menu."),
    "find_bar": ("/", "Normal/help -> native find bar."),
    "find_next": ("n", "Find next occurrence; may enter Caret."),
    "find_previous": ("N", "Find previous occurrence; may enter Caret."),
    "tabs": ("T", "Normal -> tab labels or omnibar, depending on the live configuration."),
    "bookmarks": ("b", "Normal -> bookmark omnibar."),
    "history": ("oh", "Normal -> history omnibar."),
    "recently_closed": ("ox", "Normal -> recently closed omnibar."),
    "marks": ("om", "Normal -> mark omnibar."),
    "commands": (":", "Normal -> registered command pane, not a shell or JS evaluator."),
    "search_bar": ("og", "Normal -> Google search alias; override for the configured engine."),
    "switch_frame": ("w", "Normal -> iframe hints / next frame."),
    "top_frame": (";w", "Focus the top content window."),
    "scroll_target": ("cs", "Cycle and highlight the active scrollable element."),
    "scroll_hints": (";fs", "Hint scrollable elements for focus."),
    "back": ("S", "Page history back."), "forward": ("D", "Page history forward."),
    "reload": ("r", "Reload the page."), "new_tab": ("on", "Open the browser's new tab."),
    "close_tab": ("x", "Close the current tab."), "restore_tab": ("X", "Restore closed tab."),
    "pass_enter": ("<Alt-i>", "Normal -> indefinite passthrough. Not a toggle."),
    "pass_exit": ("<Esc>", "Passthrough -> Normal, after browser focus has returned."),
    "pass_keys": (None, "Normal -> passthrough -> supplied keys -> Escape. Keep browser focus."),
    "line_start": ("<Home>", "Focused input -> line start; user binding may override this."),
    "line_end": ("<Ctrl-e>", "Page Insert -> line end."),
    "editor_save": (":w<Enter>", "ACE Vim Normal -> write without closing."),
    "editor_save_close": (":wq<Enter>", "ACE Vim Normal -> write and close."),
    "editor_discard": (":q<Enter>", "ACE Vim Normal -> discard and close."),
    "editor_emacs_save": ("<Ctrl-x><Ctrl-s>", "ACE Emacs -> save and close."),
    "private_browser": (None, "Launch using an existing native private-browser binding; reuse first."),
    "llm_chat": ("A", "Normal/Visual -> Surfingkeys' optional, already-configured LLM pane."),
    "url_editor": (";U", "Normal -> edit current URL in Vim."),
    "markdown": (";pm", "Normal -> Markdown preview from clipboard."),
    "markdown_editor": ("sm", "Markdown preview -> edit its source."),
    "find": ("/", "Open find, paste the native search pattern, Enter -> Caret."),
    "navigate": ("go", "Open URLs omnibar, paste URL, Enter -> current tab."),
    "navigate_new": ("t", "Open URLs omnibar, paste URL, Enter -> new tab."),
    "command": (":", "Open Commands, paste a registered command, Enter. Pane may remain open."),
    "search": ("og", "Open configured search alias, paste query, Enter."),
    "scrsht": (None, "Use Macrohelp's existing {screen} pane. Locate/view the saved file yourself."),
}
TEXT_RECIPES = {"find", "navigate", "navigate_new", "command", "search"}


def key_script(notation):
    """Sequential keystrokes, preserving case/chords, never pasted characters."""
    if not isinstance(notation, str) or not notation:
        raise ValueError("keys must be a non-empty Surfingkeys sequence")
    script, pos = [], 0
    while pos < len(notation):
        char = notation[pos]
        mods = []
        if char == "<" and pos + 1 < len(notation) and re.match(r"[A-Za-z]", notation[pos + 1]):
            match = re.match(r"<((?:(?:Ctrl|Control|C|Alt|A|Meta|M|Win|Super|Shift|S)-)*)([^>]+|.)>", notation[pos:], re.I)
            if not match:
                raise ValueError("Unclosed <key> notation; use <lt> for a literal opening angle bracket")
            modifier_text, token = match.groups()
            mods = [MODIFIERS[m.lower()] for m in modifier_text.rstrip("-").split("-") if m]
            if token.lower() in SPECIAL:
                target = SPECIAL[token.lower()]
            elif re.fullmatch(r"F(?:[1-9]|[12][0-9]|3[0-5])", token, re.I):
                target = token.upper()
            elif len(token) == 1:
                char = token
                target = None
            else:
                raise ValueError(f"Unknown <key>: {token}; use literal text with localsurf_type")
            pos += match.end()
        else:
            target = None
            pos += 1
        if target is None:
            if char == " ":
                target = "SPACEBAR"
            elif char == "\n":
                target = "ENTER"
            elif char == "\t":
                target = "TAB"
            elif char in ENTITIES:
                target = f"&{ENTITIES[char]};"
            elif char.isascii() and char.isalnum():
                target = char.upper()
                if char.isupper():
                    mods.append("SHIFT_LEFT")
            elif char in "-=_~^":
                # Keep native key names intact. Layout/backend support is decided
                # by Macrohelp, not a US-keyboard reimplementation in this harness.
                target = char
            else:
                raise ValueError("Use localsurf_type for Unicode/text; keys represents keyboard shortcuts")
        chord = " ".join(dict.fromkeys(mods + [target]))
        script.append("{press " + chord + "}")
    return "".join(script)


def bounded_ms(value):
    if type(value) is not int or not 0 <= value <= 86400000:
        raise ValueError("settle_ms must be an integer in 0..86400000 (the native wait range)")
    return "{wait " + str(value) + "}" if value else ""


def request(script, text=None):
    config = {"script": script}
    if text is not None:
        if not isinstance(text, str) or "\0" in text:
            raise ValueError("text must be a string without NUL")
        config["buffers"] = {"Z": text}
    value = {"menu": 2, "inputs": [{"action": "play"}], "config": config, "no_replace": True}
    if len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) > 1024 * 1024:
        raise ValueError("Translated request exceeds Macrohelp's native 1 MiB limit; split at an observation boundary")
    return value


def validate_bindings(value):
    if not isinstance(value, dict) or value.keys() - (RECIPES.keys() - {"scrsht"}):
        raise ValueError("Bindings must use known key-workflow names; scrsht keeps the native capture pane")
    for keys in value.values():
        key_script(keys)
    return value


def compile_tool(name, args, bindings=None):
    if not isinstance(args, dict):
        raise ValueError("Tool arguments must be an object")
    allowed = {
        "localsurf_keys": {"keys", "preview"},
        "localsurf_type": {"text", "before", "after", "preview"},
        "localsurf_workflow": {"name", "keys", "text", "settle_ms", "preview"},
    }
    if name not in allowed:
        raise ValueError("Unknown Localsurf action")
    if args.keys() - allowed[name]:
        raise ValueError("Unknown fields: " + ", ".join(sorted(args.keys() - allowed[name])))
    if "preview" in args and type(args["preview"]) is not bool:
        raise ValueError("preview must be a boolean")
    if name == "localsurf_keys":
        return request(key_script(args.get("keys")))
    if name == "localsurf_type":
        if not isinstance(args.get("text"), str):
            raise ValueError("text must be a string")
        if any(not isinstance(args[k], str) for k in ("before", "after") if k in args):
            raise ValueError("before and after must be key-notation strings")
        before = key_script(args["before"]) if args.get("before") else ""
        after = key_script(args["after"]) if args.get("after") else ""
        return request(before + "{paste Z}" + after, args["text"])
    recipe = args.get("name")
    if recipe not in RECIPES:
        raise ValueError("Unknown workflow; use localsurf_reference topic=workflows")
    if recipe == "scrsht":
        if args.keys() & {"text", "keys", "settle_ms"}:
            raise ValueError("scrsht only opens the existing capture pane; it accepts no keys/text/timing")
        return request("{screen}")
    keys = args.get("keys", (bindings or {}).get(recipe, RECIPES[recipe][0]))
    if not keys:
        raise ValueError(f"{recipe} needs keys from the current pane or an existing native launch binding")
    if recipe == "pass_keys":
        if "text" in args or "settle_ms" in args:
            raise ValueError("pass_keys accepts keys only; for cross-window flows use pass_enter/pass_exit separately")
        bindings = bindings or {}
        return request(key_script(bindings.get("pass_enter", "<Alt-i>")) + key_script(keys)
                       + key_script(bindings.get("pass_exit", "<Esc>")))
    script = key_script(keys)
    if recipe in TEXT_RECIPES:
        if not isinstance(args.get("text"), str):
            raise ValueError(f"{recipe} requires text as a string")
        script += bounded_ms(args.get("settle_ms", 300)) + "{paste Z}" + key_script("<Enter>")
        return request(script, args["text"])
    if "text" in args or "settle_ms" in args:
        raise ValueError("This workflow accepts keys only; use localsurf_type for literal text")
    return request(script)
