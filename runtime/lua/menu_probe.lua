-- KarPur: look into the main menu object while the game runs (TOTK_LUA_MOD=1
-- TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\menu_probe.lua). Lines start with "KARPUR" and go to the log via print.
-- The class C2F32EE4ECA8A9B15 (inspect_xmenu.lua): F46013EC08BDF7153 builds the menu when it opens,
-- FDE253113EFC442F6 sets up the grid GridPanel_00, F1EC109E2FFD97BC1 moves the cursor and selects,
-- F39C0D21A643AD050 confirms. Each is wrapped: the game's method runs first, unchanged, then the report runs under
-- pcall, so an error in it never reaches the game. Reported: the menu object's fields (name, type; for Haxe arrays
-- the length and element types; for objects their class), and the arguments of the first cursor moves. Names and
-- small numbers only.
-- The six "states" runtime/lua_mod.c runs in are threads of one Lua state that share the globals (the first run of
-- this probe wrapped every method six times), so a global flag keeps it to one run.
if rawget(_G, "KARPUR_MENU_PROBE") then return "menu_probe: already set in this Lua state" end
rawset(_G, "KARPUR_MENU_PROBE", true)
local CLS = "C2F32EE4ECA8A9B15"
local cls = rawget(_G, CLS)
if type(cls) ~= "table" or type(rawget(cls, "prototype")) ~= "table" then
  return "menu_probe: no " .. CLS .. " in this state"
end
local proto = cls.prototype

-- class tables by identity, for naming objects
local class_name = {}
for name, v in pairs(_G) do
  if type(name) == "string" and type(v) == "table" and rawget(v, "prototype") then class_name[v] = name end
end

local function describe(v)
  local t = type(v)
  if t == "number" or t == "boolean" then return t .. " " .. tostring(v) end
  if t == "string" then return "string(" .. #v .. ")" end
  if t ~= "table" then return t end
  local len = rawget(v, "length")
  if type(len) == "number" then
    local kinds = {}
    for i = 0, math.min(len, 8) - 1 do
      local e = rawget(v, i)
      local c = type(e) == "table" and (class_name[e.__class__] or "table") or type(e)
      kinds[#kinds + 1] = c
    end
    return string.format("array %d [%s]", len, table.concat(kinds, ","))
  end
  local c = class_name[v.__class__]
  if c then return "object " .. c end
  return "table"
end

-- Fields: the object's own keys (the first run showed numbered slots 1..38 next to the methods), the methods left
-- out; then the names its class lists in __fields__.
local function report_object(tag, self)
  local keys = {}
  for k, v in pairs(self) do
    if type(v) ~= "function" then keys[#keys + 1] = k end
  end
  table.sort(keys, function(a, b)
    if type(a) == type(b) then return a < b end
    return type(a) == "number"
  end)
  print(string.format("KARPUR %s: menu object %s, %d fields", tag, class_name[self.__class__] or "?", #keys))
  for _, k in ipairs(keys) do
    print("KARPUR   [" .. tostring(k) .. "] = " .. describe(rawget(self, k)))
  end
  local f = rawget(self, "__fields__") or (type(self.__class__) == "table" and rawget(self.__class__, "__fields__"))
  if type(f) == "table" then
    local names = {}
    for k, v in pairs(f) do names[#names + 1] = tostring(k) .. "=" .. tostring(v) end
    table.sort(names)
    print("KARPUR   __fields__: " .. table.concat(names, " "))
  end
end

local moves = 0
local function wrap(name, after)
  local orig = proto[name]
  if type(orig) ~= "function" then
    print("KARPUR menu_probe: no method " .. name)
    return
  end
  proto[name] = function(self, ...)
    local r = table.pack(orig(self, ...))
    local ok, err = pcall(after, self, ...)
    if not ok then print("KARPUR menu_probe error in " .. name .. ": " .. tostring(err)) end
    return table.unpack(r, 1, r.n)
  end
end

wrap("F46013EC08BDF7153", function(self) report_object("menu opened", self) end)
wrap("FDE253113EFC442F6", function(self) print("KARPUR grid set up") end)
-- The cursor method takes no arguments (first run): it reads the selection from the object, so each call reports the
-- object's number fields, which shows the one that holds the selected button.
wrap("F1EC109E2FFD97BC1", function(self)
  moves = moves + 1
  if moves > 20 then return end
  local parts = {}
  for k, v in pairs(self) do
    if type(v) == "number" then parts[#parts + 1] = "[" .. tostring(k) .. "]=" .. tostring(v) end
  end
  table.sort(parts)
  print(string.format("KARPUR cursor %d: %s", moves, table.concat(parts, " ")))
end)
wrap("F39C0D21A643AD050", function(self) print("KARPUR confirm") end)
return "menu_probe: hooks set on " .. CLS
