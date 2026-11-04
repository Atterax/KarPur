-- KarPur: find the Haxe classes and methods behind the game's options page, as a report of names (find_menu.lua for
-- the options page: the PC settings page is to reuse it; docs/progress.md 2026-11-04).
-- Run with TOTK_LUA_MOD=1 TOTK_LUA_MOD_FILE=runtime/lua/find_options.lua (path relative to build\game, or absolute).
-- Every function of every global table and of its prototype table is dumped with string.dump and searched for the
-- menu's pane and label names; for each class with a hit, the names of its fields and of its prototype's fields are
-- listed. Names only (the game's are obfuscated hashes such as C6F7A8CF2CB565DD3).
local words = {
  "msg_ui_option_01_info", "view_option_00", "msg_ui_option_select_01", "msg_ui_option_skip_01", "selectMax",
  "detailSelect", "state_option", "option_body",
}
if rawget(_G, "KARPUR_FIND_OPTIONS") then return "find_options: already run in this Lua state" end
rawset(_G, "KARPUR_FIND_OPTIONS", true)
if type(string.dump) ~= "function" then
  return "string.dump is not available in this state"
end

local out, scanned, failed = {}, 0, 0
local classes = {}

local function search(code)
  local hit = {}
  for _, w in ipairs(words) do
    if string.find(code, w, 1, true) then hit[#hit + 1] = w end
  end
  return hit
end

local function scan(cls, where, t)
  for k, v in pairs(t) do
    if type(v) == "function" then
      scanned = scanned + 1
      local ok, code = pcall(string.dump, v, true)
      if ok then
        local hit = search(code)
        code = nil
        if #hit > 0 then
          out[#out + 1] = string.format("  %s%s.%s: %s", cls, where, tostring(k), table.concat(hit, ", "))
          classes[cls] = true
        end
      else
        failed = failed + 1
      end
    end
  end
end

for name, v in pairs(_G) do
  if type(name) == "string" and type(v) == "table" and v ~= _G then
    scan(name, "", v)
    local proto = rawget(v, "prototype")
    if type(proto) == "table" then scan(name, ".prototype", proto) end
  end
end

local function keys(t)
  local list = {}
  for k, v in pairs(t) do list[#list + 1] = tostring(k) .. ":" .. type(v) end
  table.sort(list)
  return table.concat(list, " ")
end

out[#out + 1] = string.format("scanned %d functions (%d not dumpable)", scanned, failed)
for cls in pairs(classes) do
  local t = _G[cls]
  out[#out + 1] = "class " .. cls .. " fields: " .. keys(t)
  local proto = rawget(t, "prototype")
  if type(proto) == "table" then out[#out + 1] = "class " .. cls .. " prototype: " .. keys(proto) end
  local super = rawget(t, "__super__") or rawget(t, "super")
  if super then out[#out + 1] = "class " .. cls .. " super: " .. tostring(super) end
end
return "find_options:\n" .. table.concat(out, "\n")
