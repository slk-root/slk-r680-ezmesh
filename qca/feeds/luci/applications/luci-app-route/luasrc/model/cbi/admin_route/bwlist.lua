--[[
LuCI - Lua Configuration Interface

LuCI is a personal plaything which leads our developer
to be conflused and anxious.

]]--
local ft = require "luci.tools.firewall"

local m, s, o, n
--translate("Firewall") .. " - " .. "DMZ",
m = Map("bwlist",
	translate("Black & White List"),
	translate("By filtering IP addresses and MAC addresses,\
	black and white lists can help manage the network connection status of access devices."))

s1 = m:section(TypedSection, "bwlist", translate("Mode Configuration"))
s1.addremove = false
s1.anonymous = true

o = s1:option(Flag, "enabled", translate("Enable"))

o = s1:option(ListValue, "mode", translate("Mode"), translate("White List:Only allow devices in the following list to connect to the Internet.<br />")..
translate("Black List:Devices in the following list are prohibited from connecting to the Internet."))
o:value("white", translate("White List"))
o:value("black", translate("Black List"))

s2 = m:section(TypedSection, "rule", translate("Name List"))
s2.addremove = true
s2.anonymous = true
s2.sortable  = true
s2.template = "cbi/tblsection"
s2.template_addremove = "firewall/cbi_addlist"

function s2.create(self, section)
	local n = m:formvalue("_newlist.name")
	local p = m:formvalue("_newlist.proto")
	local si = m:formvalue("_newlist.src_ip")
	local sm = m:formvalue("_newlist.src_mac")
	local di = m:formvalue("_newlist.des_ip")
	local a = m:formvalue("_newlist.action")
	local i = m:formvalue("_newlist.icmp_type")

	if p == "other" or (p and si) or (p and sm) then
		created = TypedSection.create(self, section)
		self.map:set(created, "target",    a)
		self.map:set(created, "src",       "lan")
		self.map:set(created, "dest",      "wan")
		self.map:set(created, "proto",     (p ~= "other") and p or "all")
		self.map:set(created, "src_ip", si)
		self.map:set(created, "src_mac", sm or "")
		self.map:set(created, "dest_ip",   di or "")
		self.map:set(created, "icmp_type",   i or "")
		self.map:set(created, "name",      n)
	end

	if p ~= "other" then
		created = nil
	end
end

function s2.parse(self, ...)
	TypedSection.parse(self, ...)

	if created then
		m.uci:save("firewall")
		luci.http.redirect(ds.build_url(
			"admin/admin/route/bwlist", created
		))
	end
end

ft.opt_name(s2, DummyValue, translate("Name"))

local function rule_proto_txt(self, s2)
	local f = self.map:get(s2, "family")
	local p = ft.fmt_proto(self.map:get(s2, "proto"),
	                       self.map:get(s2, "icmp_type")) or translate("All")

	if f and f:match("4") then
		return "%s-%s" %{ translate("IPv4"), p }
	elseif f and f:match("6") then
		return "%s-%s" %{ translate("IPv6"), p }
	else
		return "%s" %{ p }
	end
end

local function rule_src_txt(self, s2)
	local z = ft.fmt_zone(self.map:get(s2, "src"), translate("any zone"))
	local a = ft.fmt_ip(self.map:get(s2, "src_ip"), translate("any host"))
	local p = ft.fmt_port(self.map:get(s2, "src_port"))
	local m = ft.fmt_mac(self.map:get(s2, "src_mac"))

	-- if p and m then
		-- return translatef("From %s in %s with source %s and %s", a, z, p, m)
	-- elseif p or m then
		-- return translatef("From %s in %s with source %s", a, z, p or m)
	-- else
		-- return translatef("From %s in %s", a, z)
	-- end
	if m then
		return translatef("%s", m)
	elseif a then
		return translatef("%s", a)
	end
end

local function rule_dest_txt(self, s2)
	local z = ft.fmt_zone(self.map:get(s2, "dest"))
	local p = ft.fmt_port(self.map:get(s2, "dest_port"))

	-- Forward
	if z then
		local a = ft.fmt_ip(self.map:get(s2, "dest_ip"), translate("any host"))
		-- if p then
			-- return translatef("To %s, %s in %s", a, p, z)
		-- else
			-- return translatef("To %s in %s", a, z)
		-- end
		return translatef("%s",a)

	-- Input
	else
		local a = ft.fmt_ip(self.map:get(s2, "dest_ip"),
			translate("any router IP"))

		-- if p then
			-- return translatef("To %s at %s on <var>this device</var>", a, p)
		-- else
			-- return translatef("To %s on <var>this device</var>", a)
		-- end
		return translatef("%s on <var>this device</var>",a)
	end
end

local function snat_dest_txt(self, s2)
	local z = ft.fmt_zone(self.map:get(s2, "dest"), translate("any zone"))
	local a = ft.fmt_ip(self.map:get(s2, "dest_ip"), translate("any host"))
	local p = ft.fmt_port(self.map:get(s2, "dest_port")) or
		ft.fmt_port(self.map:get(s2, "src_dport"))

	-- if p then
		-- return translatef("To %s, %s in %s", a, p, z)
	-- else
		-- return translatef("To %s in %s", a, z)
	-- end
	return translatef("%s",a)
end

protos = s2:option(DummyValue, "protos", translate("Protocol"))
protos.rawhtml = true
protos.width   = "20%"
function protos.cfgvalue(self, s2)
	return "<small>%s</small>" % {
		rule_proto_txt(self, s2)
	}
end

source = s2:option(DummyValue, "source", translate("Local"))
source.rawhtml = true
source.width   = "40%"
function source.cfgvalue(self, s2)
	-- return "<small>%s<br />%s<br />%s</small>" % {
		-- rule_proto_txt(self, s2),
		-- rule_src_txt(self, s2),
		-- rule_dest_txt(self, s2)
	-- }
	return "<small>%s</small>" % {
		--rule_proto_txt(self, s2),
		rule_src_txt(self, s2)
		--rule_dest_txt(self, s2)
	}
end

dest = s2:option(DummyValue, "dest", translate("Destination"))
dest.rawhtml = true
dest.width   = "40%"
function dest.cfgvalue(self, s2)
	-- return "<small>%s<br />%s<br />%s</small>" % {
		-- rule_proto_txt(self, s2),
		-- rule_src_txt(self, s2),
		-- rule_dest_txt(self, s2)
	-- }
	return "<small>%s</small>" % {
		--rule_src_txt(self, s2),
		rule_dest_txt(self, s2)
	}
end

target = s2:option(DummyValue, "target", translate("Action"))
target.rawhtml = true
target.width   = "20%"
function target.cfgvalue(self, s2)
	local t = ft.fmt_target(self.map:get(s2, "target"), self.map:get(s2, "dest"))
	local l = ft.fmt_limit(self.map:get(s2, "limit"),
		self.map:get(s2, "limit_burst"))

	if l then
		return translatef("<var>%s</var> and limit to %s", t, l)
	else
		return translatef("<var>%s</var>", t)
	end
end

ft.opt_enabled(s2, Flag, translate("Enable")).width = "1%"

function fork_exec(command)
	local pid = nixio.fork()
	if pid > 0 then
		return
	elseif pid == 0 then
		-- change to root dir
		nixio.chdir("/")

		-- patch stdin, out, err to /dev/null
		local null = nixio.open("/dev/null", "w+")
		if null then
			nixio.dup(null, nixio.stderr)
			nixio.dup(null, nixio.stdout)
			nixio.dup(null, nixio.stdin)
			if null:fileno() > 2 then
				null:close()
			end
		end

		-- replace with target command
		nixio.exec("/bin/sh", "-c", command)
	end
end

local apply=luci.http.formvalue("cbi.apply")
if apply then
	fork_exec("/etc/init.d/firewallon start")
	luci.http.redirect(luci.dispatcher.build_url("admin/route/bwlist"))
end

return m