--gpio-bug-network v1.2
--2021.05.06

---[[ ����ָ�����ȡ����
function Returnpopen(liunxmsg)
	local brightness = io.popen(liunxmsg) --io.popen()ÿһ��ʹ�ã�������ʹ��close()�ر� ����ʱ��ʹ�ó��������
	local get_brightness = brightness:read("*all")
	brightness:close()
	get_brightness = string.gsub(get_brightness,"%s+","")
	if tonumber(get_brightness) then --�ж��ַ����Ƿ����ת�����֣�true ת�����֣������ַ�����
		get_brightness = tonumber(get_brightness)
	end
	return get_brightness
end
--]]
--�ж�ip�Ƿ�Ϸ�
function JudgeIPString(ipStr)
    if type(ipStr) ~= "string" then
        return false;
    end
    
    --�жϳ���
    local len = string.len(ipStr);
    if len < 7 or len > 15 then --���Ȳ���
        return false;
    end
 
    --�жϳ��ֵķ������ַ�
    local point = string.find(ipStr, "%p", 1); --�ַ�"."���ֵ�λ��
    local pointNum = 0; --�ַ�"."���ֵĴ��� ����ip��3��"."
    while point ~= nil do
        if string.sub(ipStr, point, point) ~= "." then --�õ������ַ��Ų����ַ�"."
            return false;
        end
        pointNum = pointNum + 1;
        point = string.find(ipStr, "%p", point + 1);
        if pointNum > 3 then
            return false;
        end
    end
    if pointNum ~= 3 then --������ȷ��ip��ʽ
        return false;
    end
 
    --�ж����ֶԲ���
    local num = {};
    for w in string.gmatch(ipStr, "%d+") do
        num[#num + 1] = w;
        local kk = tonumber(w);
        if kk == nil or kk > 255 then --�������ֻ򳬹�ip����ȡֵ��Χ��
            return false;
        end
    end
 
    if #num ~= 4 then --����4������
        return false;
    end
 
    return ipStr;
end

local indexWhile=0;
while true do
	--print(JudgeIPString("1"))
	local ispoip=Returnpopen('ifconfig br-lan|grep inet|awk \'{print $2}\'|tr -d \'addr:\'')
	print(JudgeIPString(ispoip))
	if (ispoip ~="" or ispoip ~=nil) and JudgeIPString(ispoip) then
		break
	else
		if indexWhile == 3 and (ispoip =="" or ispoip ==nil) then
			Returnpopen('/etc/init.d/network restart')
		end
		if indexWhile >= 30 then
			break
		end
		Returnpopen("sleep 1")
	end
	indexWhile=indexWhile+1
end

local socket = require("socket")
local cjson = require("cjson")
local udp = socket.udp()

local host = '255.255.255.255'
local port = '900'
udp:setoption('broadcast', true)
udp:settimeout(10)
udp:setsockname(host,port)
--print('waiting client connect')
function Split(szFullString, szSeparator)
local nFindStartIndex = 1
local nSplitIndex = 1
local nSplitArray = {}
while true do
   local nFindLastIndex = string.find(szFullString, szSeparator, nFindStartIndex)
   if not nFindLastIndex then
     nSplitArray[nSplitIndex] = string.sub(szFullString, nFindStartIndex, string.len(szFullString))
     break
   end
   nSplitArray[nSplitIndex] = string.sub(szFullString, nFindStartIndex, nFindLastIndex - 1)
   nFindStartIndex = nFindLastIndex + string.len(szSeparator)
   nSplitIndex = nSplitIndex + 1
end
return nSplitArray
end


---[[ �ж��Ƿ���json
function ifIsJson(JsonString)
local pos1,pos2,pos3,pos4=0,0,0,0
local counter1,counter2,counter3,counter4=0,0,0,0
local string1,string2
local Mytable,Mytable2={},{}
local i,j=1,1
JsonString=JsonString:gsub("%s+", "")
string1=string.sub(JsonString,1,1)
string2=string.sub(JsonString,-1,-1)
if string1=="{" and string2=="}" then --�鿴���������Ƿ�ɶԴ���
_,pos1=string.gsub(JsonString,"{","{")  
_,pos2=string.gsub(JsonString,"}","}")
_,pos3=string.gsub(JsonString,"%[","[")
_,pos4=string.gsub(JsonString,"%]","]")
if pos1~=pos2 or pos3~=pos4 then return false end
else return false end
while (true) do
pos1,pos2=string.find(JsonString,",%[{",pos1)-- �� ,[{ �ҵ����� }]
if pos1 then
pos3,pos4=string.find(JsonString,"}]",pos4)
if pos3 then
string1=string.sub(JsonString,pos1,pos4)
_,counter1=string.gsub(string1,"{","{")  --�鿴���������Ƿ�ɶԴ���
_,counter2=string.gsub(string1,"}","}")
_,counter3=string.gsub(string1,"%[","[")
_,counter4=string.gsub(string1,"%]","]")
if counter1 == counter2 and counter3== counter4 then
Mytable[i]=string.sub(JsonString,pos2,pos3) --{....}
i=i+1
string1=string.sub(JsonString,1,pos1-1)
string2=string.sub(JsonString,pos4+1)
JsonString=string1..string2 -- ȥ��,{[..}]
pos4=pos1
end
else return false end
else 
pos1,pos2,pos3,pos4=1,1,1,1
pos1,pos2=string.find(JsonString,"%[{") --��[{ �ҵ����� }]û��������
if pos1 then
pos3,pos4=string.find(JsonString,"}]")
if pos3 then
string1=string.sub(JsonString,pos1,pos4)
_,counter1=string.gsub(string1,"{","{")  --�鿴���������Ƿ�ɶԴ���
_,counter2=string.gsub(string1,"}","}")
_,counter3=string.gsub(string1,"%[","[")
_,counter4=string.gsub(string1,"%]","]")
if counter1 == counter2 and counter3== counter4 then
Mytable[i]=string.sub(JsonString,pos2,pos3) --{....}
i=i+1
string1=string.sub(JsonString,1,pos1-1)
string2=string.sub(JsonString,pos4+1)
JsonString=string1.."\"\""..string2 -- ȥ��,[{..}]
pos4=pos1
end
else return false end
else break end
end
end
i=i-1
if Mytable[i]~= nil then
pos1,pos2,pos3,pos4=1,1,1,1
while (true) do  --��ȡǶ��n����������[{.....}]
repeat       -- ��table[]��[{���������,
pos1,pos2=string.find(Mytable[i],"%[{",pos2)
if pos1 then pos3,pos4=pos1,pos2  end
until pos1==nil
pos1,pos2=string.find(Mytable[i],"}]",pos4)  --�Ҵ���pos4֮��}]�ǰ���������
if pos1 then
Mytable2[j]=string.sub(Mytable[i],pos4,pos1) --[ {....} ]
j=j+1
string1=string.sub(Mytable[i],1,pos3-1)
stirng2=string.sub(Mytable[i],pos2+1)
Mytable[i]=string1.."\"\""..string2
else 
Mytable2[j]=Mytable[i]
j=j+1
i=i-1
if i== 0 then break end--ֱ���Ҳ����ɶԵ�[{}]
end
pos2=1
end
end


Mytable2[j]=JsonString
i=1
Mytable={}
pos1,pos2,pos3,pos4=0,0,1,1
while (true) do
repeat
pos1,_=string.find(Mytable2[j],"{",pos2+1)
if pos1 then pos2=pos1 end
until pos1 == nil 
pos3,_=string.find(Mytable2[j],"}",pos2)
if pos3 and pos2~=1 then
Mytable[i]=string.sub(Mytable2[j],pos2,pos3) --  {...}
i=i+1
string1=string.sub(Mytable2[j],1,pos2-1)
string2=string.sub(Mytable2[j],pos3+1)
Mytable2[j]=string1.."\"\""..string2
else
Mytable[i]=string.sub(Mytable2[j],1,pos3)
i=i+1
j=j-1
if j==0 then break end
end
pos2=0
-- ��b��ȡ   {  "id":"243125b4-5cf9-4ad9-827b-37698f6b98f0" }  �����ĸ�ʽ ���table[j]
-- ʣ��һ�� "image":{ "id":"243125b4-5cf9-4ad9-827b-37698f6b98f0","a":"e0", "d":"2431-f6b98f0","f":"243125b98f0"--}������Ҳ���table[j+1]
end

i=i-1
for n=1,i do  --ȥ��{}
Mytable[n]=string.sub(Mytable[n],2,-2)
end

while (true) do
pos1,_=string.find(Mytable[i],",")
if pos1~= nil then --"a":"i","b":"j"����֮��Ķ���
string1=string.sub(Mytable[i],1,pos1-1)--,ǰ
Mytable[i]=string.sub(Mytable[i],pos1+1)
pos2,_=string.find(string1,"\"")
if pos2==1 then
pos3,pos4=string.find(string1,"\":",2)
if pos3 then
string2=string.sub(string1,pos4+1)
else 
--("���ִ���1", 1)
return false
end
else 
--("���ִ���2", 1)
return false
end
else
pos2,_=string.find(Mytable[i],"\"")
if pos2==1 then
pos3,pos4=string.find(Mytable[i],"\":",2)
if pos3 then
string2=string.sub(Mytable[i],pos4+1)
else
--("���ִ���3", 1)
return false
end
else 
--("���ִ���4", 1)
return false
end
end


pos2,pos3=string.gsub(string2,"(\"-)(.+)(\"+)","")
if pos2=="" or pos2 == "null" then
--("��һ������ʽ��ȷ", 2)
else
pos2=tonumber(pos2)
if type(pos2) == "number" then
--("��һ������ʽ��ȷ2", 2)
else
--("���ִ���5", 1)
return false
end
end
if pos1== nil then
i=i-1
if i==0 then return true end
end
end

end
--]]


---[[ Debug��������ڣ�-- ���Կڣ�en������0�رգ�1�򿪣�2������ļ�
local en=1
local outfile="/tmp/slklog"
function Debug(str)
	--os.execute('echo '..str)
	local dates=io.popen('date "+%Y-%m-%d %H:%M:%S"')
	local tim=dates:read("*all")
	dates:close()
	if (en == 1) then
		print(string.gsub(tim,"\n","")..' '..string.gsub(str,"%s+",""))
	elseif (en == 2) then
		os.execute('echo '..string.gsub(tim,"\n","")..' '..string.gsub(str,"%s+","")..'>> '..outfile)
	end
end
--]]

---[[ �������ݵķ���
function SendData(TableJson,receip,receport)
	local sendcli = udp:sendto(TableJson,receip,receport)
	if(sendcli) then
	return true
		--print('send ok')
	else	
	return false
		--print('send error')
	end

end
--]]





---[[ ���ز�ѯ����״̬
function ReturnDefault()
	local get_mac = Returnpopen('ifconfig br-lan | sed -n \'\/HWaddr\/ s\/\^.\*HWaddr \*//pg\'')
	local get_ip = Returnpopen('ifconfig br-lan|grep inet|awk \'{print $2}\'|tr -d \'addr:\'')
	local get_wanip --= Returnpopen('ifconfig eth0.1|grep inet|awk \'{print $2}\'|tr -d \'addr:\'') or ""
	local get_name = Returnpopen('sed -n 1p /etc/slkinfo') or ""
	if get_name=="" then
		get_name = Returnpopen('uci get system.@system[0].modelname')
		if get_name=="" then
			get_name = Returnpopen('uci -q get system.@system[0].model')
		end
	end
	local get_hostname = Returnpopen('uci get system.@system[0].hostname') or "Seriallink"
	local vers = Returnpopen('uci get system.@system[0].F_ver')
	local util = require "luci.util"
	local sysinfo = luci.util.ubus("system", "info") or { }
	local get_uptime = sysinfo.uptime or 0
	--local ubuslan = Returnpopen('ubus call network.interface.lan status > /tmp/sedsta;cat /tmp/sedsta')
	local rv={
		MAC=get_mac,
		Ip=get_ip,
		wanIp=get_wanip,
		HostName=get_name,
		hostname=get_hostname,
		uptime=get_uptime,
		vers=vers,
		FourCode="OK"
	}
	return rv;
end
--]]


---[[ ����תJSON 
function Returnformat(rv)
	return cjson.encode(rv)
end
--]]


---[[ led���Ƿ���˸����OK��һ������
function Brightness()
	local sum=0;
	for n=0,30 do
		local s1 = Returnpopen('cat /sys/class/leds/lan2/brightness')
		if (s1 ~= Returnpopen('cat /sys/class/leds/lan2/brightness')) then
			sum=sum+1;
		end
	end
	if(sum >= 1) then
		sum="OK"
	else
		sum="NO"
	end
	return sum
end
--]]

---[[ ��ʼ������
function Initialization()
	Returnpopen('echo 0 > /sys/class/leds/lan1/brightness')
	Returnpopen('echo 0 > /sys/class/leds/lan2/brightness')
	Returnpopen('echo 0 > $(ls /sys/class/leds/*\:*/brightness)')
end
--]]

---[[ ioled�Ʋ���
function Brightness1()
	local lanleds="NO";
	local sum=0;
	for n=1,5 do
		local lan1 = Returnpopen('cat /sys/class/leds/lan1/brightness')
		local lan2 = Returnpopen('cat /sys/class/leds/lan2/brightness')
		local lan3 = Returnpopen('cat $(ls /sys/class/leds/*\:*/brightness)')
		if (lan1 == 1) then Returnpopen('echo 0 > /sys/class/leds/lan1/brightness');sum=sum+1 else Returnpopen('echo 1 > /sys/class/leds/lan1/brightness');sum=sum+1 end
		if (lan2 == 1) then Returnpopen('echo 0 > /sys/class/leds/lan2/brightness');sum=sum+1 else Returnpopen('echo 1 > /sys/class/leds/lan2/brightness');sum=sum+1 end
		if (lan3 == 1) then Returnpopen('echo 0 > $(ls /sys/class/leds/*\:*/brightness)');sum=sum+1 else Returnpopen('echo 1 > $(ls /sys/class/leds/*\:*/brightness)');sum=sum+1 end
	end
	
	local lan1 = Returnpopen('cat /sys/class/leds/lan1/brightness')
	local lan2 = Returnpopen('cat /sys/class/leds/lan2/brightness')
	local lan3 = Returnpopen('cat $(ls /sys/class/leds/*\:*/brightness)')
	if (lan1 == lan2 and lan1 == lan3 and lan2 == lan3 and sum >=90) then lanleds = "OK" end
	return lanleds
end
--]]

---[[ 
function Checks()
	local msgChecks="OK";
	--���ģ���Ƿ����
	local cmd;
	cmd = Returnpopen("ls /usr/sbin |grep 'sendat' |wc -l")
	if (cmd ~= 1) then return "Sendat module does not exist" end
	
	--����Ƿ����
	cmd = Returnpopen("sendat 2 'AT+CPIN?'| grep 'ERROR' | wc -l")
	if (cmd ~= 1) then return "No card Reading" end
	--���
	return msgChecks
end
--]]
---[[ tty�����Ƿ����
function Returntty()
	local tty="NO";
	if (Returnpopen('cat /sys/kernel/debug/usb/devices | grep 2303 |wc -l') >= 1) then
		tty="OK"
	end
	return tty
end
--]]

---[[ tty�����Ƿ����
function Returniperf()
	local tty="NO";
	if (Returnpopen("iperf3 -c 192.168.1.168 | grep '4' | awk '{print $7}'")) then
		tty="OK"
	end
	return tty
end
--]]
---[[ ����IO�ڲ��Խ������
function ReturnGpio()
	local rv = ReturnDefault()
	--rv.NetworkCheck=Checks()
	--rv.Returniperf=Returniperf()
	--rv.Brightness = Brightness1()
	--rv.tty=Returntty()
	return rv
end
--]]

function ReturnGetMAC(mtd)
	local get_mac = Returnpopen('ifconfig br-lan | sed -n \'\/HWaddr\/ s\/\^.\*HWaddr \*//pg\'')
	local get_ip = Returnpopen('ifconfig br-lan|grep inet|awk \'{print $2}\'|tr -d \'addr:\'')
	local get_wanip --= Returnpopen('ifconfig eth0.1|grep inet|awk \'{print $2}\'|tr -d \'addr:\'') or ""
	local get_name = Returnpopen('sed -n 1p /etc/slkinfo') or ""
	if get_name=="" then
		get_name = Returnpopen('uci get system.@system[0].modelname')
		if get_name=="" then
			get_name = Returnpopen('uci -q get system.@system[0].model')
		end
	end
	local get_hostname = Returnpopen('uci get system.@system[0].hostname') or "Seriallink"
	local util = require "luci.util"
	local sysinfo = luci.util.ubus("system", "info") or { }
	local get_uptime = sysinfo.uptime or 0
	local setmac = Returnpopen('slkmac eth0')
	--local ubuslan = Returnpopen('ubus call network.interface.lan status > /tmp/sedsta;cat /tmp/sedsta')
	local rv={
		MAC=get_mac,
		Ip=get_ip,
		wanIp=get_wanip,
		HostName=get_name,
		hostname=get_hostname,
		uptime=get_uptime,
		setmac = setmac,
		FourCode="OK"
	}
	return rv;
end

--[[ ����wifi
function SetWifiCode(name,psw)
	--os.execute("uci add /etc/config/wireless wifi-iface")
	os.execute("uci set wireless.@wifi-iface[0]=wifi-iface")
	os.execute("uci set wireless.@wifi-iface[0].device=radio0")
	os.execute("uci set wireless.@wifi-iface[0].network=wwan")
	os.execute("uci set wireless.@wifi-iface[0].ssid="..name)
	os.execute("uci set wireless.@wifi-iface[0].mode=sta")
	os.execute("uci set wireless.@wifi-iface[0].encryption=psk2")
	os.execute("uci set wireless.@wifi-iface[0].key="..psw)
	os.execute("uci commit wireless")
--	os.execute("/etc/init.d/network restart")
end
--]]
--Returnpopen("route add -host 255.255.255.255 dev br-lan")
while 1 do
    local revbuff,receip,receport = udp:receivefrom()
    if(revbuff and receip and receport) then
		print('revbuff:'..revbuff..',receip:'..receip..',receport:'..receport)
		local get_mac = Returnpopen('ifconfig br-lan | sed -n \'\/HWaddr\/ s\/\^.\*HWaddr \*//pg\'')
--[[

	print(get_mac)
       local ip = io.popen('ifconfig br-lan|grep inet|awk \'{print $2}\'|tr -d \'addr:\'')
       local get_ip = ip:read("*all")
       local name = io.popen('uci get system.@system[0].hostname')
       local get_name = name:read("*all")
       local ports = io.popen('ls -l /dev/ttyS* | wc -l')
       local get_ports = ports:read("*all")
       local StringMac = string.gsub(get_mac,"%s+","")
       local StringIp = string.gsub(get_ip,"%s+","")
              local StringHostName = string.gsub(get_name,"%s+","")
		local jsd=io.popen('ubus call network.interface.lan status > /tmp/sedsta;cat /tmp/sedsta')
		local jsds=string.gsub(jsd:read("*all"),"%s+","")
	

---------------  ������������
       local TableJson = string.format('{"MAC":"%s","Ip":"%s","HostName":"%s","FourCode":"%s","ubus":%s}',StringMac,StringIp,StringHostName,'OK',jsds)
---------------  ��������wifi״̬
	local SetWifiJson = string.format('{"MAC":"%s","Ip":"%s","HostName":"%s","FourCode":"%s"}',StringMac,StringIp,StringHostName,'SetWifi')

       --local a = t:read("*all")--]]
		local ss = ifIsJson(revbuff)
		print(revbuff)
		if (ss) then
			-- ��json ��ʽ ��ת������
			local unjson = cjson.decode(revbuff)
			local MAC = cjson.encode(unjson['Mac'])
			
			if (unjson['FourCodes'] == nil) then
			else
				
				if (unjson['FourCodes'] == "OK") then

					if (unjson['Mac'] == get_mac) then

					
						if(unjson['SetorGet'] == "SetTmpIP") then
							
							if(unjson['TmpIP']) and (unjson['TmpIP'] ~="")then
								Debug(1);
								Returnpopen("ifconfig br-lan "..unjson['TmpIP'].." netmask 255.255.255.0 up")
							else
								Debug(2)
								--Returnpopen("ifconfig br-lan 192.168.2.1 netmask 255.255.255.0 up")
							end
							if(unjson['hostname']) and (unjson['hostname'] ~="")then
								Returnpopen("uci set system.@system[0].hostname="..unjson['hostname'].."")
							end
							Returnpopen("uci commit system")
							
							Returnpopen("route add -host 255.255.255.255 dev br-lan")
							local RetunSGPIO=Returnformat(ReturnGpio())
							local aSend=SendData(RetunSGPIO,"255.255.255.255",receport)
							if aSend then
								--break;
							else
								aSend=SendData(RetunSGPIO,receip,receport)
							end
						
						--��������
						elseif(unjson['SetorGet'] == "reboot")then
								Returnpopen("reboot")
						--�����ָ���������
						elseif(unjson['SetorGet'] == "reset")then
								Returnpopen("jffs2reset -y && reboot &")
						--����ɾ������
						elseif(unjson['SetorGet'] == "delluci") then
							Returnpopen("rm -rf /tmp/luci-*")
							Returnpopen("rm -rf /tmp/luci-*")
						elseif(unjson['SetorGet'] == "setmac") then
							if(unjson['Commands']) and (unjson['Commands'] ~= "" )then
								Returnpopen(unjson['Commands'])
							end
							local RetunSGPIO=Returnformat(ReturnGetMAC())
							local aSend=SendData(RetunSGPIO,"255.255.255.255",receport) 
						end
					elseif(unjson['Mac'] == "*")then
						--Initialization()
						
						local RetunSGPIO=Returnformat(ReturnGpio())
						--if(not Returnpopen("route |grep '255.255.255.255'"))then
							Returnpopen("route add -host 255.255.255.255 dev br-lan")
						--end
						print(receip)
						local aSend=SendData(RetunSGPIO,"255.255.255.255",receport)
						if aSend then
							--break;
						else
							aSend=SendData(RetunSGPIO,receip,receport)
						end
					elseif(unjson['Mac'] == "reset") then
						Returnpopen("jffs2reset -y && reboot &")
					elseif(unjson['Mac'] == "reboot") then
						Returnpopen("reboot")
					elseif(unjson['Mac'] == "delluci") then
						Returnpopen("rm -rf /tmp/luci-*")
						Returnpopen("rm -rf /tmp/luci-*")
					end
				else
					--SendData(TableJson,receip,receport)
				end
			end
		end
	else
       		print('waiting connect')
    end
end
udp:close()

