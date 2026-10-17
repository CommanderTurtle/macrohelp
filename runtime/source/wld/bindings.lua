-- Optional examples for ~/.config/hypr/bindings.lua. Nothing installs these.
local macrohelp = "omarchy-shell -q io.github.commanderturtle.macrohelp "
o.bind("SHIFT + ALT + 0", "Macrohelp: save point", macrohelp .. "point")
o.bind("SHIFT + ALT + 1", "Macrohelp: buffers", macrohelp .. "page Buffers")
o.bind("SHIFT + ALT + 2", "Macrohelp: workflow", macrohelp .. "page Workflow")
o.bind("SHIFT + ALT + 3", "Macrohelp: pointer tools", macrohelp .. "page Pointer")
o.bind("SHIFT + ALT + 4", "Macrohelp: left click", macrohelp .. "click LEFT_MOUSE")
o.bind("SHIFT + ALT + 5", "Macrohelp: right click", macrohelp .. "click RIGHT_MOUSE")
o.bind("SHIFT + ALT + 6", "Macrohelp: middle click", macrohelp .. "click MIDDLE_MOUSE")
o.bind("SHIFT + ALT + 7", "Macrohelp: stop", macrohelp .. "stop")
o.bind("SHIFT + ALT + 8", "Macrohelp: view", macrohelp .. "page View")
o.bind("SHIFT + ALT + 9", "Macrohelp: keys", macrohelp .. "page Keys")
