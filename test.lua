utils = {}

function utils.keys(t, fsort)
	local res = {}
	local oktypes = { string = true, number = true }
	local function cmpfct(a,b)
		if type(a) == type(b) and oktypes[type(a)] then
			return a < b
		else
			return type(a) < type(b)
		end
	end
	for k in pairs(t) do
		res[#res+1] = k
	end
	if fsort then
		table.sort(res, cmpfct)
	end
	return res
end

function utils.get(data)
	if #data > 260 then return data end
	local prefix,name = data:match('^([A-Z][A-Z][A-Z][A-Z]?[A-Z]?):"?(.-)"?$')
	if prefix == 'FILE' then
		local file = assert(io.open(name, 'rb'))
		data = file:read("*a")
		file:close()
	elseif prefix == 'RAW' then
		data = name
	elseif #data > 10 and data:match("[%/%\\_%w%: %.]+%.%w%w%w+$") and lfs.attributes(data) then
		printf("Warning: The string '%s' is a valid file name, but will be interpreted as raw data. Did you forget the 'FILE:' prefix?", data)
	end
	return data
end

function utils.put(data, filename)
	local prefix,name = filename:match('^([A-Z][A-Z][A-Z][A-Z]?[A-Z]?):(.*)')
	local file
	if prefix == 'FILE' then
		filename = name
		file = assert(io.open(filename, 'wb'))
		file:write(utils.get(data))
		file:close()
	end
end

function utils.dump(t, level, ident, defined)
	level = level or 20
	ident = ident or 0
	defined = defined or {}
	if level <= 0 then return end
	local tabs = string.rep("  ", ident)
	if defined[t] then
		print(tabs.."(Loop detected)")
		return
	end
	defined[t] = true
	for key,value in pairs(t) do
		if type(value) == 'table' then
			print(tabs..key..": [Table]")
			utils.dump(value, level-1, ident+1, defined)
		else
			print(tabs..key..": "..tostring(value))
		end
	end
	defined[t] = nil
end

local dumplua_closure = [[
local closures = {}
local function closure(t)
	closures[#closures+1] = t
	t[1] = assert(loadstring(t[1]))
	return t[1]
end

for _,t in pairs(closures) do
	for i = 2,#t do
		debug.setupvalue(t[1], i-1, t[i])
	end
end
]]
local c_functions = {}
local lua_reserved_keywords = {
	'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for',
	'function', 'if', 'in', 'local', 'nil', 'not', 'or', 'repeat',
	'return', 'then', 'true', 'until', 'while' }

for _,lib in pairs{'_G', 'string', 'table', 'math', 'readline', 'history', 'graph',
	'io', 'os', 'coroutine', 'package', 'debug', 'bit', 'zip', 'unzip', 'lfs'} do
	local t = _G[lib] or {}
	lib = lib .. "."
	if lib == "_G." then lib = "" end
	for k,v in pairs(t) do
		if type(v) == 'function' then -- and not pcall(string.dump, v) then
			c_functions[v] = lib..k
		end
	end
end

function utils.dumplua(value, varname, mode, ident)
	local type_lut = { t = 'table', s = 'string', n = 'number', b = 'boolean', 
		l = 'nil', f = 'function', u = 'userdata', h = 'thread' }
	-- Local variables for speed optimization
	local string_format, type, string_dump, string_rep, tostring, pairs, table_concat =
	      string.format, type, string.dump, string.rep, tostring, pairs, table.concat
	local keycache, strvalcache, out, closure_cnt = {}, {}, {}, 0
	local defined, fcts, dumplua = {}, {}, nil
	setmetatable(strvalcache, {__index = function(t,value)
		local res = string_format('%q', value)
		t[value] = res
		return res
	end})
	for k,v in pairs(type_lut) do
		fcts[k..0] = function() error(string_format("Cannot dump %ss", v)) end
		fcts[k..1] = function(value) return tostring(value) end
	end
	fcts.s2 = function(value) return strvalcache[value] end
	fcts.f2 = function(value) return string_format("loadstring(%q)", string_dump(value)) end

	local function test_defined(value, path)
		if defined[value] then
			if path:match("^getmetatable.*%)$") then
				out[#out+1] = string_format("s%s, %s)\n", path:sub(2,-2), defined[value])
			else
				out[#out+1] = path .. " = " .. defined[value] .. "\n"
			end
			return true
		end
		defined[value] = path
	end
	local function make_key(t, key)
		local s
		if type(key) == 'string' and key:match('^[_%a][_%w]*$') then
			s = key .. "="
		else
			s = "[" .. dumplua(key, 0) .. "]="
		end
		t[key] = s
		return s
	end
	for _,k in ipairs(lua_reserved_keywords) do
		keycache[k] = '["'..k..'"] = '
	end
	fcts.t2 = function (value)
		-- Table value
		local numidx = 1
		out[#out+1] = "{"
		for key,val in pairs(value) do
			if key == numidx then
				numidx = numidx + 1
			else
				out[#out+1] = keycache[key]
			end
			local str = dumplua(val)
			out[#out+1] = str..","
		end
		out[#out+1] = "}"
		return ""
	end
	fcts.t3 = function (value, ident, path)
		if test_defined(value, path) then return "nil" end
		-- Table value
		local sep, str, numidx, totallen = " ", {}, 1, 0
		for _,key in pairs(utils.keys(value, true)) do
			local val = value[key]
			local s = ""
			local subpath = path
			if key == numidx then
				subpath = subpath .. "[" .. numidx .. "]"
				numidx = numidx + 1
			else
				s = keycache[key]
				if not s:match "^%[" then subpath = subpath .. "." end
				subpath = subpath .. s:gsub("%s*=%s*$","")
			end
			s = s .. dumplua(val, ident+1, subpath)
			str[#str+1] = s
			totallen = totallen + #s + 2
		end
		if totallen > 80 then
			sep = "\n" .. string_rep("  ", ident+1)
		end
		str = "{" .. sep .. table_concat(str, ","..sep) .. " " .. sep:sub(1,-3) .. "}"
		return str
	end
	fcts.t4 = function (value, ident, path)
		if test_defined(value, path) then return "nil" end
		-- Table value
		local sep, str, numidx, totallen = " ", {}, 1, 0
		local meta, metastr = (debug or _G).getmetatable(value)
		if meta then
			ident = ident + 1
			metastr = dumplua(meta, ident, "getmetatable("..path..")")
			totallen = totallen + #metastr + 16
		end
		for _,key in pairs(utils.keys(value, true)) do
			local val = value[key]
			local s = ""
			local subpath = path
			if key == numidx then
				subpath = subpath .. "[" .. numidx .. "]"
				numidx = numidx + 1
			else
				s = keycache[key]
				if not s:match "^%[" then subpath = subpath .. "." end
				subpath = subpath .. s:gsub("%s*=%s*$","")
			end
			s = s .. dumplua(val, ident+1, subpath)
			str[#str+1] = s
			totallen = totallen + #s + 2
		end
		if totallen > 80 then
			sep = "\n" .. string_rep("  ", ident+1)
		end
		str = "{" .. sep .. table_concat(str, ","..sep) .. " " .. sep:sub(1,-3) .. "}"
		if meta then
			sep = sep:sub(1,-3)
			return "setmetatable(" .. sep .. str .. "," .. sep .. metastr .. sep:sub(1,-3) .. ")"
		end
		return str
	end
	fcts.f3 = function (value, ident, path)
		if test_defined(value, path) then return "nil" end
		if c_functions[value] then
			return c_functions[value]
		elseif debug.getupvalue(value, 1) == nil then
			return string_format("loadstring(%q)", string_dump(value))
		end
		closure_cnt = closure_cnt + 1
		local res = {string.dump(value)}
		for i = debug and 1 or math.huge,1e9 do
			local name, v = debug.getupvalue(value,i)
			if name == nil then break end
			res[i+1] = v
		end
		return "closure " .. dumplua(res, ident, "closures["..closure_cnt.."]")
	end

	mode = "t4s2n1b1l1f3u1h1g2" .. (mode or "")
	for s,l in mode:gmatch("(([tsnblfuh])%d)") do
		fcts[type_lut[l]] = fcts[s]
	end

	function dumplua(value, ident, path)
		return fcts[type(value)](value, ident, path)
	end
	if varname == nil then
		varname = "return "
	elseif varname:match("^[%a_][%w_]*$") then
		varname = varname .. " = "
	end
	if mode:match("g1") then
		setmetatable(keycache, {__index = make_key })
		out[1] = varname
		table.insert(out,dumplua(value, 0))
		return table_concat(out)
	else
		setmetatable(keycache, {__index = make_key })
		local items = {}
		for i=1,10 do items[i] = '' end
		items[3] = dumplua(value, ident or 0, "t")
		if closure_cnt > 0 then
			items[1], items[6] = dumplua_closure:match("(.*\n)\n(.*)")
			out[#out+1] = ""
		end
		if #out > 0 then
			items[2] = "local t = "
			items[4] = "\n"
			items[5] = table_concat(out)
			items[7] = varname .. "t"
		else
			items[2] = varname
		end
		return table_concat(items)
	end
end

function utils.hexdump(data)
	local str, format, a, b
	if type(data) == 'string' then
		for i=1,#data,16 do
			str = data:sub(i,i+15)
			format = string.rep("%02X ", #str)
			a = format:format(str:byte(1,-1))
			b = str:gsub("[\1-\31%z]", ".")
			printf("%04X   %-24s  %-24s  %-16s", i-1, a:sub(1,24), a:sub(25,-1), b)
		end
	elseif type(data) == 'table' then
		local m, nibbles, nb_line = 0, 8, 4
		for i=1,#data do
			m = math.max(m, math.abs(data[i]))
		end
		if m < 2^8 then
			nibbles, nb_line = 2, 16
		elseif m < 2^16 then
			nibbles, nb_line = 4, 8
		end
		for i=1,#data,nb_line do
			str = { table.unpack(data, i, i+nb_line-1) }
			format = string.rep("%0"..nibbles.."X ", #str)
			a = format:format(table.unpack(str))
			printf("%04X   %s", i-1, a)
		end
	end
end


p = cl.platforms()
info = {}
for i=1,#p do
	info[i] = p[1].info()
	local d = p[1].devices()
	info[i].devices = {}
	for j=1,#d do
		info[i].devices[j] = d[j].info()
	end
end
print(utils.dumplua(info))
utils.put(utils.dumplua(info), 'FILE:c:/temp/luaCL/out.txt')

ctx = cl.context(p[1].devices())
