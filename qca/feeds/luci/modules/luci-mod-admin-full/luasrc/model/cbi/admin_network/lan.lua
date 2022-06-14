local sys   = require "luci.sys"
local m, s, m2 ,s2

m = Map("network", translate("Network Configuration"))
m:chain("luci")

s = m:section(NamedSection, "lan", "interface", translate("LAN Configuration"))
s.anonymous = true
s.addremove = false


local function set_status(self)
		st.template = "admin_network/iface_status"
		st.network  = self
		st.value    = nil
end

st = s:option(DummyValue, "__statuswifi", translate("Status"))
st.on_init = set_status("lan")

ipaddr = s:option(Value, "ipaddr", translate("IP Address"))
ipaddr.datatype = "ip4addr"
ipaddr.default = "192.168.2.1"

netmask = s:option(Value, "netmask",translate("Netmask"))
netmask.datatype = "ip4addr"
netmask.rmempty=false
netmask:value("255.255.255.0")
netmask:value("255.255.0.0")
netmask:value("255.0.0.0")

gateway = s:option(Value, "gateway", translate("IPv4 gateway"))
gateway.datatype = "ip4addr"

dns = s:option(DynamicList, "dns",translate("DNS server"))
dns.datatype = "ipaddr"
dns.default = "0.0.0.0"
dns.cast     = "string"

mtu = s:option(Value, "mtu", translate("Override MTU"))
mtu.placeholder = "1500"
mtu.datatype    = "max(9200)"

local apply = luci.http.formvalue("cbi.apply")
if apply then
    io.popen("/etc/init.d/network restart")
end

return m
