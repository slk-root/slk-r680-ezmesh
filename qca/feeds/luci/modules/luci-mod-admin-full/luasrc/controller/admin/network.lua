-- Copyright 2008 Steven Barth <steven@midlink.org>
-- Licensed to the public under the Apache License 2.0.

module("luci.controller.admin.network", package.seeall)

function index()
	x = uci.cursor()
	entry({"admin", "network"}, alias("admin","network","lan"), _("Network Setting"), 30).index = true	
	--entry({"admin", "network" ,"internet"}, cbi("admin_network/internet"), _("Internet Setting"), 1)
	x:foreach("network", "interface", function (s)
		if s['.name']=="wan" then
			entry({"admin", "network" ,"wan"}, cbi("admin_network/wan"), _("WAN Setting"), 3)
		end
	end)
	entry({"admin", "network" ,"lan"}, cbi("admin_network/lan"), _("LAN Setting"), 4)
	entry({"admin", "network" ,"dhcp"}, cbi("admin_network/dhcp"), _("DHCP Setting"), 5)
	if (nixio.fs.access("/etc/config/dhcp"))then
		entry({"admin", "network" ,"hosts"}, cbi("admin_network/hosts"), _("Hostnames"), 6)
	end
	if (nixio.fs.access("/etc/config/wireless"))then  
		page=entry({"admin", "network" ,"wifi_ap"}, cbi("admin_network/wifi_ap"), _("WIFI Setting"), 7)
		page=entry({"admin", "network" ,"wifi_sta"}, cbi("admin_network/wifi_sta"), _("WIFI Client"), 8)
	end
	entry({"admin", "network", "wireless_status"}, call("wifi_status"), nil).leaf = true
	entry({"admin", "network", "iface_status"}, call("iface_status"), nil).leaf = true
	entry({"admin", "network", "iface_add"}, cbi("admin_network/iface_add"), nil).leaf = true
	entry({"admin", "network", "wireless_add"}, call("wifi_add"), nil).leaf = true
	entry({"admin", "network", "autoreboot"}, cbi("admin_system/autoreboot"), _("Time Reboot"), 9)
	entry({"admin", "network" ,"diagnosis"}, template("admin_network/diagnostics"), _("Diagnosis"), 90)
	entry({"admin", "network", "diag_ping"}, call("diag_ping"), nil).leaf = true
	entry({"admin", "network", "diag_traceroute"}, call("diag_traceroute"), nil).leaf = true
	
end

function wifi_add()
	local dev = luci.http.formvalue("device")
	local ntm = require "luci.model.network".init()

	dev = dev and ntm:get_wifidev(dev)

	if dev then
		local net = dev:add_wifinet({
			mode       = "ap",
			ssid       = "slk-routers",
			encryption = "none"
		})

		ntm:save("wireless")
		luci.http.redirect(net:adminlink())
	end
end


function iface_status(ifaces)
	local netm = require "luci.model.network".init()
	local rv   = { }

	local iface
	for iface in ifaces:gmatch("[%w%.%-_]+") do
		local net = netm:get_network(iface)
		local device = net and net:get_interface()
		if device then
			local data = {
				id         = iface,
				proto      = net:proto(),
				uptime     = net:uptime(),
				gwaddr     = net:gwaddr(),
				dnsaddrs   = net:dnsaddrs(),
				name       = device:shortname(),
				type       = device:type(),
				ifname     = device:name(),
				macaddr    = device:mac(),
				is_up      = device:is_up(),
				rx_bytes   = device:rx_bytes(),
				tx_bytes   = device:tx_bytes(),
				rx_packets = device:rx_packets(),
				tx_packets = device:tx_packets(),

				ipaddrs    = { },
				ip6addrs   = { },
				subdevices = { }
			}

			local _, a
			for _, a in ipairs(device:ipaddrs()) do
				data.ipaddrs[#data.ipaddrs+1] = {
					addr      = a:host():string(),
					netmask   = a:mask():string(),
					prefix    = a:prefix()
				}
			end
			for _, a in ipairs(device:ip6addrs()) do
				if not a:is6linklocal() then
					data.ip6addrs[#data.ip6addrs+1] = {
						addr      = a:host():string(),
						netmask   = a:mask():string(),
						prefix    = a:prefix()
					}
				end
			end

			for _, device in ipairs(net:get_interfaces() or {}) do
				data.subdevices[#data.subdevices+1] = {
					name       = device:shortname(),
					type       = device:type(),
					ifname     = device:name(),
					macaddr    = device:mac(),
					macaddr    = device:mac(),
					is_up      = device:is_up(),
					rx_bytes   = device:rx_bytes(),
					tx_bytes   = device:tx_bytes(),
					rx_packets = device:rx_packets(),
					tx_packets = device:tx_packets(),
				}
			end

			rv[#rv+1] = data
		else
			rv[#rv+1] = {
				id   = iface,
				name = iface,
				type = "ethernet"
			}
		end
	end

	if #rv > 0 then
		luci.http.prepare_content("application/json")
		luci.http.write_json(rv)
		return
	end

	luci.http.status(404, "No such device")
end

function wifi_status(devs)
	local s    = require "luci.tools.status"
	local rv   = { }

	local dev
	for dev in devs:gmatch("[%w%.%-]+") do
		rv[#rv+1] = s.wifi_network(dev)
	end

	if #rv > 0 then
		luci.http.prepare_content("application/json")
		luci.http.write_json(rv)
		return
	end

	luci.http.status(404, "No such device")
end

function diag_command(cmd, addr)
	if addr and addr:match("^[a-zA-Z0-9%-%.:_]+$") then
		luci.http.prepare_content("text/plain")

		local util = io.popen(cmd % addr)
		if util then
			while true do
				local ln = util:read("*l")
				if not ln then break end
				luci.http.write(ln)
				luci.http.write("\n")
			end

			util:close()
		end

		return
	end

	luci.http.status(500, "Bad address")
end

function diag_ping(addr)
	diag_command("ping -c 5 -W 1 %q 2>&1", addr)
end

function diag_traceroute(addr)
	diag_command("traceroute -m 15 -w 1 %s 2>&1", addr)
end