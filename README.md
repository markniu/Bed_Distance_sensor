## Bed Distance Sensor --- 3D printer Bed Leveling

BDsensor is a high resolution inductive Bed Distance Sensor for 3d printer auto bed leveling and real time adjustment.

it can measure the distance from bed to nozzle with distance resolution 0.01mm.

before now, there is no distance sensor available for 3D printers due to the high price, and nothing with this high resolution. All the current bed level sensors used now on 3D printers are only proximity e.g. the BLTouch, pinta, ezabl, etc.. 
 
### The Future of Bed Leveling

Faster leveling, realtime compensation, high accuracy.

1. with this sensor the printer can adjust the z axis in real time at every location, not just at probe points.

2. No need to do probe points before every print or when hot or cold, it will be automatically compensated for based on actual distance in real time.
 
3. You can do mesh bed leveling like a normal proximity sensor but much faster with this sensor, because there's no z axis down and up at every probe point.


[<img alt="alt_text"   src="https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/images/mainv.jpg" />](https://www.youtube.com/watch?v=yx8pluEu0sg)
 
 . | BDsensor | BLtouch |superPINDA
--- | --- |--- |---
Sensor type |Distance Sensor| Proximity switch | Proximity switch
Distance resolution |0.005~0.01mm| None | None
Sensor type |Non-contact | Contact |Non-contact
Bed material |Metal| Unlimited | Metal
Communication |I2C [Timing Diagram](https://github.com/markniu/Bed_Distance_sensor/wiki/Data-Protocol)| PWM/Zendstop | Zendstop
Operating Range|4mm|None|None

Accuracy video: [BDsensor VS  Dial gauge](https://youtu.be/SLDsKLupcrk)

Support main board: any board which have 2 free gpio pins,Compatible with the BLtouch	connector. 


### WiKi Installation 
https://github.com/markniu/Bed_Distance_sensor/wiki

### Marlin Firmware:
The BDsensor have been integrated to Marlin2.1.x(Since 2022.8.27),you can download the latest here: https://github.com/MarlinFirmware/Marlin What do you need is change the configure file and pins file.

### Work with Klipper
https://github.com/markniu/Bed_Distance_sensor/wiki/Installing-for-Klipper

### Calibrate:
Why calibrate?

we need to calibrate this sensor before first use or the bed plate have been changed to different materials,
Because different metal plates may have different electromagnetic properties.
normally this process will take about within one minute in 3D printer .

Calibrate steps:

1)Move the Nozzle down until it just touch the bed plate(as the 0 distance).

2)Power on the printer, Send gcode `M102 S-6`,then it will move the z axis slowly up 0.1mm everytime until it reach to 3.9mm.done


## There are 3 ways to do auto bed leveling:
**1.Real time leveling at first few layers with M102**

we can easily enable or disable this auto level by sending gcode command or and adding gcode in the gcode file.

To enable bed leveling in Cura, add the M102 G-Code right below the G28 (Home All Axes) G-code in the Start G-code section of your printer’s Machine Settings.
For example `M102 S2` below the G28 , that means it will only do bed leveling below 0.2mm of Z axis height.

Send `M102 S0`or`G28`or `M18` will disable bedlevel with BDsensor,BTW,this is disabled by default.

```
M102   S-6    // Start Calibrate,before that make sure the nozzle is just touched the bed,and then the printer restarted. 
M102   S-5    // Read raw Calibrate data
M102   S4     // Set the adjustable Z height value,e.g. M102 S4  means it will do adjusting while the Z height <=0.4mm , disable it by M102 S0.
M102   S-1    // Read sensor information
```

**2. Auto Bed Leveling with G29**

Another way to do auto bed leveling is like BLtouch with G29,just add a line G29 below G28.

**3. Combine the above Real time leveling(M102) and Auto Bed Leveling(G29)**


Project status:

https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Where to buy:

https://www.aliexpress.com/item/3256804537536912.html

www.pandapi3d.com  

[中国大陆淘宝店](https://item.taobao.com/item.htm?spm=a1z10.1-c.w4004-14344044600.5.60a16ff77NRBL5&id=684572042388)

Support Forum: 

https://www.facebook.com/groups/380795976169477   

https://www.pandapi3d.com/forum


