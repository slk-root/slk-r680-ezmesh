-- Copyright 2012 Jo-Philipp Wich <jow@openwrt.org>
-- Licensed to the public under the Apache License 2.0.

local m, section, m2, s2

m = Map("modem", translate("Mobile Network"))
section = m:section(NamedSection, "SIM", "rndis", translate("SIM Settings"))
section.anonymous = true
section.addremove = false
	section:tab("general", translate("General Settings"))
	section:tab("advanced", translate("Advanced Settings"))
	section:tab("physical", translate("Physical Settings"))

local function set_status(self)
	-- if current network is empty, print a warning
	--if not net:is_floating() and net:is_empty() then
	--	st.template = "cbi/dvalue"
	--	st.network  = nil
	--	st.value    = translate("There is no device assigned yet, please attach a network device in the \"Physical Settings\" tab")
	--else
		st.template = "admin_network/iface_status"
		st.network  = self
		st.value    = nil
	--end
end

st = section:taboption("general", DummyValue, "__statuswifi", translate("Status"))
st.on_init = set_status("SIM")

enable = section:taboption("general", Flag, "enable", translate("Enable"))
enable.rmempty  = false

--查询模块型号--
function modem_name(name)
	name=string.gsub(luci.sys.exec("cat /tmp/SIM | grep "..name.." | wc -l"),"%s+","")
	return name
end


apn = section:taboption("general", Value, "apn", translate("APN"))

username = section:taboption("general", Value, "username", translate("Username"))

password = section:taboption("general", Value, "password", translate("Password"))
password.password = true

auth_type = section:taboption("general", ListValue, "auth_type", translate("Auth Type"))
auth_type:value("0", translate"none")
auth_type:value("1", "pap")
auth_type:value("2", "chap")
if modem_name("mv31w") == "1" then
	auth_type:value("3", "mschapv2")
elseif modem_name("fm650") == "1" or modem_name("fm150_na") == "1" then
	auth_type:value("3", "pap/chap")
elseif modem_name("rm500u") == "1" then
	auth_type:value("3", "pap/chap")
elseif modem_name("sim7600") == "1" then
	auth_type:value("3", "pap/chap")
end

pincode = section:taboption("general", Value, "pincode", translate("PIN Code"))
---------------------------advanced------------------------------
enable = section:taboption("advanced", Flag, "force_dial", translate("Force Dial"))
enable.rmempty  = false

ipv4v6 = section:taboption("advanced", ListValue, "ipv4v6", translate("IP Type"))
ipv4v6:value("IP", "IPV4")
ipv4v6:value("IPV6", "IPV6")
ipv4v6:value("IPV4V6", "IPV4V6")

if modem_name("fm650") == "1" then
	bandlist_wcdma = section:taboption("advanced", ListValue, "band_wcdma", translate("Lock 3G Band List"))
	bandlist_wcdma:value("0", translate("Automatically"))
	bandlist_wcdma:value("1", translate("BAND 1"))
	bandlist_wcdma:value("2", translate("BAND 2"))
	bandlist_wcdma:value("5", translate("BAND 5"))
	bandlist_wcdma:value("8", translate("BAND 8"))
	bandlist_wcdma:depends("smode","1")
	bandlist_wcdma:depends("smode","3")
	
	bandlist_lte = section:taboption("advanced", ListValue, "band_lte", translate("Lock 4G Band List"))
	bandlist_lte:value("0", translate("Automatically"))
	bandlist_lte:value("101", translate("BAND 1"))
	bandlist_lte:value("102", translate("BAND 2"))
	bandlist_lte:value("103", translate("BAND 3"))
	bandlist_lte:value("105", translate("BAND 5"))
	bandlist_lte:value("107", translate("BAND 7"))
	bandlist_lte:value("108", translate("BAND 8"))
	bandlist_lte:value("134", translate("BAND 34"))
	bandlist_lte:value("138", translate("BAND 38"))
	bandlist_lte:value("139", translate("BAND 39"))
	bandlist_lte:value("140", translate("BAND 40"))
	bandlist_lte:value("141", translate("BAND 41"))
	bandlist_lte:depends("smode","2")
	bandlist_lte:depends("smode","3")
	bandlist_lte:depends("smode","6")
	bandlist_lte:depends("smode","7")
	
	bandlist_nr = section:taboption("advanced", ListValue, "band_nr", translate("Lock 5G Band List"))
	bandlist_nr:value("0", translate("Automatically"))
	bandlist_nr:value("501", translate("BAND 1"))
	bandlist_nr:value("5028", translate("BAND 28"))
	bandlist_nr:value("5041", translate("BAND 41"))
	bandlist_nr:value("5078", translate("BAND 78"))
	bandlist_nr:value("5079", translate("BAND 79"))
	bandlist_nr:depends("smode","4")
	bandlist_nr:depends("smode","6")
	bandlist_nr:depends("smode","7")
end

if modem_name("fm150_na") == "1" then
	bandlist_wcdma = section:taboption("advanced", ListValue, "band_wcdma", translate("Lock 3G Band List"))
	bandlist_wcdma:value("0", translate("Automatically"))
	bandlist_wcdma:value("1", translate("BAND 1"))
	bandlist_wcdma:value("2", translate("BAND 2"))
	bandlist_wcdma:value("4", translate("BAND 4"))
	bandlist_wcdma:value("5", translate("BAND 5"))
	bandlist_wcdma:value("8", translate("BAND 8"))
	bandlist_wcdma:depends("smode","1")
	bandlist_wcdma:depends("smode","3")
	
	bandlist_lte = section:taboption("advanced", ListValue, "band_lte", translate("Lock 4G Band List"))
	bandlist_lte:value("0", translate("Automatically"))
	bandlist_lte:value("102", translate("BAND 2"))
	bandlist_lte:value("104", translate("BAND 4"))
	bandlist_lte:value("105", translate("BAND 5"))
	bandlist_lte:value("107", translate("BAND 7"))
	bandlist_lte:value("112", translate("BAND 12"))
	bandlist_lte:value("113", translate("BAND 13"))
	bandlist_lte:value("114", translate("BAND 14"))
	bandlist_lte:value("117", translate("BAND 17"))
	bandlist_lte:value("125", translate("BAND 25"))
	bandlist_lte:value("126", translate("BAND 26"))
	bandlist_lte:value("129", translate("BAND 29"))
	bandlist_lte:value("130", translate("BAND 30"))
	bandlist_lte:value("141", translate("BAND 41"))
	bandlist_lte:value("142", translate("BAND 42"))
	bandlist_lte:value("143", translate("BAND 43"))
	bandlist_lte:value("146", translate("BAND 46"))
	bandlist_lte:value("148", translate("BAND 48"))
	bandlist_lte:value("166", translate("BAND 66"))
	bandlist_lte:value("171", translate("BAND 71"))
	bandlist_lte:depends("smode","2")
	bandlist_lte:depends("smode","3")
	bandlist_lte:depends("smode","6")
	bandlist_lte:depends("smode","7")
	
	bandlist_nr = section:taboption("advanced", ListValue, "band_nr", translate("Lock 5G Band List"))
	bandlist_nr:value("0", translate("Automatically"))
	bandlist_nr:value("502", translate("BAND 2"))
	bandlist_nr:value("505", translate("BAND 5"))
	bandlist_nr:value("507", translate("BAND 7"))
	bandlist_nr:value("5012", translate("BAND 12"))
	bandlist_nr:value("5025", translate("BAND 25"))
	bandlist_nr:value("5041", translate("BAND 41"))
	bandlist_nr:value("5066", translate("BAND 66"))
	bandlist_nr:value("5071", translate("BAND 71"))
	bandlist_nr:value("5077", translate("BAND 77"))
	bandlist_nr:value("5078", translate("BAND 78"))
	bandlist_nr:depends("smode","4")
	bandlist_nr:depends("smode","6")
	bandlist_nr:depends("smode","7")
end

-- smode = section:taboption("advanced", ListValue, "smode", translate("Server Type"))
-- smode.default = "0"
-- smode:value("0", translate"Automatically")
-- smode:value("1", "WCDMA Only")
-- smode:value("2", "LTE Only")
-- smode:value("3", "WCDMA And LTE")
-- if modem_name("sim7600") == "1" then
	-- smode:value("5", "GSM Only")
	-- smode:value("8", "GSM And WCDMA")
	-- smode:value("9", "GSM And LTE")
	-- smode:value("10", "GSM And WCDMA And LTE")
-- else
	-- smode:value("4", "NR5G Only")
	-- smode:value("6", "LTE And NR5G")
	-- smode:value("7", "WCDMA And LTE And NR5G")
-- end
smode = section:taboption("advanced", ListValue, "smode", translate("Server Type"))
smode.default = "0"
smode:value("0", translate"Automatically")
if modem_name("sim7600") == "1" then
	smode:value("5", "2G (GSM) Only")
	smode:value("1", "3G (WCDMA) Only")
	smode:value("2", "4G (LTE) Only")
	smode:value("8", "2/3G (GSM/WCDMA)")
	smode:value("9", "2/4G (GSM/LTE)")
	smode:value("3", "3/4G (WCDMA/LTE)")
	smode:value("10", "2/3/4G (GSM/WCDMA/LTE)")
else
	smode:value("1", "3G (WCDMA) Only")
	smode:value("2", "4G (LTE) Only")
	smode:value("4", "5G (NR) Only")
	smode:value("3", "3/4G (WCDMA/LTE)")
	smode:value("6", "4/5G (LTE/NR)")
	smode:value("7", "3/4/5G(WCDMA/LTE/NR5G)")
end


default_card = section:taboption("physical", ListValue, "default_card", translate("Default SIM Card"))
if modem_name("sim7600") == "1" then
default_card:value("1", translate"SIM1")
default_card:value("0", translate"SIM2")
default_card.default = "1"
else
default_card:value("0", translate"SIM1")
default_card:value("1", translate"SIM2")
default_card.default = "0"
end
metric = section:taboption("physical", Value, "metric", translate("Metric"))
metric.default = "10"

mtu = section:taboption("physical", Value, "mtu", translate("Override MTU"))
mtu.placeholder = "1500"
mtu.datatype = "max(9200)"

s2 = m:section(NamedSection, "SIM", "rndis", translate("Abnormal Restart"),translate("Network exception handling: \
check the network connection in a loop for 5 seconds. If the Ping IP address is not successful, After the network \
exceeds the abnormal number, restart and search the registered network again."))
s2.anonymous = true
s2.addremove = false

en = s2:option(Flag, "pingen", translate("Enable"))
en.rmempty = false

ipaddress= s2:option(Value, "pingaddr", translate("Ping IP address"))
ipaddress.default = "114.114.114.114"
ipaddress.rmempty=false

opera_mode = s2:option(ListValue, "opera_mode", translate("Operating mode"))
opera_mode.default = "airplane_mode"
opera_mode:value("reboot_route", translate("Reboot on internet connection lost"))
opera_mode:value("airplane_mode", translate("Set airplane mode"))
opera_mode:value("switch_card", translate("Switch SIM card"))

an = s2:option(Value, "count", translate("Abnormal number"))
an.default = "15"
an:value("3", "3")
an:value("5", "5")
an:value("10", "10")
an:value("15", "15")
an:value("20", "20")
an:value("25", "25")
an:value("30", "30")
an.rmempty=false

local apply = luci.http.formvalue("cbi.apply")
if apply then
    io.popen("/etc/init.d/modeminit restart")
end

return m,m2
