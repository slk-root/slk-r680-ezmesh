-- Copyright 2012 Christian Gagneraud <chris@techworks.ie>
-- Licensed to the public under the Apache License 2.0.

m = Map("system", translate("Iperf Test"), translate("Iperf streaming test"))

s = m:section(TypedSection, "iperf")
s.anonymous = true

ipf2 = s:option(Flag, "ipf2",translate("Iperf2 Server"))
ipf2port=s:option(Value, "ipf2port", translate("Iperf2 Port"))
ipf2port.default = "5001"
ipf2port:depends({ipf2="1"})
ipf3 = s:option(Flag, "ipf3",translate("Iperf3 Server"))
ipf3port=s:option(Value, "ipf3port", translate("Iperf3 Port"))
ipf3port.default = "5201"
ipf3port:depends({ipf3="1"})

local apply = luci.http.formvalue("cbi.apply")
if apply then
    io.popen("/etc/init.d/iperss restart")
end

return m
