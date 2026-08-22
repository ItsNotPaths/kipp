-- kipp parsing in Lua. Reads lines on stdin:
--   socat -u UNIX-CONNECT:/tmp/kipp-demo.sock - | lua read.lua

local function parse(line)
	local kind, subj, attr = nil, {}, {}
	for w in line:gmatch("[^\t]+") do
		if not kind then
			if w:sub(1, 1) ~= "@" then kind = w end   -- @ is reserved
		else
			local k, v = w:match("^([%w_]+)=(.*)$")
			if k then attr[k] = v else subj[#subj + 1] = w end
		end
	end
	return kind, subj, attr
end

for line in io.lines() do
	local kind, subj, attr = parse(line)
	if kind == "tag" then
		print(("tag %s on %s is %s"):format(subj[2], subj[1], attr.state))
	elseif kind == "net" then
		print(("net %s ssid=%s signal=%s"):format(subj[1], attr.ssid, attr.signal))
	elseif kind == "sync" then
		print("--- state complete ---")
	end
	-- every other kind falls through and is skipped
end
