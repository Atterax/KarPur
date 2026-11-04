-- KarPur: what the game's options page holds and calls when it opens, as a report of names and numbers, to reuse
-- the page for the PC settings (docs/progress.md 2026-11-04). Run with TOTK_LUA_MOD=1
-- TOTK_LUA_MOD_FILE=C:\KarPur\repo\runtime\lua\options_probe.lua, then open X menu > Options once.
-- find_options.lua named the classes: C9813692A25E9E386 (the page; its constructor builds the entries with numId,
-- startNum, defaultId, menuName, infoName, selectMax, detailSelect, buttonType), C0FD26BFB981E066D (uses selectMax:
-- a row?), C326BD36502E320DE (opens view_option_00), C53C7322A79AC43BA and C3EE83BE9E031B7C8 (dialogs).
-- Logged: each method of these classes the first three times it is called; the entry list after the constructor;
-- and (run 3) the calls into the native tables (globals c + 8 hex digits) inside the methods that change a value or
-- close the page, each followed by the page's numbers and every entry's numId: change a value, close the page and
-- confirm saving, to see where the values live and how they get saved.
if rawget(_G, "KARPUR_OPTIONS_PROBE") then return "options_probe: already set in this Lua state" end
rawset(_G, "KARPUR_OPTIONS_PROBE", true)

local CLASSES = { "C9813692A25E9E386", "C0FD26BFB981E066D", "C326BD36502E320DE", "C53C7322A79AC43BA",
                  "C3EE83BE9E031B7C8" }
local PAGE = "C9813692A25E9E386"

local class_name = {}
for name, v in pairs(_G) do
  if type(name) == "string" and type(v) == "table" and rawget(v, "prototype") then class_name[v] = name end
end

local function show(v)
  local t = type(v)
  if t == "string" then return '"' .. v:sub(1, 48) .. '"' end
  if t == "number" or t == "boolean" then return tostring(v) end
  if t == "table" then
    local len = rawget(v, "length")
    if type(len) == "number" then return "array " .. len end
    local mt = getmetatable(v)
    local cls = rawget(v, "__class__") or (mt and type(mt.__index) == "table" and rawget(mt.__index, "__class__"))
    if class_name[cls] then return "object " .. class_name[cls] end
  end
  return t
end

local lines = 0
local function dump(v, indent, depth)
  if lines > 300 then return end
  lines = lines + 1
  print("KARPUR page " .. indent .. show(v))
  if type(v) ~= "table" or depth >= 3 then return end
  local keys = {}
  for k, e in pairs(v) do if type(e) ~= "function" then keys[#keys + 1] = k end end
  table.sort(keys, function(a, b)
    if type(a) == type(b) and (type(a) == "number" or type(a) == "string") then return a < b end
    return type(a) == "number"
  end)
  for _, k in ipairs(keys) do
    lines = lines + 1
    if lines > 300 then return end
    print("KARPUR page " .. indent .. "  [" .. tostring(k) .. "]:")
    dump(rawget(v, k), indent .. "    ", depth + 1)
  end
end

local tracing, traced = 0, 0   -- depth of watched method calls: native calls are logged only inside them
for name, tbl in pairs(_G) do
  if type(name) == "string" and name:match("^c%x%x%x%x%x%x%x%x$") and type(tbl) == "table" then
    for fname, fn in pairs(tbl) do
      if type(fname) == "string" and type(fn) == "function" then
        pcall(rawset, tbl, fname, function(...)
          if tracing == 0 or traced >= 400 then return fn(...) end
          traced = traced + 1
          local res = table.pack(fn(...))
          pcall(function(...)
            local a, r, parts = table.pack(...), {}, {}
            for i = 1, a.n do parts[#parts + 1] = show(a[i]) end
            for i = 1, res.n do r[#r + 1] = show(res[i]) end
            print(string.format("KARPUR trace %s.%s(%s) -> %s", name, fname, table.concat(parts, ", "),
                                table.concat(r, ", ")))
          end, ...)
          return table.unpack(res, 1, res.n)
        end)
      end
    end
  end
end

local TRACE = {}
for _, m in ipairs({ "F58FE3DC34D491A25", "F6A8DB7A4AC4B16F3", "F1F6114A4533DE974", "FE0EBB34B8A1B41AB",
                     "F20A49010A4198793", "F218E2A14A790D265", "F1C2AA00ADAC52EC5", "F7C68FEDB79AB6396" }) do
  TRACE[PAGE .. "." .. m] = true
end
for _, m in ipairs({ "super", "F8ECAA28B9538110D", "F4511F94555C77C3A" }) do TRACE["C3EE83BE9E031B7C8." .. m] = true end

-- After a traced method of the page: the page's own numbers and booleans, and numId of every entry.
local function values(label, self)
  if type(self) ~= "table" then return end
  local own, ids = {}, {}
  for k, v in pairs(self) do
    if type(v) == "number" or type(v) == "boolean" then own[#own + 1] = tostring(k) .. "=" .. tostring(v) end
  end
  table.sort(own)
  local list = rawget(self, 12)
  for i = 0, (type(list) == "table" and rawget(list, "length") or 0) - 1 do
    local e = rawget(list, i)
    ids[#ids + 1] = tostring(type(e) == "table" and rawget(e, "numId"))
  end
  print(string.format("KARPUR values after %s: page %s; numId %s", label, table.concat(own, " "),
                      table.concat(ids, " ")))
end

local calls = {}
local function wrap(owner, key, label, after)
  local fn = rawget(owner, key)
  if type(fn) ~= "function" then return 0 end
  rawset(owner, key, function(...)
    local n = (calls[label] or 0) + 1
    calls[label] = n
    if n <= 3 then
      local a, parts = table.pack(...), {}
      for i = 1, a.n do parts[#parts + 1] = show(a[i]) end
      print(string.format("KARPUR call %s (%d)(%s)", label, n, table.concat(parts, ", ")))
    end
    -- run 3: native calls only inside the methods that change a value and that close the page (runs 1 and 2 showed
    -- the opening), to find where the page keeps its values and how it saves them
    local watch = TRACE[label] and calls[label] <= 3
    if watch then tracing = tracing + 1 end
    local res = table.pack(pcall(fn, ...))
    if watch then tracing = tracing - 1 end
    if not res[1] then error(res[2], 0) end
    if after then pcall(after, ...) end
    if TRACE[label] and calls[label] <= 3 then pcall(values, label, ...) end
    return table.unpack(res, 2, res.n)
  end)
  return 1
end

local wrapped = 0
for _, cname in ipairs(CLASSES) do
  local cls = rawget(_G, cname)
  if type(cls) == "table" then
    -- after the page's constructor: its entry list [12], one line per entry
    local after = cname == PAGE and function(self)
      local list = rawget(self, 12)
      for i = 0, (type(list) == "table" and rawget(list, "length") or 0) - 1 do
        local e = rawget(list, i)
        local sel, labels = rawget(e, "detailSelect"), {}
        for j = 0, (type(sel) == "table" and rawget(sel, "length") or 0) - 1 do labels[#labels + 1] = show(rawget(sel, j)) end
        print(string.format("KARPUR entry %d: %s info %s numId %s startNum %s defaultId %s selectMax %s buttonType %s: %s",
                            i, show(e.menuName), show(e.infoName), show(e.numId), show(e.startNum), show(e.defaultId),
                            show(e.selectMax), show(e.buttonType), table.concat(labels, ", ")))
      end
    end or nil
    wrapped = wrapped + wrap(cls, "super", cname .. ".super", after)
    local proto = rawget(cls, "prototype")
    if type(proto) == "table" then
      local keys = {}
      for k, v in pairs(proto) do if type(v) == "function" then keys[#keys + 1] = k end end
      for _, k in ipairs(keys) do wrapped = wrapped + wrap(proto, k, cname .. "." .. k) end
    end
  end
end
return "options_probe: " .. wrapped .. " methods watched"
