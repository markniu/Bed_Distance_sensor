#!/bin/bash

BUILD_ARG=$1
FLASH_ARG=""
if [ "$BUILD_ARG" == "flash" ]; then
	if [ -z "$2" ]; then
		echo "Device ID is required for flashing"
		exit 1
	fi
	FLASH_ARG="flash FLASH_DEVICE=$2"
fi

echo "compiling BD_sensor.c into the klipper firmware"
sed -i '/BD_sensor/d' src/Makefile;echo "src-y += BD_sensor.c" >> src/Makefile
sed 's/--dirty//g' "./scripts/buildcommands.py" -i

make $FLASH_ARG

sed -i '/BD_sensor/d' src/Makefile
git checkout src/Makefile
git checkout ./scripts/buildcommands.py