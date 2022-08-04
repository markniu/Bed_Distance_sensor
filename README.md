## Auto bed level --- Bed Distance Sensor

BDsensor is a non-contact dial gauge

BDsensor is a high resolution inductive **Bed Distance Sensor**, it can measure the distance from bed to nozzle with distance resolution 0.01mm in real time.
there is now no distance sensor used on the **3D printer** for it's high price with this high resoluion for example [IWFK20Z8704](https://www.walkerindustrial.com/IWFK-20Z8704-S35A-BAUMER-10155694-p/iwfk-20z8704-s35a.htm) is $575,all the bed level sensor used now on 3D printer is proximity switch e.g. the BLTouch.
 
### Benefits
1,Menu Bed leveling: The Distance between bed and hotend will be displayed on the screen while you do menu bed leveling so you do not need paper.[video](https://youtu.be/5Hh-R__WlqY)

2,Auto Bed leveling:there is no Z axis up and down while do mesh bed leveling probe,so whole bed leveling will be very fast.[Test Video](https://video.wixstatic.com/video/0d0edf_f2f0b38c765e43c680faaa4f673a74b6/480p/mp4/file.mp4)

3,Auto adjust hotend at every point in real time.[video](https://youtu.be/4qdCDU4c2ac)

4, Easy to use, for there is no Z offset setting. 


![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/516115055.jpg)
 
 . | BDsensor | BLtouch |superPINDA
--- | --- |--- |---
Sensor type |Distance Sensor| Proximity switch | Proximity switch
Distance resolution |0.005~0.01mm| None | None
Sensor type |Non-contact | Contact |Non-contact
Bed material |Metal| Unlimited | Metal
Communication |I2C [Timing Diagram](https://github.com/markniu/Bed_Distance_sensor/blob/main/doc/0220517153950.png)| PWM/Zendstop | Zendstop
Operating Range|4mm|None|None

Accuracy video: [BDsensor VS  Dial gauge](https://youtu.be/SLDsKLupcrk)

Support main board: any board which have 2 free gpio pins,Compatible with the BLtouch	connector. 
![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/FastBD.gif)

### Arduino Testing code:
here is the arduino testing code,it's very simple, you can read the distance data from BDsensor after init the communication port.

```
void setup() {
  delay(500);
  // init the communication port.
  BD_SENSOR_I2C.i2c_init(I2C_BED_SDA,I2C_BED_SCL,0x3C,10);
  Serial.begin(115200);
}

void loop() {
    unsigned short read_data=0;
    //read distance from BDsensor
    read_data=BD_SENSOR_I2C.BD_i2c_read();    
    if(BD_SENSOR_I2C.BD_Check_OddEven(read_data)==0)
      printf("Data Check error!\n");
    else
    {
      Distance=(read_data&0x3ff)/100.0;
      //display the Distance
      sprintf(tmp_1,"Distance:%.2f mm\n",Distance);
      printf(tmp_1);
    }
    delay(100);
}
```


### Marlin Firmware:
This is a new Sensor, so now only support Marlin firmware,we just have pulled the code to Marlin and have sent hardware to scott:
https://github.com/MarlinFirmware/Marlin/pull/24554

Enable this features by `#define BD_SENSOR 1` in the Configuration.h , enable `#define BABYSTEPPING` in the Configuration_adv.h and add `lib_deps = markyue/Panda_SoftMasterI2C` in the ini file,and also do not forget to configure the pins for example:
```
#define  I2C_BD_SDA_PIN    PC6
#define  I2C_BD_SCL_PIN    PB2
#define  I2C_BD_DELAY  10
```
Have been Tested Marlin Firmware:[PandaPi](https://github.com/markniu/PandaPi/tree/master/Marlin2.x/pandapi)  (run marlin on raspberry pi)  , [PandaPi standalone mode](https://github.com/markniu/PandaPi/tree/master/Marlin2.x/standalone/Marlin-2.0.9.3) (run marlin on stm32), [PandaZHU/M4](https://github.com/markniu/PandaZHU) (ESP32 marlin),



### Main Board:
 Any board which have 2 free gpio pins would work, or have BLtouch connector.
 
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

Send `M102 S0`or`G28`or `M18` will disable bedlevel with BDsensor,BTW,this is disabled by default.

```
//M102   S-5     Read raw Calibrate data
//M102   S-6     Start Calibrate 
//M102   S4      Set the adjustable Z height value,e.g. M102 S4  means it will do adjusting while the Z height <=0.4mm , disable it by M102 S0.
//M102   S-1     Read sensor information
```

### Raw Data
Here is the data diagram of this sensor, we can see that the distance resolution can be <0.005 below the 5mm.
![](https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/data.jpg)


Test video: 

[<img alt="alt_text"   src="https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/135228.jpg" />](https://youtu.be/5Hh-R__WlqY)

[<img alt="alt_text"   src="https://raw.githubusercontent.com/markniu/Bed_Distance_sensor/main/doc/135204.jpg" />](https://youtu.be/4qdCDU4c2ac)


Project status:

https://hackaday.io/project/185096-0006mm-distance-resolution-sensor-for-3d-printer

Where to buy:

www.pandapi3d.com  

Support Forum: 

https://www.facebook.com/groups/380795976169477   

https://www.pandapi3d.com/forum
