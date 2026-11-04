-- KarPur: the "PC settings" entry in the game's main menu (X menu) and its page, run in the game's Lua state by
-- runtime/lua_mod.c, which has this file built in (TOTK_LUA_MOD_FILE names a file to try instead). docs/progress.md
-- 2026-11-03/04.
--
-- The menu object (class C2F32EE4ECA8A9B15, menu_probe.lua) keeps its buttons in Haxe arrays of seven (0-based,
-- with a length field): [5] the text panes (L_xbutton_00/T_btn_name_00 ...), [6] the text labels (xmenu_button_bag
-- ... xmenu_dlc_button), and their count in [9]; the setup (F46013EC08BDF7153) fills [7] with the button objects and
-- [11] with their actions. The seventh place, the downloadable-content button, becomes "PC settings": its look, text,
-- place and cursor order come from game/ui_patch.txt (tools/uilayout.py, tools/msgpatch.py), its action is set here.
--
-- The PC settings page is the game's own options page (class C9813692A25E9E386, options_probe.lua) with another
-- entry list. The page keeps its entries in [12], a Haxe array of objects {menuName, infoName, numId (the value),
-- startNum, defaultId, selectMax, detailSelect (the value labels), buttonType (0 a choice, 1 a gauge)}. Opening it
-- (F7C68FEDB79AB6396) reads the saved options into numId by index, then sets up the scroll list (FE144991FA9F4B640),
-- which takes the list's length; left / right (F58FE3DC34D491A25) only change numId. So in PC mode the list is
-- swapped right before the scroll setup, every change goes to the runtime at once ("KARPUR_CMD set KEY VALUE"), and
-- B (F1F6114A4533DE974) puts the game's own list back, untouched, before the page saves anything. Y (restore
-- defaults, F2D9FC67FADE8F60E) does nothing in PC mode. The texts are KarPur's, in labels of the message file "option"
-- that the options page never shows (PC_LABELS; tools/pc_texts.py writes them). The current values come from the
-- runtime: "KARPUR_CMD sync <table> KEY:values ..." writes each setting's position into a Lua table of integers.
--
-- Everything here runs around the game's methods under pcall, so an error of ours never reaches the game; lines
-- starting with KARPUR go to the log.
if rawget(_G, "KARPUR_MENU_MOD") then return "menu_mod: already set in this Lua state" end
rawset(_G, "KARPUR_MENU_MOD", true)

local CLS, SETUP = "C2F32EE4ECA8A9B15", "F46013EC08BDF7153"
local F_PANES, F_LABELS, F_ACTIONS = 5, 6, 11
local DLC_LABEL, OPTIONS_LABEL = "xmenu_dlc_button", "xmenu_button_options"

local PAGE = "C9813692A25E9E386"
local P_ENTRIES = 12
local M_OPEN, M_SCROLL, M_CHANGE = "F7C68FEDB79AB6396", "FE144991FA9F4B640", "F58FE3DC34D491A25"
local M_BACK, M_RESET, M_CLOSE = "F1F6114A4533DE974", "F2D9FC67FADE8F60E", "F1C2AA00ADAC52EC5"
local M_REFRESH, M_UPDATE = "F6A8DB7A4AC4B16F3", "FE94F3E13286232CF"

-- Labels of "option" the options page does not use, and the PC text each holds (tools/pc_texts.py reads this block).
local PC_LABELS = {
  title = "msg_ui_option_02",
  fullscreen = "msg_ui_option_02_01",
  fullscreen_info = "msg_ui_option_02_02",
  fps = "msg_ui_option_02_info",
  fps_info = "msg_ui_option_03",
  fps_30 = "msg_ui_option_03_01",
  fps_40 = "msg_ui_option_03_02",
  fps_45 = "msg_ui_option_03_info",
  fps_60 = "msg_ui_option_04",
  fps_90 = "msg_ui_option_04_01",
  fps_120 = "msg_ui_option_04_02",
  fps_144 = "msg_ui_option_04_info",
  fps_display = "msg_option_05",
  sync = "msg_option_05_01",
  sync_info = "msg_option_05_02",
  sync_mailbox = "msg_option_05_info",
  sync_vsync = "msg_ui_option_06",
  upscaler = "msg_ui_option_06_01",
  upscaler_info = "msg_ui_option_06_02",
  upscaler_fsr = "msg_ui_option_06_info",
  upscaler_bilinear = "msg_ui_option_08",
  sharpness = "msg_ui_option_08_info",
  sharpness_info = "msg_ui_option_14",
  perf = "msg_ui_option_14_info",
  perf_info = "msg_ui_option_20",
  perf_fps = "msg_ui_option_20_info",
  perf_full = "ori_option_title",   -- msg_option_10_01..03 are used by a script of the game: not free
  window = "msg_ui_option_18_01",
  window_info = "msg_ui_option_18_02",
  window_720 = "msg_ui_option_09_info_01",
  window_900 = "msg_ui_option_09_info_02",
  window_1080 = "msg_ui_option_lan_win_01",
  window_1440 = "msg_ui_option_lan_win_02",
  window_2160 = "msg_ui_option_lan_win_03",
  keymap = "msg_ui_option_common_04",
  keymap_info = "msg_ui_option_common_07",
  keymap_open = "msg_ui_option_13_info",
}
local ON, OFF = "msg_ui_option_common_01", "msg_ui_option_common_02"   -- the game's own "On" / "Off"
local GAUGE = "msg_ui_option_volume"                                   -- what the volume gauges show

-- The page's entries: the runtime setting, its values in order, their labels (or a gauge), the default's position.
-- At most 12: the page's value refresh (F6A8DB7A4AC4B16F3) names the game's three volumes, which sit at positions 12 to
-- 14 of its own list, so no entry of ours goes there.
local PC = {
  { key = "FULLSCREEN", name = "fullscreen", values = { "0", "1" }, labels = { OFF, ON }, default = 1 },
  { key = "WINDOW_SIZE", name = "window", values = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" },
    widths = { 1280, 1600, 1920, 2560, 3840 },
    labels = { "window_720", "window_900", "window_1080", "window_1440", "window_2160" }, default = 3 },
  { key = "FPS_LIMIT", name = "fps", values = { "30", "40", "45", "60", "90", "120", "144", "display" },
    labels = { "fps_30", "fps_40", "fps_45", "fps_60", "fps_90", "fps_120", "fps_144", "fps_display" }, default = 4 },
  { key = "VK_PRESENT", name = "sync", values = { "mailbox", "fifo", "immediate" },
    labels = { "sync_mailbox", "sync_vsync", OFF }, default = 1 },
  { key = "UPSCALER", name = "upscaler", values = { "fsr", "bilinear" },
    labels = { "upscaler_fsr", "upscaler_bilinear" }, default = 1 },
  { key = "SHARPNESS", name = "sharpness", values = { "0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100" },
    gauge = true, default = 9 },
  { key = "PERF_OVERLAY", name = "perf", values = { "off", "fps", "full" }, labels = { OFF, "perf_fps", "perf_full" },
    default = 1 },
  -- no setting: left or right shows the Controls page (ui/menu.cpp layout_controls)
  { action = "open_controls", name = "keymap", values = { "", "" }, labels = { "keymap_open", "keymap_open" }, default = 1 },
}

local function label_of(id)
  return PC_LABELS[id] or id   -- a PC text id, or one of the game's own labels
end

local cls = rawget(_G, CLS)
local proto = type(cls) == "table" and rawget(cls, "prototype")
if type(proto) ~= "table" or type(proto[SETUP]) ~= "function" then
  return "menu_mod: no " .. CLS .. "." .. SETUP .. " in this state"
end

-- ---- PC mode of the options page
local pc_mode = false   -- the next opening of the options page is the PC settings page
local swapped = nil     -- while our list is in the page: { list = the game's list, ours = our list, last = values }

-- The current position of each setting in its values (1-based), from the runtime (sync), else the default; and the
-- monitor's width (0 unknown).
local function current_values()
  local slots = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
  local addr = tostring(slots):match("(%x+)$")
  for i, e in ipairs(PC) do
    slots[i] = -2
    if addr and e.key then
      print("KARPUR_CMD sync " .. addr .. " " .. (i - 1) .. " " .. e.key .. ":" .. table.concat(e.values, ","))
    end
  end
  if addr then print("KARPUR_CMD sync " .. addr .. " " .. #PC .. " MONITOR_W") end
  local out = {}
  for i, e in ipairs(PC) do
    local pos = slots[i]
    out[i] = (pos >= 0 and pos < #e.values) and pos + 1 or e.default
  end
  return out, slots[#PC + 1]
end

-- A Haxe array (0-based, with a length field) with the metatable of one of the game's.
local function hx_array(items, like)
  local a = { length = #items }
  for i, v in ipairs(items) do a[i - 1] = v end
  local mt = getmetatable(like)
  if mt then setmetatable(a, mt) end
  return a
end

local function our_list(game_list)
  local like = rawget(game_list, 0)
  local values, monitor = current_values()
  local entries = {}
  for i, e in ipairs(PC) do
    local labels, count = {}, #e.values
    if e.widths and monitor > 0 then   -- no window size beyond the monitor
      count = 0
      for _, w in ipairs(e.widths) do if w <= monitor then count = count + 1 end end
      count = math.max(count, 1)
      values[i] = math.min(values[i], count)
    end
    if e.gauge then
      labels[1] = GAUGE
    else
      for j = 1, count do labels[j] = label_of(e.labels[j]) end
    end
    local entry = {
      menuName = label_of(e.name), infoName = label_of(e.name .. "_info"),
      numId = values[i] - 1, startNum = values[i] - 1, defaultId = e.default - 1,
      selectMax = count, detailSelect = hx_array(labels, rawget(like, "detailSelect")),
      buttonType = e.gauge and 1 or 0, __fields__ = rawget(like, "__fields__"),
    }
    local mt = getmetatable(like)
    if mt then setmetatable(entry, mt) end
    entries[i] = entry
  end
  return hx_array(entries, game_list), values
end

local function swap_in(self)
  local game_list = rawget(self, P_ENTRIES)
  if type(game_list) ~= "table" or type(rawget(game_list, 0)) ~= "table" then return end
  local ours, values = our_list(game_list)
  rawset(self, P_ENTRIES, ours)
  swapped = { list = game_list, ours = ours, last = values }
  print("KARPUR menu_mod: PC settings page opened (" .. #PC .. " entries)")
end

local function swap_out(self)
  if not swapped then return end
  rawset(self, P_ENTRIES, swapped.list)
  swapped = nil
end

-- The entry whose value changed goes to the runtime. Checked after left / right, after the row's refresh, on every
-- update of the page and when it closes, so a value that changes on another path is not lost.
local function send_changes()
  if not swapped then return end
  for i, e in ipairs(PC) do
    local entry = rawget(swapped.ours, i - 1)
    local v = type(entry) == "table" and rawget(entry, "numId")
    if type(v) == "number" and v + 1 ~= swapped.last[i] and e.values[v + 1] then
      if e.action then
        rawset(entry, "numId", swapped.last[i] - 1)   -- a button: its one label stays
        print("KARPUR_CMD " .. e.action)
      else
        swapped.last[i] = v + 1
        print("KARPUR_CMD set " .. e.key .. " " .. e.values[v + 1])
      end
    end
  end
end

local page_note = "no options page class " .. PAGE
local page = rawget(_G, PAGE)
local pproto = type(page) == "table" and rawget(page, "prototype")
if type(pproto) == "table" then
  local function wrap(name, before, after)
    local fn = rawget(pproto, name)
    if type(fn) ~= "function" then return false end
    rawset(pproto, name, function(self, ...)
      if before then
        local ok, skip = pcall(before, self, ...)
        if not ok then print("KARPUR menu_mod: " .. name .. ": " .. tostring(skip)) end
        if ok and skip then return end
      end
      local res = table.pack(fn(self, ...))
      if after then
        local ok, err = pcall(after, self, ...)
        if not ok then print("KARPUR menu_mod: " .. name .. ": " .. tostring(err)) end
      end
      return table.unpack(res, 1, res.n)
    end)
    return true
  end
  local ok = wrap(M_SCROLL, function(self) if pc_mode and not swapped then swap_in(self) end end)
  ok = wrap(M_CHANGE, nil, send_changes) and ok
  ok = wrap(M_REFRESH, nil, send_changes) and ok
  ok = wrap(M_UPDATE, nil, send_changes) and ok
  ok = wrap(M_BACK, function(self) send_changes(); swap_out(self) end) and ok
  ok = wrap(M_RESET, function() return swapped ~= nil end) and ok
  ok = wrap(M_CLOSE, function(self) swap_out(self); pc_mode = false end) and ok
  page_note = ok and "options page wrapped for the PC mode" or "options page: a method is missing"
  if ok then print("KARPUR_CMD pc_page_ready") end   -- only now the F1 menu may be locked
end

-- The page's title in PC mode: the header text it fetches becomes ours.
do
  local helper = rawget(_G, "c8C3BF576")
  local fetch = type(helper) == "table" and rawget(helper, "fC8CEF9EF")
  if type(fetch) == "function" then
    rawset(helper, "fC8CEF9EF", function(file, label, ...)
      if pc_mode and file == "appli_header" and label == "appli_header_00_15" then
        return fetch("option", PC_LABELS.title, ...)
      end
      return fetch(file, label, ...)
    end)
  end
end

-- ---- the main menu
-- The list button's layout (game/ui_patch.txt: --part L_xbutton_06=xmenu_button_00) names its text pane
-- T_btn_name_00, not T_00: before the setup the seventh text pane in [5] is renamed.
local function rename_text_pane(self)
  local panes = rawget(self, F_PANES)
  if type(panes) ~= "table" then return nil end
  for i = 0, (rawget(panes, "length") or 0) - 1 do
    if rawget(panes, i) == "L_xbutton_06/T_00" then
      rawset(panes, i, "L_xbutton_06/T_btn_name_00")
      return i
    end
  end
  return nil
end

-- After the setup: the DLC button opens the options page in PC mode (through the Options button's own action), and
-- the Options button's action clears PC mode, so the real options page never shows our list.
local function retarget(self)
  local labels, actions = rawget(self, F_LABELS), rawget(self, F_ACTIONS)
  if type(labels) ~= "table" or type(actions) ~= "table" then return nil end
  local dlc, opt
  for i = 0, (rawget(labels, "length") or 0) - 1 do
    if rawget(labels, i) == DLC_LABEL then dlc = i end
    if rawget(labels, i) == OPTIONS_LABEL then opt = i end
  end
  local options_action = opt and rawget(actions, opt)
  if not dlc or type(options_action) ~= "function" then return nil end
  rawset(actions, opt, function(...)
    pc_mode = false
    return options_action(...)
  end)
  rawset(actions, dlc, function(...)
    pc_mode = true
    return options_action(...)
  end)
  return dlc
end

local orig = proto[SETUP]
proto[SETUP] = function(self, ...)
  local okp, rp = pcall(rename_text_pane, self)
  if not okp then print("KARPUR menu_mod: error while renaming the text pane: " .. tostring(rp)) end
  local res = table.pack(orig(self, ...))
  local ok, r = pcall(retarget, self)
  if not ok then
    print("KARPUR menu_mod: error while setting the PC settings action: " .. tostring(r))
  elseif r then
    print(string.format("KARPUR menu_mod: button %d (the DLC place) now opens the PC settings page", r))
  else
    print("KARPUR menu_mod: no DLC or Options button with an action after the setup")
  end
  return table.unpack(res, 1, res.n)
end
return "menu_mod: main menu setup wrapped (" .. CLS .. "." .. SETUP .. "); " .. page_note
