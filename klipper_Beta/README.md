
### This is the Beta version patch code for klipper.

if you like to test with this new code please run
```
cd  ~/Bed_Distance_sensor/klipper_Beta/
./install_BDsensor.sh
```
instead of 
```
cd  ~/Bed_Distance_sensor/klipper/
./install_BDsensor.sh
```

in the installation step of Klipper

https://github.com/markniu/Bed_Distance_sensor/wiki/Installing-for-Klipper


#### update: 
#### Add real time leveling. 
November 21, 2023

How to run?   

1. update the files in ~/Bed_Distance_sensor/klipper_Beta
2. go into the folder `cd ~/Bed_Distance_sensor/klipper_Beta`  and run` ./install_BDsensor.sh`
3. compile and flash the klipper.bin firmware again

4. Edit the start gcode in your slicer:
remove the auto bed leveling command `BED_MESH_CALIBRATE` and add `BDSENSOR_SET REAL_TIME_HEIGHT=`xx under the G28,
also z_tilt or gantry level is recommended. 
xx is the max z axis height that will do real time leveling automatically, here we just let this value the same as the first layer height in slicer setting.for example 0.3mm
```
G28
QUAD_GANTRY_LEVEL
G28
BDSENSOR_SET REAL_TIME_HEIGHT=0.3
```

note: this real time leveling works only with the BDsensor and Z motors are connected in the same MCU. 

And install the sensor coil as close to the nozzle as possible.

if you find the nozzle is too close or too far away the bed while printing, please do the calibration again.


#### update:
November 15, 2023

1, add new command:
```
BDSENSOR_VERSION                   //equals M102 S-1
BDSENSOR_CALIBRATE                //equals M102 S-6 
BDSENSOR_READ_CALIBRATION         //equals M102 S-5 
BDSENSOR_DISTANCE                 //equals M102 S-2 
BDSENSOR_SET                           // set value, now only for setting z_adjust, for example BDSENSOR_SET z_adjust=0.1

```
2, invert z_adjust direction as z_offset





