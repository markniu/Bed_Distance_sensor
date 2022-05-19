## BDsensor 

This is a high resolution inductive Bed Distance Sensor, it can measure the distance from bed to nozzle with distance resolution 0.01mm.
there is no distance sensor used on the 3D printer for it's high price with this high resoluion,all the bed level sensor used on 3D printer is proximity e.g. the BLTouch.
 

1. ####  with this sensor the printer can adjust the z axis in real time if the bed plate is not flat in every point,also no need to do probe points before every print.not only the plate not flat itself but also the high and uneven temperature will also cause the plate to expand and contract.

2. ####  you can do mesh bed leveling like normal proximity sensor but much faster with this sensor, because it's no z axis down and up at every probe point.so you can do more probe points in short time.

![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/516115055.png)

features |  | .
--- | --- | --- 
Distance resolution| 0.01mm | 	
Operating Range|7mm|
Repeatability|+/- 0.005mm|
Communication port| I2C | 	 
Connection| only 4 wires: GND,5V,I2C_Data,I2C_Clk
 
### Calibrate:
Send gcode G102 to Calibrate,

we need to calibrate this sensor before first use or the bed plate have been changed to different materials,
Because different metal plates are made of different materials, they may have different electromagnetic properties
normally this process will take about 1 minute in 3D printer .


Test video: https://youtu.be/MMPM2GHVfew

Project status:https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Store: www.pandapi3d.com  will come soon

Support: https://www.facebook.com/groups/380795976169477
