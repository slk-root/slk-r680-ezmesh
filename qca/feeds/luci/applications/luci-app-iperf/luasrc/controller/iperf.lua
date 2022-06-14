-- Copyright 2012 Christian Gagneraud <chris@techworks.ie>
-- Licensed to the public under the Apache License 2.0.

module("luci.controller.iperf", package.seeall)

function index()
   entry({"admin", "network", "iperf"}, cbi("iperf/iperf"))
end
