-- KarPur: runs once in the game's own Lua state, right after the game's main script has run (runtime/lua_mod.c,
-- TOTK_LUA_MOD=1). Step 1 ("hello Lua", docs/progress.md 2026-11-03): show that it runs and what the game's script
-- defined, as a report string the runtime logs. Names and counts only.
local globals, tables, functions = 0, 0, 0
local names = {}
for k, v in pairs(_G) do
  globals = globals + 1
  local t = type(v)
  if t == "table" then
    tables = tables + 1
  elseif t == "function" then
    functions = functions + 1
  end
  if type(k) == "string" and #names < 60 then
    names[#names + 1] = k .. ":" .. t
  end
end
table.sort(names)
return string.format("hello from KarPur Lua (%s): %d globals, %d tables, %d functions; first names: %s",
  _VERSION, globals, tables, functions, table.concat(names, " "))
