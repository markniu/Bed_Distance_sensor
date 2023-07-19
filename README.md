## Bed Distance Sensor --- 3D printer Bed Leveling

[BDsensor](https://www.pandapi3d.com/bdsensor) is a high resolution inductive Bed Distance Sensor for 3d printer auto bed leveling and real time adjustment.

it can measure the distance from bed to nozzle with distance resolution 0.01mm.
 
### The Future of Bed Leveling

Faster leveling, realtime compensation, high accuracy.

1. with this sensor the printer can adjust the z axis in real time at every location, not just at probe points.(now only the marlin support this feature)

2. No need to do probe points before every print or when hot or cold, it will be automatically compensated for based on actual distance in real time.
 
3. You can do mesh bed leveling like a normal proximity sensor but much faster with this sensor, because there's no z axis down and up at every probe point.


[<img alt="alt_text"   src="https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/mainv.jpg" />](https://www.youtube.com/watch?v=yx8pluEu0sg)
 
 . | BDsensor | BLtouch |superPINDA
--- | --- |--- |---
Sensor type |Distance Sensor| Proximity switch | Proximity switch
Distance resolution |0.005~0.01mm| None | None
Bed material |Metal| Unlimited | Metal
Communication |I2C [Timing Diagram](https://github.com/markniu/Bed_Distance_sensor/wiki/Data-Protocol)| PWM/Zendstop | Zendstop
Operating Range|4mm|None|None
Accuracy video: [BDsensor VS  Dial gauge](https://youtu.be/SLDsKLupcrk)


### Hardware:
Support any mainboard(8 bit or 32 bit) or Can bus module

Wire: Compatible with the BLtouch	connector. 


### Software
 . | Marlin | Klipper
--- | --- |---  
Real Time leveling |Support| No  
Fast probe |Support| Support 
Fast probe(No toolhead stop probe)|Support | Support 
Distance display |Support | Support 
Can bus toolhead|No | Support 



Marlin installation: https://github.com/markniu/Bed_Distance_sensor/wiki/Installing-Bed-Distance-Sensor

Klipper installation: https://github.com/markniu/Bed_Distance_sensor/wiki/Installing-for-Klipper


Project status: https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Where to buy: [https://www.pandapi3d.com/bdsensor](https://www.pandapi3d.com/bdsensor) , if you are in China please purchase it here
 [中国大陆淘宝店](https://item.taobao.com/item.htm?spm=a1z10.1-c.w4004-14344044600.5.60a16ff77NRBL5&id=684572042388)


<img alt="alt_text"   src="https://github.com/markniu/Bed_Distance_sensor/blob/new/doc/images/map.jpg" />
