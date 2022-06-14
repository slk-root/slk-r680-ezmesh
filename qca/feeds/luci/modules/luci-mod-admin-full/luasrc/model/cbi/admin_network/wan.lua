local sys   = require "luci.sys"
local m, s

m = Map("network", translate("Network Configuration"))
m:chain("luci")

s = m:section(NamedSection, "wan", "interface", translate("WAN Configuration"))
s.anonymous = true
s.addremove = false
s:tab("basic", translate("Basic Configuration"))
s:tab("advanced", translate("Advanced Configuration"))

local function set_status(self)
	st.template = "admin_network/iface_status"
	st.network  = self
	st.value    = nil
end

st = s:taboption("basic", DummyValue, "__statuswan", translate("Status"))
st.on_init = set_status("wan")


--static
protocol = s:taboption("basic", ListValue, "proto", translate("Protocol"))
protocol.default = "dhcp"
protocol:value("pppoe", translate("PPPoE"))
protocol:value("dhcp", translate("DHCP address"))
protocol:value("static", translate("Static address"))
protocol:value("aslan", translate("As lan"))
protocol:value("disable", translate("Disable"))

ipaddr = s:taboption("basic", Value, "ipaddr", translate("IP Address"))
ipaddr.datatype = "ip4addr"
ipaddr.default = "192.168.1.100"
ipaddr:depends({proto="static"})

netmask = s:taboption("basic", Value, "netmask",translate("Netmask"))
netmask.datatype = "ip4addr"
netmask.default = "255.255.255.0"
netmask:value("255.255.255.0")
netmask:value("255.255.0.0")
netmask:value("255.0.0.0")
netmask:depends({proto="static"})

gateway= s:taboption("basic", Value, "gateway", translate("Gateway"))
gateway.datatype = "ip4addr"
gateway:depends({proto="static"})

dns= s:taboption("basic", DynamicList, "dns", translate("DNS"))
dns.datatype = "ip4addr"
dns.cast     = "string"
dns:depends({proto="static"})


-----------------------PPPoE--------------------------

username = s:taboption("basic", Value, "username",translate("Username"))
username:depends({proto="pppoe"})

password = s:taboption("basic", Value, "password",translate("Password"))
password:depends({proto="pppoe"})
password.password = true


ifname = s:taboption("advanced", ListValue, "interface", translate("Interface"), translate("If SFP(eth4) selected for the interface, WAN(eth0) will be used as lan"))
ifname:value("eth0", translate("WAN(eth0)"))
ifname:value("eth4", translate("SFP(eth4)"))
ifname.default = "eth0"

mtu = s:taboption("advanced", Value, "mtu", translate("Override MTU"))
mtu.placeholder = "1500"
mtu.datatype    = "max(9200)"

function m.on_after_commit(map)
    io.popen("/etc/init.d/netwan")
end

return m
