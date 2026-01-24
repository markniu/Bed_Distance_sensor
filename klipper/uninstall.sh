#!/bin/bash

HOME_DIR="${HOME}/klipper"

if [  -d "$1" ] ; then

	echo "$1"
	HOME_DIR=""$1"/klipper"
fi

BDDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

echo "klipper path:  "$HOME_DIR""
echo "BDsensor path: "$BDDIR""
echo ""

echo "delete BDsensor.py and BD_sensor.c from klipper"

rm "${HOME_DIR}/klippy/extras/BDsensor.py"
rm "${HOME_DIR}/src/BD_sensor.c"
rm "${HOME_DIR}/make_with_bdsensor.sh"

sed -i '/src-y += BD_sensor.c/d' "${HOME_DIR}/src/Makefile"

cd "$HOME_DIR"
git checkout "${HOME_DIR}/scripts/buildcommands.py"

echo ""
echo "Uninstalled Bed Distance Sensor successful :) "
echo ""
