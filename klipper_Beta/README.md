
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
November 15, 2023

1, add new command:
```
BDSENSOR_VERSION                   //replace M102 S-1
BDSENSOR_CALIBRATE                //replace M102 S6 
BDSENSOR_READ_CALIBRATION  //replace M102 S-5 
BDSENSOR_DISTANCE                 //replace M102 S-2 
BDSENSOR_SET                           // set value, now only for setting z_adjust, for example BDSENSOR_SET z_adjust=0.1

```
2, invert z_adjust direction as z_offset



#### update: Add real time leveling. 
November 21, 2023

How to test?   

remove the auto bed leveling command  BED_MESH_CALIBRATE and add M102 Sxx below the G28, here xx is the first layer height that will do real time leveling, for example, set the first layer height is 0.3mm

```
G28
M102 S0.3

```


