##  The Future of 3D printer Bed Leveling

[BDsensor](https://www.pandapi3d.com/bdsensor) is the first distance sensor that can work with 3D printer and do fast bed leveling and adjust z axis in real time.
it can measure the bed distance at any point in real time without moving the z axis up and down.

Faster leveling, realtime compensation, high accuracy.

1. No need to do probe points before every print, it will be automatically compensated for based on actual distance in real time.
 
2. You can do bed mesh leveling like a normal proximity sensor but much faster with this BDsensor.
   
3. Easy manual bed level adjustment thanks to ability to display the live sensor distance measurement on your screen.

Sensor type: inductive, eddy, can only measure the metal plate.

Range: 4mm (for different metal the range should be different, the normal pei steel plate is 4mm, but aluminum is about 2mm)


### Hardware:
* It can connect to most motherboards or GPIOs on the RaspberryPi
* Easy to install:
  small and light; can be installed anywhere, even surrounding metal above
* Support Can bus module
* Support high temperature chamber(120C) with the long cable probe [BDsensorM](https://github.com/markniu/Bed_Distance_sensor/wiki/BDsensor-M).
### Hardware Version
 . | hardware | firmware/software
--- | --- |---  
2022.6 | BDsensor VA, V1.0 | V1.0  
2023.4 | BDsensor VB, V1.1, the connector was changed | V1.1, support self reboot
2023.11 | BDsensor VB, V1.3, Upgrade the MCU to STM32 | V1.2, for stm32
2024.2  |              |V1.2c, [Support nozzle collision sensing](https://github.com/markniu/Bed_Distance_sensor/wiki/Collision-sensing)
2024.3  | BDsensorM V1.0             | V1.2c
2024.7  | BDsensorM V1.1             |  V1.2c, [Pins order](https://github.com/markniu/Bed_Distance_sensor/issues/178#issuecomment-2319621934) are changed in EXP1 connector
2025.1  |             | Auto calibration

### Software
 . | Marlin | Klipper
--- | --- |---  
Real Time leveling |:heavy_check_mark:| :heavy_check_mark:  
Fast bed mesh |:heavy_check_mark:| :heavy_check_mark: 
Fast bed mesh(No toolhead stop)|:heavy_check_mark: | :heavy_check_mark: 
Distance display |:heavy_check_mark: | :heavy_check_mark: 
Can bus toolhead|No | :heavy_check_mark: 
standby mode automatic while printing|:heavy_check_mark: | :heavy_check_mark: 
[KAMP](https://github.com/kyleisah/Klipper-Adaptive-Meshing-Purging) Adaptive Meshing & Purging |No | :heavy_check_mark: 
nozzle collision sensing|[Detail](https://github.com/markniu/Bed_Distance_sensor/wiki/Collision-sensing-for-Marlin):heavy_check_mark:  | :heavy_check_mark: 

### Benefit of Collision sensing
1.  **Auto z offset calibration**.
2.  **Overcome the temperature drift**.
     The temperature drift will only change the z_offset, it will rise or low the whole bed mesh, but does not change the bed mesh with this BDsensor. that means the bed mesh is the same even with [different temperature](https://www.pandapi3d.com/post/nozzle-collision-sensing-with-bdsensor).
3. Repeatability: **+/-0.005mm**
4. No external hardware and easy to adjust

### Benefit of version M:

1.  For High temperature chamber up to [120C](https://github.com/markniu/Bed_Distance_sensor/wiki/BDsensor-M)
2.  The lightest probe, 1.5g

### Document  : [WiKi](https://github.com/markniu/Bed_Distance_sensor/wiki)

[History](https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer)

#### Mount STL:  [BLtouch_Compatible](https://www.thingiverse.com/thing:6098131) | [VzBoT](https://discord.com/channels/829828765512106054/1163237892957671424) | [Stealthburner Voron](https://www.printables.com/model/831679-lazy-bd-sensor-adapter-for-stealthburner-voron)

### Support
 https://www.facebook.com/groups/380795976169477


#### Where to buy:
 [pandapi3d.com](https://www.pandapi3d.com) , [elecrow](https://www.elecrow.com/bd-sensor.html) , [淘宝店](https://item.taobao.com/item.htm?spm=a1z10.1-c.w4004-14344044600.5.60a16ff77NRBL5&id=684572042388)

 

