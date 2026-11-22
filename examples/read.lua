-- kipp parsing in Lua. Reads lines on stdin:
--   printf 'connect\tread_lua\nsubscribe\t*\n' |
--     socat - UNIX-CONNECT:"$XDG_RUNTIME_DIR/kippsrv.sock" | lua read.lua

local function parse(line)
	local verb, topic, fields = nil, nil, {}
	for word in line:gmatch("[^\t]+") do
		if not verb then
			verb = word
		elseif not topic then
			topic = word
		else
			local key, value = word:match("^([%l%d_]+)=(.*)$")
			if key then fields[key] = value end   -- not key=value: skip it
		end
	end
	return verb, topic, fields
end

for line in io.lines() do
	local verb, topic, fields = parse(line)
	local kind, rest = (topic or ""):match("^([^/]*)/?(.*)$")
	if verb == "retain" and kind == "tag" then
		print(("tag %s is %s"):format(rest, fields.state or "gone"))
	elseif verb == "retain" and kind == "net" then
		print(("net %s ssid=%s signal=%s"):format(rest, fields.ssid, fields.signal))
	elseif verb == "sync" then
		print("--- " .. topic .. " complete ---")
	end
	-- every other verb and kind falls through and is skipped
end
