## Auto bed level --- BDsensor

This is a high resolution inductive **Bed Distance Sensor**, it can measure the distance from bed to nozzle with distance resolution 0.01mm.
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
 
### Calibrate:
Why calibrate?

we need to calibrate this sensor before first use or the bed plate have been changed to different materials,
Because different metal plates are made of different materials, they may have different electromagnetic properties.
normally this process will take about within one minute in 3D printer .

Calibrate steps:

1)Move the Nozzle down until it just touch the bed plate(as the 0 distance).

2)Power on the printer, Send gcode `G102 T-6`,then it will move the z axis slowing up 7mm to calibrate itself until it stop.


### Enable/Disable 
we can easiy enable or disable this auto level by sending gcode command or and adding gcode in the gcode file.

normally we want to disable it after first layer for example 0.2mm, so we can add this gcode `G102 T2` in the begain of the gcode file.

send `G102 T0` to disable BDsensor,BTW,the default is disabled.


Test video: https://youtu.be/MMPM2GHVfew

Project status:https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Store: www.pandapi3d.com  will come soon

Support: https://www.facebook.com/groups/380795976169477
