-- Load from Hyprland configuration, never from an IPC sample. Observers belong
-- to this Lua/config lifetime; only leased, bounded display/aggregate data is kept.
local previous = _G.__macrohelp_input
if previous then
  -- Do not call an older feed's stop(): v2 removes bindings by chord, including
  -- unrelated user bindings. A full config reload creates a fresh Lua state.
  if previous.version == 3 then return previous.ready and '{"ready":true}' or '{"ready":false}' end
  return '{"ready":false}'
end
if not hl or not hl.on or not hl.bind or not hl.timer then return '{"ready":false}' end

local feed = { version = 3, ready = false, queue = {}, held = {}, counts = {}, display = false, heatmap = false, idle = 0 }
local timer, subscription
local mouse_binds = {}
function feed.stop()
  feed.queue, feed.held, feed.counts = {}, {}, {}
  feed.display, feed.heatmap = false, false
  if timer then timer:set_enabled(false) end
end
local function accept(code, state, keyboard)
  if not feed.ready or (not feed.display and not feed.heatmap) then return end
  if state ~= 0 and state ~= 1 then return end -- Native repeat (2) is not a release.
  local down = state == 1
  if down == (feed.held[code] == true) then return end
  feed.held[code] = down or nil
  if keyboard and down and feed.heatmap then feed.counts[code] = (feed.counts[code] or 0) + 1 end
  if not feed.display then return end
  if #feed.queue >= 128 then
    -- Never retain an unbounded input stream if the display stalls.
    feed.queue = {}
  end
  feed.queue[#feed.queue + 1] = '[' .. code .. ',' .. (down and '1' or '0') .. ']'
end
local function on_key(xkb_code, _, state)
  local code = tonumber(xkb_code)
  if not code or code < 8 or code > 775 then return end
  code = code - 8 -- hl.on exposes XKB codes, not evdev codes.
  accept(code, state, true)
end
function feed.sample(display, heatmap)
  if not feed.ready then return '{"available":false,"events":[],"held":[],"counts":{},"error":"Native input observer setup failed; reload Hyprland config and check config errors."}' end
  if not display and not heatmap then feed.stop(); return '{"available":true,"events":[],"held":[],"counts":{}}' end
  local active = feed.display or feed.heatmap
  feed.idle = 0
  if not display then
    feed.queue = {}
    for code = 272, 274 do feed.held[code] = nil end
  end
  feed.display, feed.heatmap = display, heatmap
  if not active then timer:set_enabled(true) end
  local held = {}
  for code in pairs(feed.held) do held[#held + 1] = code end
  table.sort(held)
  for i, code in ipairs(held) do held[i] = tostring(code) end
  local counts = {}
  for code, count in pairs(feed.counts) do counts[#counts + 1] = '"' .. code .. '":' .. count end
  local result = '{"available":true,"events":[' .. table.concat(feed.queue, ',')
    .. '],"held":[' .. (display and table.concat(held, ',') or '') .. '],"counts":{' .. table.concat(counts, ',') .. '}}'
  feed.queue, feed.counts = {}, {}
  return result
end
-- These calls run only while the user config is loading. sample(), stop(),
-- lock transitions and lease expiry never create or remove native observers.
local ok
ok, timer = pcall(hl.timer, function()
  feed.idle = feed.idle + 1
  if feed.idle >= 2 then feed.stop() end
end, { timeout = 1000, type = 'repeat' })
if not ok or not timer then return '{"ready":false}' end
timer:set_enabled(false)
ok, subscription = pcall(hl.on, 'input.keyboard.key', on_key)
if ok and subscription then
  for code = 272, 274 do
    for state = 0, 1 do
      local button, edge = code, state
      local binding
      ok, binding = pcall(hl.bind, 'mouse:' .. button, function()
        if feed.display then accept(button, edge, false) end
      end, { non_consuming = true, transparent = true, ignore_mods = true,
             submap_universal = true, release = edge == 0 })
      if not ok or not binding then ok = false; break end
      mouse_binds[#mouse_binds + 1] = binding
    end
    if not ok then break end
  end
end
feed.ready = ok and subscription ~= nil and #mouse_binds == 6
if not feed.ready then
  -- Keybind:remove() is chord-wide in 0.56.2. Disable only our own handles on
  -- partial setup failure; the compositor will discard them on config reload.
  for _, binding in ipairs(mouse_binds) do pcall(binding.set_enabled, binding, false) end
  if subscription then subscription:remove() end
end
_G.__macrohelp_input = feed
return feed.ready and '{"ready":true}' or '{"ready":false}'
