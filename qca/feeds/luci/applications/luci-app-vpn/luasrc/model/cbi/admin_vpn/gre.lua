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
				translate("GRE VPN"), 
				translate("Configurable GRE access to VPN."))

local s1 = m:section( NamedSection, "gre", "interface",translate("Interface information"))
local s2 = m:section( NamedSection, "gre_static", "interface",translate("Tunnel information") )


st = s1:option(DummyValue, "__status", translate("Status"))
st.on_init = set_status("gre_static")

enable = s1:option(Flag, "auto", translate("Enable"))
enable.rmempty  = false

gre_proto = s1:option(ListValue, "proto", translate("Protocol"))
gre_proto.default = "gre"
gre_proto:value("gre", translate("gre"))
gre_proto:value("gretap", translate("gretap"))

gre_ipaddr=s1:option(Value,"ipaddr",translate("Local IPv4 address"))
gre_ipaddr.datatype = "ip4addr"

gre_peeraddr=s1:option(Value,"peeraddr",translate("Remote IPv4 address"))
gre_peeraddr.datatype = "ip4addr"

--iface_name = s1:option(Value, "interface", translate("Interface"))
--iface_name.template  = "cbi/network_ifacelist"

gre_static_ipaddr=s2:option(Value,"ipaddr",translate("Local tunnel address"))
gre_static_ipaddr.datatype = "ip4addr"

gre_static_netmask=s2:option(Value,"netmask",translate("Netmask"))
gre_static_netmask.datatype = "ip4addr"

local apply = luci.http.formvalue("cbi.apply")
if apply then
    local proto = luci.http.formvalue("cbid.network.gre.proto")
	local gre_ifname = "gre-gre"
	if proto == "gre" then
		gre_ifname = "gre-gre"
	else if proto == "gretap" then
		gre_ifname = "gre-gre"
		end
	end
	luci.sys.exec("uci set network.gre_static.ifname="..gre_ifname.."")
	luci.sys.exec("uci commit network")
end


return m
