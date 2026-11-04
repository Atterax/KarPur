-- KarPur: which standard libraries the game's Lua states have, and whether print reaches anything.
-- Run with TOTK_LUA_MOD=1 TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\probe_libs.lua
-- The main menu's navigation has to be changed while the game runs (when the menu opens), so the runtime's Lua needs
-- a way to report from inside a hook; this lists what there is. Names and types only.
local out = {}
local function has(name, t)
  if type(t) ~= "table" then
    out[#out + 1] = name .. "=" .. type(t)
    return
  end
  local keys = {}
  for k in pairs(t) do keys[#keys + 1] = tostring(k) end
  table.sort(keys)
  out[#out + 1] = name .. "={" .. table.concat(keys, ",") .. "}"
end
for _, n in ipairs({ "print", "io", "os", "debug", "package", "coroutine", "utf8", "math", "string", "table",
                     "require", "load", "loadstring", "dofile", "collectgarbage", "setmetatable", "rawget", "_hx_print" }) do
  has(n, _G[n])
end
if type(print) == "function" then
  pcall(print, "KARPUR_PRINT_TEST from the runtime's Lua")
end
if type(io) == "table" and type(io.write) == "function" then
  pcall(io.write, "KARPUR_IO_WRITE_TEST from the runtime's Lua\n")
end
local gcmode = "?"
if type(collectgarbage) == "function" then
  local ok, r = pcall(collectgarbage, "isrunning")
  gcmode = tostring(ok) .. "/" .. tostring(r)
end
out[#out + 1] = "gc running: " .. gcmode
return "probe_libs:\n" .. table.concat(out, "\n")
