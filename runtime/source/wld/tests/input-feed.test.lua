-- Run with Lua 5.4/5.5: lua runtime/source/wld/tests/input-feed.test.lua
-- This executes the real feed against a native-API fixture, never a compositor.
local here = arg[0]:match('^(.*[/\\])') or ''
local source = arg[1] or here .. '../input.lua'
local checks = 0
local function check(name, test)
  if arg[2] and not name:find(arg[2], 1, true) then return end
  test()
  checks = checks + 1
  print('ok - ' .. name)
end
local function contains(text, fragment)
  assert(text:find(fragment, 1, true), text .. ' does not contain ' .. fragment)
end
local function fixture(fail_at)
  local f = { binds = {}, registrations = 0, removes = 0, on_calls = 0, timer_calls = 0, timers = {} }
  -- Model 0.56.2's chord-wide remove, including an unrelated user binding.
  f.user = { key = 'mouse:272', enabled = true, user = true }
  f.binds[1] = f.user
  function f.hl_bind(key, callback, options)
    f.registrations = f.registrations + 1
    if f.registrations == fail_at then error('fixture bind failure') end
    local binding = { key = key, callback = callback, options = options, enabled = true }
    function binding:set_enabled(enabled) self.enabled = enabled end
    function binding:remove()
      f.removes = f.removes + 1
      for _, other in ipairs(f.binds) do if other.key == self.key then other.enabled = false end end
    end
    f.binds[#f.binds + 1] = binding
    return binding
  end
  f.env = setmetatable({ _G = {} }, { __index = _G })
  f.env.hl = {
    bind = f.hl_bind,
    on = function(event, callback)
      assert(event == 'input.keyboard.key')
      f.on_calls = f.on_calls + 1
      f.key = callback
      return { remove = function() f.key = nil end }
    end,
    timer = function(callback, options)
      f.timer_calls = f.timer_calls + 1
      assert(options.timeout == 1000 and options.type == 'repeat')
      local timer = { enabled = true, callback = callback }
      function timer:set_enabled(enabled) self.enabled = enabled end
      f.timers[#f.timers + 1] = timer
      return timer
    end,
  }
  function f:boot() return assert(loadfile(source, 't', self.env))() end
  function f:button(code, state)
    for _, binding in ipairs(self.binds) do
      if not binding.user and binding.enabled and binding.key == 'mouse:' .. code and binding.options.release == (state == 0) then
        binding.callback()
      end
    end
  end
  function f:tick()
    for _, timer in ipairs(self.timers) do if timer.enabled then timer.callback() end end
  end
  return f
end

check('config owns exactly one keyboard subscription, timer and six non-consuming mouse edges', function()
  local f = fixture()
  assert(f:boot() == '{"ready":true}')
  assert(f.registrations == 6 and f.on_calls == 1 and f.timer_calls == 1)
  for _, binding in ipairs(f.binds) do
    if not binding.user then
      local opts = binding.options
      assert(opts.non_consuming and opts.transparent and opts.ignore_mods and opts.submap_universal)
      assert(not opts.locked)
    end
  end
  assert(f:boot() == '{"ready":true}')
  assert(f.registrations == 6 and f.on_calls == 1 and f.timer_calls == 1)
end)

check('all fast mouse taps and modifier edges survive between samples', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  feed.sample(true, false)
  f.key(37, 0, 1) -- Control evdev 29 + XKB offset 8.
  for button = 272, 274 do f:button(button, 1); f:button(button, 0) end
  f.key(37, 0, 0)
  local sample = feed.sample(true, false)
  contains(sample, '"events":[[29,1],[272,1],[272,0],[273,1],[273,0],[274,1],[274,0],[29,0]]')
  contains(sample, '"held":[]')
  contains(feed.sample(true, false), '"events":[]')
end)

check('heatmap-only mode counts keyboard presses, not repeats, clicks or ordered key history', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  feed.sample(false, true)
  f.key(38, 0, 1); f.key(38, 0, 2); f.key(38, 0, 1); f.key(38, 0, 0)
  f:button(272, 1); f:button(272, 0)
  local sample = feed.sample(false, true)
  contains(sample, '"events":[],"held":[],"counts":{"30":1}')
  contains(feed.sample(false, true), '"counts":{}')
end)

check('stop, lock and repeated HUD toggles never register or remove bindings', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  for _ = 1, 20 do
    feed.sample(true, false); f:button(272, 1)
    feed.sample(false, false) -- Also the locked-session sample from Rust.
    assert(f.removes == 0 and f.user.enabled, 'cleanup removed a matching user mouse binding')
    f:button(272, 0); f.key(38, 0, 1)
    assert(next(feed.queue) == nil and next(feed.held) == nil and next(feed.counts) == nil)
  end
  assert(f.registrations == 6 and f.on_calls == 1 and f.timer_calls == 1)
  assert(f.removes == 0 and f.user.enabled)
end)

check('consumer death clears data on lease expiry without touching user bindings', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  feed.sample(true, true); f.key(38, 0, 1); f:button(272, 1)
  f:tick(); assert(feed.display)
  f:tick(); assert(not feed.display and not feed.heatmap)
  assert(next(feed.queue) == nil and next(feed.held) == nil and next(feed.counts) == nil)
  assert(not f.timers[1].enabled and f.removes == 0 and f.user.enabled)
  feed.sample(true, false); f:button(273, 1); f:button(273, 0)
  contains(feed.sample(true, false), '[273,1],[273,0]')
  assert(f.registrations == 6)
end)

check('overflow is bounded and a held button still has a release after draining', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  feed.sample(true, false)
  for _ = 1, 100 do f:button(272, 1); f:button(272, 0) end
  f:button(273, 1)
  assert(#feed.queue <= 128)
  contains(feed.sample(true, false), '"held":[273]')
  f:button(273, 0)
  contains(feed.sample(true, false), '"events":[[273,0]],"held":[]')
end)

check('partial native setup failure disables only owned handles and never retries from a sample', function()
  local f = fixture(4)
  assert(f:boot() == '{"ready":false}')
  local feed = f.env._G.__macrohelp_input
  contains(feed.sample(true, true), '"available":false')
  contains(feed.sample(false, true), 'Native input observer setup failed')
  assert(f.removes == 0 and f.user.enabled and f.key == nil)
  for _, binding in ipairs(f.binds) do if not binding.user then assert(not binding.enabled) end end
  f:boot(); assert(f.registrations == 4)
end)

check('callbacks retain nothing while collection is inactive, including after garbage collection', function()
  local f = fixture(); f:boot()
  local feed = f.env._G.__macrohelp_input
  collectgarbage('collect')
  f.key(38, 0, 1); f:button(272, 1); f:button(272, 0)
  assert(next(feed.queue) == nil and next(feed.held) == nil and next(feed.counts) == nil)
  feed.sample(true, false); f.key(38, 0, 1); f.key(38, 0, 0)
  contains(feed.sample(true, false), '"events":[[30,1],[30,0]]')
end)

check('old feed migration does not execute its unsafe cleanup and requires a config reload', function()
  local f = fixture()
  f.env._G.__macrohelp_input = { version = 2, stop = function() error('unsafe old cleanup called') end }
  assert(f:boot() == '{"ready":false}')
  assert(f.registrations == 0 and f.user.enabled)
end)

check('a new compositor/config lifetime registers once without inheriting held keys', function()
  local old = fixture(); old:boot()
  old.env._G.__macrohelp_input.sample(true, false); old:button(272, 1)
  local fresh = fixture(); fresh:boot()
  contains(fresh.env._G.__macrohelp_input.sample(true, false), '"events":[],"held":[]')
  assert(fresh.registrations == 6 and fresh.removes == 0)
end)

print(checks .. ' input feed checks passed')
