module("luci.controller.modem", package.seeall)

function index()
	if nixio.fs.access("/etc/config/modem") then
		local get_sim1=luci.sys.exec("uci -q get modem.SIM")
		if get_sim ~= "" or get_sim ~= nil then
			local types=luci.sys.exec("uci -q get modem.SIM.net")
			if string.find(types,"4G") then
				page=entry({"admin", "network", "modem"}, cbi("admin_network/modem"), _("4G Modem"), 1)
			else
				page=entry({"admin", "network", "modem"}, cbi("admin_network/modem"), _("5G Modem"), 1)
			end
		end
	end
end
