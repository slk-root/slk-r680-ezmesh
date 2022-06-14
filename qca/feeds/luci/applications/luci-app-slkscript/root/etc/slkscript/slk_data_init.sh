#!/bin/sh

. /lib/functions.sh
output_log=/tmp/slkLog

# 1, 查看/dev/mmcblk0p22分区名称是否为'slk_data' ，否则直接退出。
# 2, 查看此分区是否挂载成功，df -h|grep slk_data
# 3, 创建slk_data文件，并挂载分区，mkdir /mnt/slk_data && mount /dev/mmcblk0p22 /mnt/slk_data

# 4, 当set_config文件存在时，读取set_config配置写到/etc/config/中。
# 5, 运行slk_data中的run.sh文件



Set_etc()
{
	file_config=$1
	#echo "$file_config"
	str=$(cat $file_config |grep "sn_number" |awk '{print $2}')
	if [ "$str" ];then
		#echo "sn_number=$str"
		uci -q set system.@system[0].sn_number=$str
	fi
	str=$(cat $file_config |grep "model_name" |awk '{print $2}')
	if [ "$str" ];then
		#echo "model=$str"
		uci -q set system.@system[0].model=$str
	fi
	str=$(cat $file_config |grep "lan_ipaddr" |awk '{print $2}')
	if [ "$str" ];then
		#echo "lan ipaddr=$str"
		uci -q set network.lan.ipaddr=$str
	fi
	str=$(cat $file_config |grep "lan_netmask" |awk '{print $2}')
	if [ "$str" ];then
		#echo "lan netmask=$str"
		uci -q set network.lan.netmaskr=$str
	fi
	
	wifimac=$(slkmac | awk -F ":" '{print $5""$6 }'| tr a-z A-Z)
	str=$(cat $file_config |grep "ssid5" |awk '{print $2}')
	if [ "$str" ];then
		#echo "wifi ssid2=$str-$wifimac"
		uci -q set wireless.wlan0.ssid="$str-$wifimac"
	fi
	str=$(cat $file_config |grep "ssid2" |awk '{print $2}')
	if [ "$str" ];then
		#echo "wifi ssid5=$str-$wifimac"
		uci -q set wireless.wlan1.ssid="$str-$wifimac"
	fi
	str=$(cat $file_config |grep "key5" |awk '{print $2}')
	if [ "$str" ];then
		#echo "key5=$str"
		uci -q set wireless.wlan0.key=$str
	fi
	str=$(cat $file_config |grep "key2" |awk '{print $2}')
	if [ "$str" ];then
		#echo "lan netmask=$str"
		uci -q set wireless.wlan1.key=$str
	fi
	uci commit
}


###step1,2,3###
art_partition=$(find_mmc_part slk_data)
if [ ! -z "$art_partition" ];then
	ext=$(file -s $art_partition |grep ext4)
	if [ -z "$ext" ];then
		mkfs.ext4 -t /dev/mmcblk0p22
	fi
	dh=$(df -h |grep slk_data)
	if [ -z "$dh" ];then
		mkdir /mnt/slk_data 
		mount /dev/mmcblk0p22 /mnt/slk_data
	fi

	###step4
	set_config="/mnt/slk_data/set_config"
	if [ -f "$set_config" ];then
		Set_etc $set_config
		###配置完成后删除set_config文件
		rm -r $set_config
	fi	
	
	###step5
	run="/mnt/slk_data/run.sh"
	if [ -f "$run" ];then
		/bin/sh $run &
	fi
else
	echo "Do not run, exit"
fi

