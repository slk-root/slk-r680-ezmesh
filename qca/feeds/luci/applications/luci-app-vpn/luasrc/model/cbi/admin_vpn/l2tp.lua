-- Copyright 2008 Steven Barth <steven@midlink.org>
-- Licensed to the public under the Apache License 2.0.

require("luci.ip")
require("luci.model.uci")

local function set_status(self)
	st.template = "admin_network/iface_status"
	st.network  = self
	st.value    = nil
end

local m = Map("network",
				translate("L2TP Client"), 
				translate("Configurable L2TP access to VPN."))

local s = m:section( NamedSection, "l2tp", "interface" )

st = s:option(DummyValue, "__status", translate("Status"))
st.on_init = set_status("l2tp")

enable = s:option(Flag, "auto", translate("Enable"))
enable.rmempty  = false
severs=s:option(Value,"server",translate("Server Address"))
username=s:option(Value,"username",translate("Username"))
password=s:option(Value,"password",translate("Password"))
password.password=true
metric=s:option(Value,"metric",translate("Metric"),translate("Configure the priority of this network"))
metric.default=9

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

-- function m.on_after_commit(map)
	-- fork_exec("/etc/init.d/vpn_init stop re_l2tp")
	-- fork_exec("/etc/init.d/vpn_init start re_l2tp &")
-- end
return m
