-- Session-local display buffer. No files, typed text, or key-event timestamps.
if _G.__macrohelp_input then return '{"ready":true}' end
if not hl or not hl.on or not hl.timer then return '{"ready":false}' end

local feed = { queue = {}, held = {}, subscription = nil, idle = 0 }
local timer
function feed.stop()
  if feed.subscription then feed.subscription:remove() end
  feed.subscription = nil
  feed.queue, feed.held = {}, {}
  if timer then timer:set_enabled(false) end
end
local function on_key(xkb_code, _, state)
  local code = tonumber(xkb_code)
  if not code or code < 8 or code > 775 then return end
  code = code - 8 -- hl.on exposes XKB codes, not evdev codes.
  local down = state == 1
  if down == (feed.held[code] == true) then return end
  feed.held[code] = down or nil
  if #feed.queue >= 128 then
    -- Never retain an unbounded input stream if the display stalls.
    feed.queue = {}
  end
  feed.queue[#feed.queue + 1] = '[' .. code .. ',' .. (down and '1' or '0') .. ']'
end
local function start()
  if feed.subscription then return true end
  local ok, subscription = pcall(hl.on, 'input.keyboard.key', on_key)
  if not ok or not subscription then return false end
  feed.subscription = subscription
  timer:set_enabled(true)
  return true
end
function feed.sample(enabled)
  if not enabled then feed.stop(); return '{"available":true,"events":[],"held":[]}' end
  feed.idle = 0
  if not start() then return '{"available":false,"events":[],"held":[]}' end
  local held = {}
  for code in pairs(feed.held) do held[#held + 1] = code end
  table.sort(held)
  for i, code in ipairs(held) do held[i] = tostring(code) end
  local result = '{"available":true,"events":[' .. table.concat(feed.queue, ',')
    .. '],"held":[' .. table.concat(held, ',') .. ']}'
  feed.queue = {}
  return result
end
timer = hl.timer(function()
  feed.idle = feed.idle + 1
  if feed.idle >= 2 then feed.stop() end
end, { timeout = 1000, type = 'repeat' })
timer:set_enabled(false)
_G.__macrohelp_input = feed
return '{"ready":true}'
