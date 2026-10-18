-- Session-local display buffer. No files, typed text, or key-event timestamps.
if _G.__macrohelp_input then return '{"ready":true}' end
if not hl or not hl.on or not hl.timer then return '{"ready":false}' end

local feed = { queue = {}, held = {}, counts = {}, display = false, heatmap = false, subscription = nil, idle = 0 }
local timer
function feed.stop()
  if feed.subscription then feed.subscription:remove() end
  feed.subscription = nil
  feed.queue, feed.held, feed.counts = {}, {}, {}
  feed.display, feed.heatmap = false, false
  if timer then timer:set_enabled(false) end
end
local function on_key(xkb_code, _, state)
  if state ~= 0 and state ~= 1 then return end -- Native repeat (2) is not a release.
  local code = tonumber(xkb_code)
  if not code or code < 8 or code > 775 then return end
  code = code - 8 -- hl.on exposes XKB codes, not evdev codes.
  local down = state == 1
  if down == (feed.held[code] == true) then return end
  feed.held[code] = down or nil
  if down and feed.heatmap then feed.counts[code] = (feed.counts[code] or 0) + 1 end
  if not feed.display then return end
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
function feed.sample(display, heatmap)
  if not display and not heatmap then feed.stop(); return '{"available":true,"events":[],"held":[],"counts":{}}' end
  feed.idle = 0
  if not display then feed.queue = {} end
  feed.display, feed.heatmap = display, heatmap
  if not start() then return '{"available":false,"events":[],"held":[]}' end
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
timer = hl.timer(function()
  feed.idle = feed.idle + 1
  if feed.idle >= 2 then feed.stop() end
end, { timeout = 1000, type = 'repeat' })
timer:set_enabled(false)
_G.__macrohelp_input = feed
return '{"ready":true}'
