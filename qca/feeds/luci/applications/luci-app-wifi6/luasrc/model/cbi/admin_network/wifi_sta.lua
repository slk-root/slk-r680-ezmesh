-- Copyright 2012 Christian Gagneraud <chris@techworks.ie>
-- Licensed to the public under the Apache License 2.0.

local wific="sta0"

local nw = require "luci.model.network"
m = Map("wificonfig",translate("General Setup"))

s = m:section(NamedSection,wific,"wifi-iface")
s.anonymous = true

-----------------------Status--------------------------

local function set_status(self)
	st.template = "admin_network/iface_status"
	st.network  = self
	st.value    = nil
end


--local proto  = luci.sys.exec("uci get network.@internets[0].proto")
st = s:option(DummyValue, "__statuswifi", translate("Status"))
st.on_init = set_status("wific")


-----------------------WIFI Client--------------------------
enable = s:option(Flag, "en", translate("Enable"))
enable.rmempty = false

local wifiinfo=string.gsub(luci.sys.exec("uci get wificonfig."..wific..".device"),"%s+","")
infos = s:option(ListValue, "device", translate("WiFi Interface"))
infos.default=wifiinfo--string.gsub(luci.sys.exec("uci set wireless."..wnet.sid..".device"),"%s+","")
infos:depends({en="1"})
infos:value("wifi1",translate("2.4G Client"))
infos:value("wifi0",translate("5.8G Client"))

scan_btn = s:option(Button, "", translate("Scan"))
scan_btn.inputtitle = translate("Scan")
scan_btn:depends({en="1"})
local one= "wifi0";
function scan_btn.write()
one = m:formvalue("cbid.wificonfig.%s.device" % wific)
if one == "wifi0" then
	one="wifi0"
else
	one="wifi1"
end
local iw = luci.sys.wifi.getiwinfo(one)

for k, v in ipairs(iw.scanlist or { }) do
	if v.ssid then
		ssid:value(v.ssid, "%s" %{ v.ssid })
	end
end

end

ssid = s:option(Value, "ssid", translate("SSID"), translate("Click the scan button and select the wifi name to be connected"))
ssid.validator = 'maxlength="32"'
ssid.placeholder = translate("Enter or click search")--translate("SSID is required")
ssid:depends({en="1"})
ssid.default= translate("Select wifi ssid")

enc = s:option(ListValue, "security", translate("Security"))
enc:value("none", translate("No Encryption"))
enc:value("encryption", translate("Encryption"))
enc:depends({en="1"})

key = s:option(Value, "key", translate("Key"))
key.datatype = "wpakey"
key.placeholder = translate("Key")
key.password = true
key:depends("security","encryption")
key.errmsg = translate("Key can't be empty")

wds = s:option(Flag, "wds", translate("WDS"))
wds.default = "0"
wds.rmempty = false
wds.submit = true
wds:depends({en="1"})

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

local sys   = require "luci.sys"
local s, m2 ,s2

m2 = Map("wificonfig", translate("Advanced Settings"))
m2:chain("luci")

s2 = m2:section(NamedSection, "wific", "interface")
s2.anonymous = true
s2.addremove = false

proto = s2:option(ListValue, "proto", translate("Protocol"), translate("LAN port of bridge should share the same network segment with upstream equipment"))
proto:value("dhcp", translate("DHCP address"))
proto:value("static", translate("Static address"))
proto:value("bridgelan", translate("Bridge Lan"))

ipaddr = s2:option(Value, "ipaddr", translate("IP Address"))
ipaddr.datatype = "ip4addr"
ipaddr.default = "192.168.1.100"
ipaddr:depends({proto="static"})

netmask = s2:option(Value, "netmask",translate("Netmask"))
netmask.datatype = "ip4addr"
netmask.default = "255.255.255.0"
netmask:value("255.255.255.0")
netmask:value("255.255.0.0")
netmask:value("255.0.0.0")
netmask:depends({proto="static"})

gateway= s2:option(Value, "gateway", translate("Gateway"))
gateway.datatype = "ip4addr"
gateway:depends({proto="static"})

dns= s2:option(DynamicList, "dns", translate("DNS"))
dns.datatype = "ip4addr"
dns.cast     = "string"
dns:depends({proto="static"})

local apply = luci.http.formvalue("cbi.apply")
if apply then
	io.popen("/etc/init.d/wifi_init restart &")
end


return m,m2
