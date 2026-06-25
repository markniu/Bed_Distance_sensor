##  The Future of 3D printer Bed Leveling

[BDsensor](https://www.pandapi3d.com/bdsensor) is the first eddy bed level sensor that can work with 3D printer since 2022 and do fast bed leveling and adjust z axis in real time.
it can measure the bed distance at any point in real time without moving the z axis up and down.

Faster leveling, realtime compensation, high accuracy.

### Hardware:
* Shielded Coil
* Work with any GPIO pins(softI2C)
* The lightest and samllest eddy probe, 1.5g:
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
2025.1  |             | [Auto calibration](https://www.pandapi3d.com/post/new-feature-for-bdsensor)

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

| Channel | Link |
|---|---|
| Discord | [discord.gg/z6ahddnGVU](https://discord.gg/z6ahddnGVU) |
| Facebook group | [facebook.com/groups/380795976169477](https://facebook.com/groups/380795976169477) |
| Where to buy | 🇺🇸 [fabreeko.com](https://fabreeko.com) <br>  🌏 [pandapi3d.com](https://pandapi3d.com) <br> 🇨🇳 [淘宝](https://shop62248922.taobao.com/)  <br> 🇩🇰 [3do.dk](https://3do.dk/12-abl-probe) |

 

