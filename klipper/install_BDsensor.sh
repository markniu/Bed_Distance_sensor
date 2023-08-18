#!/bin/bash

HOME_DIR="${HOME}/klipper"

if [  -d "$1" ] ; then
    
	echo "$1"
	HOME_DIR=""$1"/klipper"
fi

BDDIR="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"



if [ ! -d "$HOME_DIR" ] ; then
    echo ""
    echo "path error doesn't exist in "$HOME_DIR""
    echo ""
    echo "BDsensor path: "$BDDIR""
    echo ""
    echo "usage example:./install_BDsensor.sh /home/pi  or ./install_BDsensor.sh /home/mks "
    echo "Error!!"
    exit 1
fi

echo "klipper path:  "$HOME_DIR""
echo "BDsensor path: "$BDDIR""
echo ""

echo "linking BDsensor.py to klippy."

if [ -e "${HOME_DIR}/klippy/extras/BDsensor.py" ]; then
    rm "${HOME_DIR}/klippy/extras/BDsensor.py"
fi
ln -s "${BDDIR}/BDsensor.py" "${HOME_DIR}/klippy/extras/BDsensor.py"

echo "linking BD_sensor.c to klipper."

if [ -e "${HOME_DIR}/src/BD_sensor.c" ]; then
    rm "${HOME_DIR}/src/BD_sensor.c"
fi
ln -s "${BDDIR}/BD_sensor.c" "${HOME_DIR}/src/BD_sensor.c"

if ! grep -q "BD_sensor.c" "${HOME_DIR}/src/Makefile"; then
    echo "src-y += BD_sensor.c  " >> "${HOME_DIR}/src/Makefile"
fi


if ! grep -q "klippy/extras/BDsensor.py" "${HOME_DIR}/.git/info/exclude"; then
    echo "klippy/extras/BDsensor.py" >> "${HOME_DIR}/.git/info/exclude"
fi
if ! grep -q "src/BD_sensor.c" "${HOME_DIR}/.git/info/exclude"; then
    echo "src/BD_sensor.c" >> "${HOME_DIR}/.git/info/exclude"
fi

if ! grep -q "src/Makefile" "${HOME_DIR}/.git/info/exclude"; then
    echo "src/Makefile" >> "${HOME_DIR}/.git/info/exclude"
fi

sed 's/--dirty//g' "${HOME_DIR}/scripts/buildcommands.py" -i

echo ""
echo "Install Bed Distance Sensor successful :) "
echo ""
