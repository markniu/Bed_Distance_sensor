## Step1. Attach the sensor cable to the mainboard [Video](https://www.youtube.com/watch?v=fPwIH4phbgM)
the wires CKL and SDA of BDsensor can be connect to any GPIO pins of your mainboard. You can also connect the BDsensor cable into the Bltouch port directly if your mainboard has,for example:
```
 5V -- 5V(red)
GND -- GND(black)
 S  -- CLK(green)
Zmin-- SDA(white)
```  
## Step2. Mount the BDsensor near to the hot end

![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/Connection.jpg) 

## Step3. Configure & flash Marlin firmware [Video](https://www.youtube.com/watch?v=0T2bbuZYJtM)

The BDsensor have been integrated to Marlin2.1.x(Since 2022.8.27),you can download the latest here: https://github.com/MarlinFirmware/Marlin
 What do you need is change the configure file and pins file. 

### Edit Configuration.h

 Uncomment

 ```#define BD_SENSOR```    
 ```#define Z_SAFE_HOMING``` 

 ```#define BD_SENSOR_PROBE_NO_STOP //for fast bed leveling ```

the `BD_SENSOR_PROBE_NO_STOP` is a new feature only in the latest fork https://github.com/MarlinFirmware/Marlin/pull/25847

and make sure the Z_MIN_PROBE_USES_Z_MIN_ENDSTOP_PIN and USE_PROBE_FOR_Z_HOMING is disabled like the following
``` 
//#define Z_MIN_PROBE_USES_Z_MIN_ENDSTOP_PIN
// Force the use of the probe for Z-axis homing
//#define USE_PROBE_FOR_Z_HOMING
``` 

### Edit Configuration_adv.h

`#define BABYSTEPPING`

`#define HOMING_BUMP_DIVISOR { 2, 2, 8 }       // Re-Bump Speed Divisor (Divides the Homing Feedrate)`
 

Here we have to slow down bump homing speed and Z homing speed, because the endstop read from BDsensor process is not in the ISR like normal endstop. 

### Edit pins_boardname.h
Configure the pins for the SDA and SCL of the BDsensor in your pins file pins_boardname.h (E.g. `pins_PANDA_PI_V29.h`) by adding the following 3 lines:
``` 
#define  I2C_BD_SDA_PIN    PC6   // Please change to the actual number which the SDA wire is connected to your mainboard
#define  I2C_BD_SCL_PIN    PB2   // Please change to the actual number which the SCL wire is connected to your mainboard
#define  I2C_BD_DELAY  20      // this value depends on the process speed of the MCU. here 20 is for the stm32f103(72MHz), higher value for higher MCU speed.
``` 
if you want to do the auto bed leveling probe (G29) before print like normal BLtouch,
Uncomment

`#define AUTO_BED_LEVELING_BILINEAR`

and edit the value like the following
``` 
#define Z_CLEARANCE_DEPLOY_PROBE   0 // Z Clearance for Deploy/Stow
#define Z_CLEARANCE_BETWEEN_PROBES  1 // Z Clearance between probe points
#define Z_CLEARANCE_MULTI_PROBE     1 // Z Clearance between multiple probes
``` 
in the Configuration.h

## Step4. Display BD sensor value on LCD

For the printer has status display(support gcode M117) like LCD12864 or some uart screen like ender3V2 ...

## Step5. Calibration 
1)Move the Nozzle down until it just touch the bed plate(The BDsensor will use this position as the 0 point).

2)Reboot the printer, Send gcode M102 S-6,then it will move the z axis slowly up 0.1mm everytime until it reach to 3.9mm.done

you can check whether the BDsensor has been calibrated successful by sending gcode `M102   S-5 ` to read the raw calibration data from BDsensor.

There is also a Calibrate Tools to do that:https://github.com/markniu/Bed_Distance_sensor/blob/main/BD_Config_Tool.zip
![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/Read.jpg) 

Note: data value 1015 or > 1010 that means out of sensor range. if the value of the first 5 points(0~0.5mm) or more are in the range 0 to 1000, and the increased value delta is >=10 that means calibrate successful.like the graph shown above.

## Step6. Test and printing

Menu bed level


Auto bed level

## There are 3 ways to do auto bed leveling:
**1.Real time leveling at first few layers with M102**

we can easily enable or disable this auto level by sending gcode command or and adding gcode in the gcode file.

To enable bed leveling in Cura, add the M102 G-Code right below the G28 (Home All Axes) G-code in the Start G-code section of your printer’s Machine Settings.
For example `M102 S2` below the G28 , that means it will only do bed leveling below 0.2mm of Z axis height.

Send `M102 S0`or`G28`or `M18` will disable bedlevel with BDsensor,BTW,this is disabled by default.

```
M102   S-6     //Start Calibrate,before that make sure the nozzle is just touched the bed,and then the printer restarted. don't home z axis before this. 
M102   S-5     //Read raw Calibrate data
M102   S4      //Set the adjustable Z height value,e.g. M102 S4  means it will do adjusting while the Z height <=0.4mm , disable it by M102 S0.
M102   S-1     //Read sensor information
```

**2. Auto Bed Leveling with G29**

Another way to do auto bed leveling is like BLtouch with G29,just add a line G29 below G28.

## Example 
 1. [Creality V4.2.x](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/21110408.jpg)
 2. [SKR_V1.4](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/skrv1_4.jpg)  

## Install video: https://www.pandapi3d.com/post/install-bed-distance-sensor-video


###  Check the Z endstop by `M119`

Please do not Homing Z until you have checked this step or else the nozzle maybe cashes the printer bed.

This is the return message after sending M119 command(Reporting endstop status).
```
Send: M119
Recv: x:open y:open z:open
```
if the z min is not in open,please check `#define Z_MAX_ENDSTOP_HIT_STATE HIGH` in your configuration.h

 * make sure the z motors off/unlock 

 * move the z axis down with hand until the nozzle close the bed

 * send `M102 S-2`, the return value should be 0.00mm and then send M119 again, you can see the z endstop is triggered now.
 
```
Send: M119
Recv: x:open y:open z:TRIGGERED
```
 * If all above steps are right, then you can homing z axis now.

**Note:**
* Don't run M102 S-6 before you install the sensor and ready to calibrate, for after sending this gcode command the sensor will erase all the calibrate data in memory.

* If the first raw calibration data returned by the M102 S-5 is greater than 600, that means the sensor is mounted too high and needs to be remounted closer to the bed. also make sure that the second data is greater than the first data value by more than 10
    + `FAQ: if calibration data begins with 1 and second value is 9 and third 24 what that means?`
    - `that means the resolution between 0-0.1mm is only 9,and the 0.1-0.2mm is 15.so recommend to calibate again let the first resolution 0-0.1mm bigger than 10`