## Auto bed level --- Bed Distance Sensor

BDsensor is a high resolution inductive **Bed Distance Sensor**, it can measure the distance from bed to nozzle with distance resolution 0.01mm.
there is now no distance sensor used on the **3D printer** for it's high price with this high resoluion for example [IWFK20Z8704](https://www.walkerindustrial.com/IWFK-20Z8704-S35A-BAUMER-10155694-p/iwfk-20z8704-s35a.htm),all the bed level sensor used now on 3D printer is proximity e.g. the BLTouch.
 
### Distance resolution: 0.01mm
### Sense and adjust Z at every point in real time
### No Z offset setting, no probe points

1. with this sensor the printer can adjust the z axis in real time if the bed plate is not flat in every point,also no need to do probe points before every print.not only the plate not flat itself but also the high and uneven temperature will also cause the plate to expand and contract.

2. you can do mesh bed leveling like normal proximity sensor but much faster with this sensor, because it's no z axis down and up at every probe point.so you can do more probe points in short time.

![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/516115055.png)

features |  | .
--- | --- | --- 
Distance resolution| 0.01mm | 	
Operating Range|7mm|
Repeatability|+/- 0.005mm|
Communication port| I2C | 	 
Support bed plate| only support metal plate | 	 
Connection| only 4 wires: GND,5V,I2C_Data,I2C_Clk
Support main board| any board which have 2 free gpio connector | 	 
 
### Firmware:
This is a new Sensor, so now only support Marlin firmware,we just have pulled the code to Marlin:
https://github.com/MarlinFirmware/Marlin/pull/24303

### Main Board:
 Any board which have 2 free gpio would work, now we have tested it on the stm32 and esp32 based board.
 
### Calibrate:
Why calibrate?

we need to calibrate this sensor before first use or the bed plate have been changed to different materials,
Because different metal plates may have different electromagnetic properties.
normally this process will take about within one minute in 3D printer .

Calibrate steps:

1)Move the Nozzle down until it just touch the bed plate(as the 0 distance).

2)Power on the printer, Send gcode `M102 S-6`,then it will move the z axis slowing up 7mm to calibrate itself until it stop.


### Enable/Disable 
we can easily enable or disable this auto level by sending gcode command or and adding gcode in the gcode file.

To enable bed leveling in Cura, add the M102 G-Code right below the G28 (Home All Axes) G-code in the Start G-code section of your printer’s Machine Settings.
For example `M102 S2` below the G28 , that means it will only do bed leveling below 0.2mm of Z axis height.

send `M102 S0` to disable bedlevel with BDsensor or after homing the printer with `G28` ,BTW,the default is disabled.

```
//M102   S-5     Read raw Calibrate data
//M102   S-6     Start Calibrate 
//M102   S4      Set the adjustable Z height value,e.g. M102 S4  means it will do adjusting while the Z height <=0.4mm , disable it by M102 S0.
//M102   S-1     Read sensor information
```

Test video: https://youtu.be/MMPM2GHVfew

Project status:https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Store: www.pandapi3d.com  

Support: https://www.facebook.com/groups/380795976169477
