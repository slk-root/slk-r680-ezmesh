#!/bin/bash

BUILD_DIR=~/Image_X64
path=$(pwd)

echo "Build ipq image"
if [ ! -d $path/bin/ipq/img_x64/ ];then
	mkdir $path/bin/ipq/img_x64
fi

if [ ! -d ~/Image_X64 ];then
	echo "Image_X64 folder not found"
	exit
fi

tim=$(date "+%m%d_%H%M%S")
cp bin/ipq/openwrt* $BUILD_DIR/common/build/ipq_x64/  &&
cp bin/ipq/dtbs/qcom-ipq6018-cp01* $BUILD_DIR/common/build/ipq_x64/  &&

cd $BUILD_DIR/common/build  &&

export BLD_ENV_BUILD_ID=P  &&

python update_common_info.py  &&

echo "Copy bin/* to $path/bin/ipq/img_x64"

cp  bin/emmc-ipq6018_64-apps.img $path/bin/ipq/img_x64/slk-r680_$tim-sysupgrade.bin
cp  bin/emmc-ipq6018_64-single.img $path/bin/ipq/img_x64/slk-r680_$tim-factory.bin
ls $path/bin/ipq/img_x64/

echo "Build END!"