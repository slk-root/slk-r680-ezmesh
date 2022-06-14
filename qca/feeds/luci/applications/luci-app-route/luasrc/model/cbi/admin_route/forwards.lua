-- Copyright 2008 Steven Barth <steven@midlink.org>
-- Copyright 2010-2012 Jo-Philipp Wich <jow@openwrt.org>
-- Licensed to the public under the Apache License 2.0.

local ds = require "luci.dispatcher"
local ft = require "luci.tools.firewall"
--Firewall - 
m = Map("redirect", translate("Port Forwards"),
	translate("Completely forward the communication sent to a port of the external network\
	to a designated port of an address of the internal network."))

--
-- Port Forwards
--

s = m:section(TypedSection, "redirect", translate("Port Forwards"))
s.template  = "cbi/tblsection"
s.addremove = true
s.anonymous = true
s.sortable  = true
--s.extedit   = ds.build_url("admin/routingsettings/dmz/%s")
s.template_addremove = "firewall/cbi_addforward"

function s.create(self, section)
	--[[--]]
	local n = m:formvalue("_newfwd.name")
	local p = m:formvalue("_newfwd.proto")
	local E = m:formvalue("_newfwd.extzone")
	local e = m:formvalue("_newfwd.extport")
	local I = m:formvalue("_newfwd.intzone")
	local a = m:formvalue("_newfwd.intaddr")
	local i = m:formvalue("_newfwd.intport")

	if p == "other" or (p and a) then
		created = TypedSection.create(self, section)
		self.map:set(created, "target",    "DNAT")
		self.map:set(created, "src",       E or "wan")
		self.map:set(created, "dest",      I or "lan")
		self.map:set(created, "proto",     (p ~= "other") and p or "all")
		self.map:set(created, "src_dport", e)
		self.map:set(created, "dest_ip",   a)
		self.map:set(created, "dest_port", i)
		self.map:set(created, "name",      n)
	end

	if p ~= "other" then
		created = nil
	end
end

function s.parse(self, ...)
	TypedSection.parse(self, ...)
	if created then
		m.uci:save("firewall")
		luci.http.redirect(ds.build_url(
			"admin/routingsettings/dmz", created
		))
	end
end

function s.filter(self, sid)
	return (self.map:get(sid, "target") ~= "SNAT")
end


ft.opt_name(s, DummyValue, translate("Name"))


local function forward_proto_txt(self, s)
	return "%s-%s" %{
		translate("IPv4"),
		ft.fmt_proto(self.map:get(s, "proto"),
	                 self.map:get(s, "icmp_type")) or "TCP+UDP"
	}
end

local function forward_src_txt(self, s)
	local z = ft.fmt_zone(self.map:get(s, "src"), translate("any zone"))
	local a = ft.fmt_ip(self.map:get(s, "src_ip"), translate("any host"))
	local p = ft.fmt_port(self.map:get(s, "src_port"))
	local m = ft.fmt_mac(self.map:get(s, "src_mac"))

	if p and m then
		return translatef("From %s in %s with source %s and %s", a, z, p, m)
	elseif p or m then
		return translatef("From %s in %s with source %s", a, z, p or m)
	else
		return translatef("From %s in %s", a, z)
	end
end

local function forward_via_txt(self, s)
	local a = ft.fmt_ip(self.map:get(s, "src_dip"), translate("any router IP"))
	local p = ft.fmt_port(self.map:get(s, "src_dport"))

	if p then
		return translatef("Via %s at %s", a, p)
	else
		return translatef("Via %s", a)
	end
end

protos = s:option(DummyValue, "protos", translate("Protocol"))
protos.rawhtml = true
protos.width   = "20%"
function protos.cfgvalue(self, s)
	return "<small>%s</small>" % {
		forward_proto_txt(self, s)
	}
end

match = s:option(DummyValue, "match", translate("External"))
match.rawhtml = true
match.width   = "50%"
function match.cfgvalue(self, s)
	local p = ft.fmt_port(self.map:get(s, "src_dport"))
	if p then
		return translatef("%s",p)
	else
		return translatef("%s","-")
	end
end

dest = s:option(DummyValue, "dest", translate("Forward to internal"))
dest.rawhtml = true
dest.width   = "40%"
function dest.cfgvalue(self, s)
	local z = ft.fmt_zone(self.map:get(s, "dest"), translate("any zone"))
	local a = ft.fmt_ip(self.map:get(s, "dest_ip"), translate("any host"))
	local p = ft.fmt_port(self.map:get(s, "dest_port")) or
		ft.fmt_port(self.map:get(s, "src_dport"))

	if p then
		return translatef("%s, %s", a, p)
	else
		return translatef("%s", a)
	end
end

ft.opt_enabled(s, Flag, translate("Enable")).width = "1%"

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
end

return m
