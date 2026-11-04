-- KarPur: what the main menu class keeps in its static field SA9A94FAF23CF031B, which its grid method
-- FDFD30DB128D3525F reads next to GridPanel_00 (inspect_xmenu.lua). Run once at start with TOTK_LUA_MOD=1
-- TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\grid_probe.lua. The tables are walked three levels deep: numbers,
-- booleans, the game's identifier strings, the class names of objects, the lengths of Haxe arrays.
if rawget(_G, "KARPUR_GRID_PROBE") then return "grid_probe: already run in this Lua state" end
rawset(_G, "KARPUR_GRID_PROBE", true)
local cls = rawget(_G, "C2F32EE4ECA8A9B15")
if type(cls) ~= "table" then return "grid_probe: no menu class in this state" end

local class_name = {}
for name, v in pairs(_G) do
  if type(name) == "string" and type(v) == "table" and rawget(v, "prototype") then class_name[v] = name end
end

local out, lines = {}, 0
local function walk(v, indent, depth)
  lines = lines + 1
  if lines > 400 then return end
  local t = type(v)
  if t ~= "table" then
    out[#out + 1] = indent .. t .. " " .. tostring(v)
    return
  end
  local cn = class_name[rawget(v, "__class__") or (getmetatable(v) and getmetatable(v).__index and
                         rawget(getmetatable(v).__index, "__class__"))]
  local len = rawget(v, "length")
  out[#out + 1] = indent .. (cn and ("object " .. cn) or "table") ..
                  (type(len) == "number" and (" length " .. len) or "")
  if depth >= 3 then return end
  local keys = {}
  for k in pairs(v) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b)
    if type(a) == type(b) and (type(a) == "number" or type(a) == "string") then return a < b end
    return type(a) == "number"
  end)
  for _, k in ipairs(keys) do
    local e = rawget(v, k)
    if type(e) ~= "function" then
      out[#out + 1] = indent .. "  [" .. tostring(k) .. "]:"
      walk(e, indent .. "    ", depth + 1)
    end
  end
end

walk(rawget(cls, "SA9A94FAF23CF031B"), "", 0)
return "grid_probe: C2F32EE4ECA8A9B15.SA9A94FAF23CF031B\n" .. table.concat(out, "\n")
