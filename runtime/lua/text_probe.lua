-- KarPur: how the game puts its own values (numbers, names) into texts, as a report of names and argument types, for
-- the PC settings page (docs/progress.md 2026-11-04). Run with TOTK_LUA_MOD=1
-- TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\text_probe.lua, then show texts with names in them (talk to people,
-- start a picnic, open the boxes).
-- Runs 1 and 2: none of the functions of the text helper c8C3BF576 takes a value, and the text object (userdata from
-- c8C3BF576.fC8CEF9EF) has only four methods, none used for the volume numbers. In the script, the functions that
-- show texts with names (picnic and box messages, trainer messages, the name input) call into the native tables
-- below right before fetching the text; one of them should register a value for a placeholder. Every function of
-- these tables is wrapped and its first eight calls are logged with their arguments (strings cut to 24 characters,
-- other values by type) and results.
if rawget(_G, "KARPUR_TEXT_PROBE") then return "text_probe: already set in this Lua state" end
rawset(_G, "KARPUR_TEXT_PROBE", true)

local TABLES = { "c4AA228B4", "cB3DDDC2A", "c20B491A2", "cE22CD062", "c34EB7997", "c535A0125" }

local function show(v)
  local t = type(v)
  if t == "string" then return '"' .. v:sub(1, 24) .. '"' end
  if t == "number" or t == "boolean" then return tostring(v) end
  if t == "table" and type(rawget(v, "length")) == "number" then return "array " .. v.length end
  return t
end

local calls, report = {}, {}
for _, tname in ipairs(TABLES) do
  local tbl = rawget(_G, tname)
  local names = {}
  if type(tbl) == "table" then
    for k, fn in pairs(tbl) do
      if type(fn) == "function" then
        names[#names + 1] = tostring(k)
        local label = tname .. "." .. tostring(k)
        pcall(rawset, tbl, k, function(...)
          local n = (calls[label] or 0) + 1
          calls[label] = n
          if n > 8 then return fn(...) end
          local res = table.pack(fn(...))
          pcall(function(...)
            local a, parts, r = table.pack(...), {}, {}
            for i = 1, a.n do parts[#parts + 1] = show(a[i]) end
            for i = 1, res.n do r[#r + 1] = show(res[i]) end
            print(string.format("KARPUR text %s (%d)(%s) -> %s", label, n, table.concat(parts, ", "),
                                table.concat(r, ", ")))
          end, ...)
          return table.unpack(res, 1, res.n)
        end)
      end
    end
  end
  table.sort(names)
  report[#report + 1] = tname .. ": " .. (type(tbl) == "table" and table.concat(names, " ") or "missing")
end
return "text_probe:\n" .. table.concat(report, "\n")
