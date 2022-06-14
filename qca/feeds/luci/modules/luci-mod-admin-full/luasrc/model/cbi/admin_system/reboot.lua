--[[
LuCI - Lua Configuration Interface

Copyright 2010 Jo-Philipp Wich <xm@subsignal.org>

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0
]]--
local m, section

m = Map("system", translate("Device Reboot"), translate("Configure Device Reboot"))
m:chain("luci")
s2 = m:section(TypedSection, "system")
button = s2:option(ListValue, "_reboot")
button.template = "admin_system/reboots"
s2.anonymous = true
s2.addremove = false

--[[
button = s:option(Button, "__reboot", translate("reboot"))
function button.write()
		luci.template.render("admin_system/reboot", {reboot=reboot})
	if reboot then
		luci.sys.reboot()
	end
end
--]]


return m
