-- KarPur: what each method of the main menu's classes works with, as a report of names.
-- Run with TOTK_LUA_MOD=1 TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\inspect_xmenu.lua
-- find_menu.lua (2026-11-03) found the main menu class C2F32EE4ECA8A9B15 (its constructor body `super` builds the
-- button lists) and C4D2B1CB8AD9685C7 (a method uses N_dlc_00). For the constructor and every prototype method of
-- these classes this lists the name-like strings in the function's bytecode (string.dump): pane and label names,
-- and the obfuscated names of the methods and fields it uses, which tell the methods' roles apart. It leaves the
-- garbage collector alone: the game may steer it itself.
local classes = { "C2F32EE4ECA8A9B15", "C4D2B1CB8AD9685C7" }
local out = {}

local function names_in(f)
  local ok, code = pcall(string.dump, f, true)
  if not ok then return "(not dumpable)" end
  local seen, list = {}, {}
  for s in string.gmatch(code, "[%a_][%w_/%.]+") do
    if #s >= 5 and not seen[s] then
      seen[s] = true
      list[#list + 1] = s
      if #list >= 48 then break end
    end
  end
  code = nil
  return table.concat(list, " ")
end

for _, name in ipairs(classes) do
  local c = _G[name]
  if type(c) ~= "table" then
    out[#out + 1] = name .. ": not in this state"
  else
    out[#out + 1] = string.format("class %s (%s):", name, tostring(rawget(c, "__name__")))
    local sup = rawget(c, "super")
    if type(sup) == "function" then out[#out + 1] = "  super: " .. names_in(sup) end
    local proto = rawget(c, "prototype")
    if type(proto) == "table" then
      local keys = {}
      for k, v in pairs(proto) do
        if type(v) == "function" then keys[#keys + 1] = k end
      end
      table.sort(keys)
      for _, k in ipairs(keys) do
        out[#out + 1] = "  " .. k .. ": " .. names_in(proto[k])
      end
    end
  end
end
return "inspect_xmenu:\n" .. table.concat(out, "\n")
